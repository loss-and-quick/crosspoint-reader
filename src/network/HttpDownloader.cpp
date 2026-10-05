#include "HttpDownloader.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <ResumableFetch.h>
#include <esp_rom_crc.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>

#include "HttpResume.h"
#include "WifiPowerSaveGuard.h"

extern "C" void wolfSSL_Arduino_Serial_Print(const char* const msg) { LOG_DBG("WOLFSSL", "%s", msg); }

namespace {
// Per-socket-op timeout. Some OPDS download endpoints are slow to send headers
// (>15s) and chunked catalogs stall mid-body, so 15s killed them. 60s gives
// slow servers room.
constexpr int HTTP_TIMEOUT_MS = 60000;

constexpr int MAX_REDIRECTS = 5;

// postForm response cap: token responses are small JSON; a misbehaving server
// must not balloon the heap.
constexpr size_t MAX_RESPONSE_BYTES = 4096;

void applyCommonClientSetup(freeink::SecureHttpClient& http) {
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setInsecure();
  // setUserAgent replaces SecureHttpClient's built-in UA; addHeader would
  // append a second User-Agent header, which strict servers reject (aiohttp
  // answers 400 "Duplicate 'User-Agent' header found").
  http.setUserAgent("CrossPoint-ESP32-" CROSSPOINT_VERSION);
}

// Written after every request, and read by the UI right after a failure.
int g_lastStatus = 0;
// Whether the last answer came from another origin than the request started at.
bool g_lastRedirected = false;

// What a resumable download adds to a single runGetSecure() call.
struct Transfer {
  size_t startOffset = 0;  // bytes the sink already holds
  std::string ifRange;     // validator sent with every ranged request, if known
  // Client of the attempt in flight. Valid inside the sink callbacks, which is
  // where the response headers are read.
  const freeink::SecureHttpClient* client = nullptr;
  int status = 0;    // last HTTP status
  size_t total = 0;  // whole-resource size when the server reported it
};

// All HTTP(S) fetches go through wolfSSL (the firmware's only TLS stack: it
// speaks TLS 1.3 and reads large bodies reliably). Plain-http URLs still use a
// WiFiClient here, so this is safe for non-TLS targets too. A body cut short
// mid-transfer resumes with a Range request (see ResumableFetch.h).
HttpDownloader::DownloadError runGetSecure(const std::string& url, const std::string& username,
                                           const std::string& password,
                                           const std::vector<HttpDownloader::Header>& headers,
                                           const freeink::FetchSink& sink, const bool* cancelFlag = nullptr,
                                           size_t* bytesOut = nullptr, const bool downgradeRedirectsToHttp = false,
                                           Transfer* transfer = nullptr) {
  WifiPowerSaveGuard psGuard;
  freeink::FetchOptions options;
  options.redirectToHttp = downgradeRedirectsToHttp;
  if (transfer) options.startOffset = transfer->startOffset;
  bool answeredBySameOrigin = true;
  const freeink::FetchResult result = freeink::fetchResumable(
      url, options,
      [&](freeink::SecureHttpClient& http, const bool sameOrigin) {
        answeredBySameOrigin = sameOrigin;
        applyCommonClientSetup(http);
        if (transfer) {
          // The validator belongs to the entity, not the origin: it goes to
          // whichever server finally answers.
          transfer->client = &http;
          if (!transfer->ifRange.empty()) http.addHeader("If-Range", transfer->ifRange);
        }
        // Credentials and caller headers stay with the starting origin; a
        // redirect elsewhere (or to plain http) gets neither.
        if (sameOrigin) {
          if (!username.empty() && !password.empty()) http.setBasicAuth(username, password);
          for (const auto& h : headers) http.addHeader(h.first, h.second);
        }
        LOG_DBG("HTTP", "wolfSSL GET: %s (heap %u, max block %u)", url.c_str(), (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
      },
      sink, [cancelFlag] { return cancelFlag && *cancelFlag; });
  if (bytesOut) *bytesOut = result.bytes;
  // FetchResult::status is -1 when no response arrived (bad URL, DNS, connect, TLS).
  g_lastStatus = result.status < 0 ? 0 : result.status;
  g_lastRedirected = !answeredBySameOrigin;
  if (transfer) {
    transfer->status = result.status;
    transfer->total = result.total;
  }

  if (result.aborted) return HttpDownloader::ABORTED;
  if (result.stopped) return HttpDownloader::FILE_ERROR;
  if (result.status == 401 || result.status == 403) {
    LOG_ERR("HTTP", "wolfSSL request unauthorized: status %d: %s", result.status, url.c_str());
    return HttpDownloader::UNAUTHORIZED;
  }
  if (result.status < 200 || result.status >= 300) {
    LOG_ERR("HTTP", "wolfSSL request failed: status %d: %s", result.status, url.c_str());
    return HttpDownloader::HTTP_ERROR;
  }
  if (!result.complete) {
    LOG_ERR("HTTP", "wolfSSL incomplete: got %zu of %zu bytes", result.bytes, result.total);
    return HttpDownloader::HTTP_ERROR;
  }
  return HttpDownloader::OK;
}

}  // namespace

int HttpDownloader::lastStatus() { return g_lastStatus; }

bool HttpDownloader::lastAnswerRedirected() { return g_lastRedirected; }

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password, const std::vector<Header>& headers) {
  return fetchUrl(
      url, [&outContent](const uint8_t* data, size_t len) { return outContent.write(data, len) == len; }, username,
      password, headers);
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username,
                              const std::string& password, const std::vector<Header>& headers) {
  LOG_DBG("HTTP", "Fetching: %s", url.c_str());
  freeink::FetchSink sink;
  sink.write = onData;
  return runGetSecure(url, username, password, headers, sink) == OK;
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const FetchOptions& options) {
  // OPDS fetches need what fetchResumable withholds: the final HTTP status
  // and a 401 body (auth documents are served as 401 bodies). Direct GET with
  // a manual redirect loop; catalog feeds are small enough that Range resume
  // isn't worth carrying here (books go through downloadToFile, which has it).
  WifiPowerSaveGuard psGuard;
  std::string current = url;
  const std::string startOrigin = freeink::fetchOrigin(url);
  int status = 0;
  bool sinkStopped = false;
  for (int hop = 0; hop <= MAX_REDIRECTS; ++hop) {
    freeink::SecureHttpClient http;
    if (!http.begin(current)) {
      LOG_ERR("HTTP", "bad URL: %s", current.c_str());
      status = 0;
      break;
    }
    applyCommonClientSetup(http);
    // Credentials belong to the starting origin only: a redirect to another
    // origin (a CDN) must not receive them.
    if (freeink::fetchOrigin(current) == startOrigin) {
      if (!options.bearer.empty()) {
        http.addHeader("Authorization", "Bearer " + options.bearer);
      } else if (!options.username.empty() && !options.password.empty()) {
        http.setBasicAuth(options.username, options.password);
      }
      for (const auto& h : options.headers) http.addHeader(h.first, h.second);
    }
    if (!options.accept.empty()) http.addHeader("Accept", options.accept);
    if (!options.acceptLanguage.empty()) http.addHeader("Accept-Language", options.acceptLanguage);

    LOG_DBG("HTTP", "wolfSSL GET: %s", current.c_str());
    status = http.GET([&](const uint8_t* data, size_t len) {
      const int st = http.getStatus();
      const bool wanted = (st >= 200 && st < 300) || (options.captureErrorBody && st == 401);
      if (!wanted) return true;  // redirect or error body: drain
      if (!onData(data, len)) {
        sinkStopped = true;
        return false;
      }
      return true;
    });
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
      const std::string location = http.getHeader("location");
      if (location.empty() || !freeink::SecureHttpClient::resolveUrl(current, location, current)) break;
      continue;
    }
    break;
  }
  g_lastStatus = status > 0 ? status : 0;
  g_lastRedirected = freeink::fetchOrigin(current) != startOrigin;
  if (options.statusOut) *options.statusOut = g_lastStatus;
  return status >= 200 && status < 300 && !sinkStopped;
}

