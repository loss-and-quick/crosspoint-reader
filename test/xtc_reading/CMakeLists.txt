add_executable(XtcReadingTest
  XtcReadingTest.cpp
)

target_include_directories(XtcReadingTest PRIVATE
  ${REPO_ROOT}/lib/Xtc/Xtc
)

target_link_libraries(XtcReadingTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(XtcReadingTest)
