add_executable(SiblingCoverNameTest SiblingCoverNameTest.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
target_include_directories(SiblingCoverNameTest PRIVATE ${REPO_ROOT}/lib/SiblingCover ${REPO_ROOT}/lib/Utf8)
target_link_libraries(SiblingCoverNameTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SiblingCoverNameTest)