bool HttpDownloader::postForm(const std::string& url, const std::string& formBody, std::string& outResponse,
                              int* statusOut) {
  WifiPowerSaveGuard psGuard;
  outResponse.clear();
  freeink::SecureHttpClient http;
  if (!http.begin(url)) {
    LOG_ERR("HTTP", "bad URL: %s", url.c_str());
    if (statusOut) *statusOut = 0;
    return false;
  }
  applyCommonClientSetup(http);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  LOG_DBG("HTTP", "wolfSSL POST: %s", url.c_str());
  const int status = http.sendRequest("POST", reinterpret_cast<const uint8_t*>(formBody.data()), formBody.size(),
                                      [&outResponse](const uint8_t* data, size_t len) {
                                        const size_t room =
                                            MAX_RESPONSE_BYTES - std::min(outResponse.size(), MAX_RESPONSE_BYTES);
                                        outResponse.append(reinterpret_cast<const char*>(data), std::min(len, room));
                                        return true;
                                      });
  if (statusOut) *statusOut = status > 0 ? status : 0;
  return status >= 200 && status < 300;
}

HttpDownloader::DownloadError HttpDownloader::postToFile(const std::string& url, const std::string& body,
                                                         const std::string& contentType, const std::string& destPath,
                                                         ProgressCallback progress, const bool* cancelFlag) {
  LOG_DBG("HTTP", "POST download: %s -> %s", url.c_str(), destPath.c_str());
  WifiPowerSaveGuard psGuard;

  const std::string partPath = destPath + ".part";
  Storage.remove(partPath.c_str());
  HalFile file;
  if (!Storage.openFileForWrite("HTTP", partPath.c_str(), file)) {
    LOG_ERR("HTTP", "Failed to open file for writing");
    return FILE_ERROR;
  }

  freeink::SecureHttpClient http;
  if (!http.begin(url)) {
    LOG_ERR("HTTP", "bad URL: %s", url.c_str());
    return HTTP_ERROR;
  }
  applyCommonClientSetup(http);
  http.addHeader("Content-Type", contentType);

  size_t downloaded = 0;
  bool writeFailed = false;
  const int status = http.sendRequest(
      "POST", reinterpret_cast<const uint8_t*>(body.data()), body.size(),
      [&](const uint8_t* data, size_t len) {
        const int st = http.getStatus();
        if (st < 200 || st >= 300) return true;  // drain the error body
        if (file.write(data, len) != len) {
          writeFailed = true;
          return false;
        }
        downloaded += len;
        if (progress && http.hasContentLength()) progress(downloaded, http.getContentLength());
        return true;
      },
      [cancelFlag] { return cancelFlag && *cancelFlag; });
  // Close before any remove() on the same path; DESTRUCTOR_CLOSES_FILE would
  // otherwise close only after the remove.
  if (file.isOpen()) file.close();

  DownloadError result = OK;
  if (http.aborted()) {
    result = ABORTED;
  } else if (writeFailed) {
    result = FILE_ERROR;
  } else if (status < 200 || status >= 300) {
    LOG_ERR("HTTP", "POST failed: status %d: %s", status, url.c_str());
    result = HTTP_ERROR;
  } else if (downloaded == 0) {
    LOG_ERR("HTTP", "no data received");
    result = HTTP_ERROR;
  } else if (http.hasContentLength() && downloaded != http.getContentLength()) {
    LOG_ERR("HTTP", "POST response incomplete: %zu of %zu bytes", downloaded, (size_t)http.getContentLength());
    result = HTTP_ERROR;
  }
  if (result != OK) {
    Storage.remove(partPath.c_str());
    return result;
  }
  if (!Storage.replaceFile(partPath.c_str(), destPath.c_str())) {
    LOG_ERR("HTTP", "Failed to move download into place: %s", destPath.c_str());
    Storage.remove(partPath.c_str());
    return FILE_ERROR;
  }
  LOG_DBG("HTTP", "Downloaded %zu bytes", downloaded);
  return OK;
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, const bool* cancelFlag,
                                                             const std::string& username, const std::string& password,
                                                             const std::vector<Header>& headers,
                                                             bool downgradeRedirectsToHttp) {
  LOG_DBG("HTTP", "Downloading: %s -> %s", url.c_str(), destPath.c_str());

  // Stage in <dest>.part: a failed or cancelled download never replaces an
  // existing copy, and a partial file never sits under the real name.
  const std::string partPath = destPath + ".part";
  Storage.remove(partPath.c_str());
  HalFile file;
  if (!Storage.openFileForWrite("HTTP", partPath.c_str(), file)) {
    LOG_ERR("HTTP", "Failed to open file for writing");
    return FILE_ERROR;
  }

  freeink::FetchSink sink;
  sink.write = [&file](const uint8_t* data, size_t len) { return file.write(data, len) == len; };
  // Reopening for write truncates: the server restarted the body from byte 0.
  sink.rewind = [&file, &partPath] {
    file.close();
    return Storage.openFileForWrite("HTTP", partPath.c_str(), file);
  };
  sink.progress = progress;

  size_t downloaded = 0;
  const DownloadError result =
      runGetSecure(url, username, password, headers, sink, cancelFlag, &downloaded, downgradeRedirectsToHttp);
  // Close before any remove() on the same path; DESTRUCTOR_CLOSES_FILE would
  // otherwise close only after the remove. A failed rewind leaves no open handle.
  if (file.isOpen()) file.close();

  if (result != OK) {
    Storage.remove(partPath.c_str());
    return result;
  }
  if (downloaded == 0) {
    LOG_ERR("HTTP", "no data received");
    Storage.remove(partPath.c_str());
    return HTTP_ERROR;
  }
  if (!Storage.replaceFile(partPath.c_str(), destPath.c_str())) {
    LOG_ERR("HTTP", "Failed to move download into place: %s", destPath.c_str());
    Storage.remove(partPath.c_str());
    return FILE_ERROR;
  }
  LOG_DBG("HTTP", "Downloaded %zu bytes", downloaded);
  return OK;
}

