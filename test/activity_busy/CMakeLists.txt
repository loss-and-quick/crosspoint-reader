add_executable(ActivityBusyTest
  ActivityBusyTest.cpp
)

target_include_directories(ActivityBusyTest PRIVATE
  ${REPO_ROOT}/src/activities
)

target_link_libraries(ActivityBusyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ActivityBusyTest)
