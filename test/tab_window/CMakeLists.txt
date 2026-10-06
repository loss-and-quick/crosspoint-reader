add_executable(TabWindowTest TabWindowTest.cpp)
target_include_directories(TabWindowTest PRIVATE ../../src)
target_link_libraries(TabWindowTest PRIVATE GTest::gtest_main)
gtest_discover_tests(TabWindowTest)
