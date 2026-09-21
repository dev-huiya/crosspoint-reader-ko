add_executable(ReadingStatsTest
  ReadingStatsTest.cpp
  ${REPO_ROOT}/src/util/ReadingStats.cpp
  ${REPO_ROOT}/src/util/ReadingTimer.cpp
)
target_include_directories(ReadingStatsTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src/util
)
target_link_libraries(ReadingStatsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReadingStatsTest)
