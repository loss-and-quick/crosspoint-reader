add_executable(HttpResumeTest
  HttpResumeTest.cpp
  ${REPO_ROOT}/src/network/HttpResume.cpp
)

target_include_directories(HttpResumeTest PRIVATE
  ${REPO_ROOT}/src/network
)

target_link_libraries(HttpResumeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HttpResumeTest)