namespace {
// Sidecar text and the CRC pass over a resumed prefix go through this buffer.
// One SD sector: SdFat reads aligned whole sectors straight into it instead of
// copying through its sector cache.
constexpr size_t RESUME_IO_CHUNK = 512;
static_assert(RESUME_IO_CHUNK >= http_resume::MAX_META_LEN, "sidecar must fit the I/O buffer");
// Sync the ".part" periodically so a reset or power loss keeps most of the
// transfer; a network error closes (and so syncs) the file anyway.
constexpr size_t RESUME_SYNC_INTERVAL = 256 * 1024;

// Per-call state, heap-allocated so the transport's body reader (which already
// holds a 2 KB stack buffer) keeps its headroom.
struct ResumeContext {
  std::string partPath;
  std::string metaPath;
  HalFile file;
  http_resume::Meta meta;
  uint32_t urlHash = 0;
  uint32_t crc = 0;
  size_t sinceSync = 0;
  bool verifyCrc = false;
  bool metaPending = false;  // sidecar must describe the response before its first body byte
  bool fileError = false;
  uint8_t io[RESUME_IO_CHUNK] = {};
};

// HalFile::close() asserts on a handle that was never opened.
void closeFile(HalFile& file) {
  if (file.isOpen()) file.close();
}

bool loadMeta(ResumeContext& ctx) {
  HalFile f;
  if (!Storage.exists(ctx.metaPath.c_str()) || !Storage.openFileForRead("HTTP", ctx.metaPath.c_str(), f)) return false;
  const int n = f.read(ctx.io, http_resume::MAX_META_LEN);
  if (n <= 0) return false;
  http_resume::Meta meta;
  if (!http_resume::parseMeta(reinterpret_cast<const char*>(ctx.io), static_cast<size_t>(n), meta)) return false;
  if (meta.urlHash != ctx.urlHash) return false;
  ctx.meta = meta;
  return true;
}

bool writeMeta(ResumeContext& ctx) {
  const size_t len = http_resume::formatMeta(ctx.meta, reinterpret_cast<char*>(ctx.io), sizeof(ctx.io));
  HalFile f;
  if (len == 0 || !Storage.openFileForWrite("HTTP", ctx.metaPath.c_str(), f)) return false;
  return f.write(ctx.io, len) == len;
}

void discardPartial(ResumeContext& ctx) {
  // Close before remove(); DESTRUCTOR_CLOSES_FILE would close only afterwards.
  closeFile(ctx.file);
  if (Storage.exists(ctx.partPath.c_str())) Storage.remove(ctx.partPath.c_str());
  if (Storage.exists(ctx.metaPath.c_str())) Storage.remove(ctx.metaPath.c_str());
  ctx.meta = http_resume::Meta{};
}

// Opens ".part" positioned at offset. With CRC verification the prefix is read
// once to seed the running CRC, which also leaves the file positioned at its end.
bool openForResume(ResumeContext& ctx, const size_t offset) {
  ctx.file = Storage.open(ctx.partPath.c_str(), O_RDWR);
  if (!ctx.file) return false;
  if (!ctx.verifyCrc) return ctx.file.seekSet(offset);
  size_t done = 0;
  while (done < offset) {
    const size_t want = offset - done < sizeof(ctx.io) ? offset - done : sizeof(ctx.io);
    const int n = ctx.file.read(ctx.io, want);
    if (n <= 0) return false;
    ctx.crc = esp_rom_crc32_le(ctx.crc, ctx.io, static_cast<uint32_t>(n));
    done += static_cast<size_t>(n);
  }
  return true;
}

// Whether a failed transfer's ".part" is still worth continuing. A 4xx other
// than auth, timeout and rate limiting means the resource is gone or the
// request is wrong, so the bytes will not be reused.
bool keepPartialAfter(const HttpDownloader::DownloadError error, const int status) {
  if (error == HttpDownloader::ABORTED || error == HttpDownloader::UNAUTHORIZED) return true;
  return !(status >= 400 && status < 500 && status != 408 && status != 429);
}
}  // namespace

