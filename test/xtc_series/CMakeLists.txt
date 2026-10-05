add_executable(XtcSeriesFormatTest
  XtcSeriesFormatTest.cpp
  ${REPO_ROOT}/lib/Xtc/Xtc/XtcSeriesFormat.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(XtcSeriesFormatTest PRIVATE
  ${REPO_ROOT}/test/fs_helpers/stubs
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Xtc/Xtc
)

target_link_libraries(XtcSeriesFormatTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(XtcSeriesFormatTest)
