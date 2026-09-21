add_executable(KoBuiltinFontsTest
  KoBuiltinFontsTest.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(KoBuiltinFontsTest PRIVATE
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
)
target_link_libraries(KoBuiltinFontsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(KoBuiltinFontsTest)