HttpDownloader::DownloadError HttpDownloader::downloadToFileResumable(
    const std::string& url, const std::string& destPath, const ResumeOptions& options, ProgressCallback progress,
    const bool* cancelFlag, const std::string& username, const std::string& password,
    const std::vector<Header>& headers) {
  LOG_DBG("HTTP", "Downloading (resumable): %s -> %s", url.c_str(), destPath.c_str());

  auto ctx = makeUniqueNoThrow<ResumeContext>();
  if (!ctx) {
    LOG_ERR("HTTP", "OOM: resume context");
    return FILE_ERROR;
  }
  ResumeContext* const c = ctx.get();
  c->partPath = destPath + ".part";
  c->metaPath = c->partPath + ".meta";
  c->verifyCrc = options.verifyCrc32;
  c->urlHash = http_resume::urlHash(url.c_str());

  http_resume::LocalState local;
  local.expectedSize = options.expectedSize;
  if (Storage.exists(c->partPath.c_str())) {
    HalFile part;
    if (Storage.openFileForRead("HTTP", c->partPath.c_str(), part)) local.partSize = part.fileSize64();
  }
  local.metaValid = loadMeta(*c);
  local.metaTotal = c->meta.total;

  Transfer transfer;
  freeink::FetchSink sink;
  sink.write = [c, &transfer, &options](const uint8_t* data, size_t len) {
    if (c->metaPending) {
      // First body byte of a fresh (or restarted) entity: record what resumes it.
      c->metaPending = false;
      std::string etag;
      std::string lastModified;
      uint64_t total = options.expectedSize;
      if (transfer.client) {
        etag = transfer.client->getHeader("etag");
        lastModified = transfer.client->getHeader("last-modified");
        if (transfer.client->hasContentLength()) total = transfer.client->getContentLength();
      }
      c->meta = http_resume::Meta{};
      c->meta.urlHash = c->urlHash;
      c->meta.total = total;
      http_resume::selectValidator(etag.c_str(), lastModified.c_str(), c->meta.validator, sizeof(c->meta.validator));
      transfer.ifRange = c->meta.validator;
      if (!writeMeta(*c)) {
        c->fileError = true;
        return false;
      }
    }
    if (c->verifyCrc) c->crc = esp_rom_crc32_le(c->crc, data, static_cast<uint32_t>(len));
    if (c->file.write(data, len) != len) {
      c->fileError = true;
      return false;
    }
    c->sinceSync += len;
    if (c->sinceSync >= RESUME_SYNC_INTERVAL) {
      c->file.flush();
      c->sinceSync = 0;
    }
    return true;
  };
  // The server ignored Range (or the If-Range validator no longer matches) and
  // sends the whole body: truncate and describe the new entity before byte 0.
  sink.rewind = [c] {
    closeFile(c->file);
    if (!Storage.openFileForWrite("HTTP", c->partPath.c_str(), c->file)) {
      c->fileError = true;
      return false;
    }
    c->crc = 0;
    c->sinceSync = 0;
    c->metaPending = true;
    return true;
  };
  sink.progress = progress;

  // At most two requests: a ranged one and, if the server cannot satisfy it, a
  // full one.
  DownloadError result = HTTP_ERROR;
  size_t size = 0;
  bool alreadyComplete = false;
  for (int attempt = 0; attempt < 2; ++attempt) {
    const http_resume::RequestPlan plan = http_resume::planRequest(local);
    c->crc = 0;
    c->sinceSync = 0;
    c->fileError = false;
    bool resuming = plan.resume;
    if (resuming && !openForResume(*c, static_cast<size_t>(plan.offset))) {
      LOG_ERR("HTTP", "Cannot reopen %s, restarting", c->partPath.c_str());
      resuming = false;
      c->crc = 0;
    }
    if (!resuming) {
      discardPartial(*c);
      if (!Storage.openFileForWrite("HTTP", c->partPath.c_str(), c->file)) {
        LOG_ERR("HTTP", "Failed to open file for writing");
        return FILE_ERROR;
      }
      c->metaPending = true;
    } else {
      c->metaPending = false;
      LOG_DBG("HTTP", "Resuming at %lu", static_cast<unsigned long>(plan.offset));
    }
    transfer = Transfer{};
    transfer.startOffset = resuming ? static_cast<size_t>(plan.offset) : 0;
    transfer.ifRange = resuming ? c->meta.validator : "";

    size_t downloaded = 0;
    result = runGetSecure(url, username, password, headers, sink, cancelFlag, &downloaded,
                          options.downgradeRedirectsToHttp, &transfer);
    // Close (and sync) before any rename()/remove() on the path.
    closeFile(c->file);
    size = downloaded;

    if (resuming && transfer.status == 416) {
      // Range not satisfiable. If the sidecar says the part is the whole entity
      // (a rename failed last time) finish it; otherwise start over.
      if (http_resume::partIsComplete(plan.offset, c->meta.total, options.expectedSize)) {
        alreadyComplete = true;
        result = OK;
        size = static_cast<size_t>(plan.offset);
        break;
      }
      LOG_INF("HTTP", "Server rejected resume, restarting from zero");
      discardPartial(*c);
      local.partSize = 0;
      local.metaValid = false;
      continue;
    }
    break;
  }

  if (c->fileError || result == FILE_ERROR) {
    discardPartial(*c);
    return FILE_ERROR;
  }
  if (result != OK) {
    if (size == 0 || !keepPartialAfter(result, transfer.status)) discardPartial(*c);
    return result;
  }

  switch (http_resume::verifyComplete(size, alreadyComplete ? c->meta.total : transfer.total, options.expectedSize,
                                      options.verifyCrc32, c->crc, options.expectedCrc32)) {
    case http_resume::Verdict::Ok:
      break;
    case http_resume::Verdict::Incomplete:
      LOG_ERR("HTTP", "Incomplete: %zu bytes", size);
      return HTTP_ERROR;
    case http_resume::Verdict::Empty:
      LOG_ERR("HTTP", "no data received");
      discardPartial(*c);
      return HTTP_ERROR;
    case http_resume::Verdict::SizeMismatch:
      LOG_ERR("HTTP", "Size mismatch: %zu bytes, expected %zu", size, options.expectedSize);
      discardPartial(*c);
      return SIZE_MISMATCH;
    case http_resume::Verdict::CrcMismatch:
      LOG_ERR("HTTP", "CRC mismatch: got %08lx, expected %08lx", static_cast<unsigned long>(c->crc),
              static_cast<unsigned long>(options.expectedCrc32));
      discardPartial(*c);
      return CRC_MISMATCH;
  }

  // A failed move keeps the complete ".part" and sidecar: the next call
  // finishes with a 416 instead of downloading again.
  if (!Storage.replaceFile(c->partPath.c_str(), destPath.c_str())) {
    LOG_ERR("HTTP", "Failed to move download into place: %s", destPath.c_str());
    return FILE_ERROR;
  }
  Storage.remove(c->metaPath.c_str());
  if (alreadyComplete && progress) progress(size, size);
  LOG_DBG("HTTP", "Downloaded %zu bytes", size);
  return OK;
}
