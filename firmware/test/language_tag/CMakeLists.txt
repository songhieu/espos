add_executable(LanguageTagTest
  LanguageTagTest.cpp
  ${REPO_ROOT}/lib/I18n/I18n.cpp
  ${REPO_ROOT}/lib/I18n/I18nStrings.cpp
)

target_include_directories(LanguageTagTest PRIVATE
  ${REPO_ROOT}/lib/I18n
)

target_link_libraries(LanguageTagTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LanguageTagTest)
