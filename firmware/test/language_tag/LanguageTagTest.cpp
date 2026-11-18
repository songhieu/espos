// The web's language tags ("de", "pt-BR") and the reader's language codes ("DE", "PT") map both ways.
#include <gtest/gtest.h>

#include "I18n.h"

namespace {

std::string tagOf(Language lang) {
  char tag[16];
  I18n::languageTag(lang, tag, sizeof(tag));
  return tag;
}

}  // namespace

TEST(LanguageTag, PlainCodesAreLowerCase) {
  EXPECT_EQ(tagOf(I18n::languageFromCode("EN")), "en");
  EXPECT_EQ(tagOf(I18n::languageFromCode("VI")), "vi");
  EXPECT_EQ(tagOf(I18n::languageFromCode("DE")), "de");
}

TEST(LanguageTag, CodesThatAreNotIsoUseTheIsoTag) {
  EXPECT_EQ(tagOf(I18n::languageFromCode("PT")), "pt-BR");
  EXPECT_EQ(tagOf(I18n::languageFromCode("P2")), "pt-PT");
  EXPECT_EQ(tagOf(I18n::languageFromCode("CAV")), "ca-valencia");
  EXPECT_EQ(tagOf(I18n::languageFromCode("SI")), "sl");
}

TEST(LanguageTag, EveryLanguageRoundTrips) {
  for (uint8_t i = 0; i < static_cast<uint8_t>(Language::_COUNT); i++) {
    const auto lang = static_cast<Language>(i);
    Language back = Language::_COUNT;
    ASSERT_TRUE(I18n::languageFromTag(tagOf(lang).c_str(), back)) << tagOf(lang);
    EXPECT_EQ(back, lang) << tagOf(lang);
  }
}

TEST(LanguageTag, TagsAndCodesMatchWithoutCase) {
  Language lang = Language::_COUNT;
  ASSERT_TRUE(I18n::languageFromTag("PT-br", lang));
  EXPECT_EQ(lang, I18n::languageFromCode("PT"));
  ASSERT_TRUE(I18n::languageFromTag("vi", lang));
  EXPECT_EQ(lang, I18n::languageFromCode("VI"));
  ASSERT_TRUE(I18n::languageFromTag("DE", lang));
  EXPECT_EQ(lang, I18n::languageFromCode("DE"));
}

TEST(LanguageTag, UnknownTagsAreRefusedAndLeaveTheOutputAlone) {
  Language lang = Language::VI;
  EXPECT_FALSE(I18n::languageFromTag("xx", lang));
  EXPECT_FALSE(I18n::languageFromTag("", lang));
  EXPECT_FALSE(I18n::languageFromTag(nullptr, lang));
  EXPECT_FALSE(I18n::languageFromTag("zh-Hans", lang));
  EXPECT_EQ(lang, Language::VI);
}

TEST(LanguageTag, ShortBufferIsCutAndTerminated) {
  char tag[4];
  I18n::languageTag(I18n::languageFromCode("CAV"), tag, sizeof(tag));
  EXPECT_STREQ(tag, "ca-");
}
