#include "I18n.h"

#include <strings.h>

#include <cctype>
#include <cstddef>
#include <cstring>

#include "I18nStrings.h"

using namespace i18n_strings;

namespace {
// Firmware codes whose web tag is not just the code in lower case.
struct TagAlias {
  const char* code;
  const char* tag;
};
constexpr TagAlias TAG_ALIASES[] = {{"PT", "pt-BR"}, {"P2", "pt-PT"}, {"CAV", "ca-valencia"}, {"SI", "sl"}};
}  // namespace

I18n& I18n::getInstance() {
  static I18n instance;
  return instance;
}

const char* I18n::get(StrId id) const {
  const auto index = static_cast<size_t>(id);
  if (index >= static_cast<size_t>(StrId::_COUNT)) {
    return "???";
  }

  // Use generated helper function - no hardcoded switch needed!
  const LangStrings lang = getLanguageStrings(_language);

  // If bit 15 of the offset is set, apply the offset to the English lookup table
  const uint16_t off = lang.offsets[index];
  if (off & 0x8000) return STRINGS_EN_DATA + (off & 0x7FFF);
  return lang.data + off;
}

void I18n::setLanguage(Language lang) {
  if (lang >= Language::_COUNT) {
    return;
  }
  _language = lang;
}

const char* I18n::getLanguageName(Language lang) const {
  const auto index = static_cast<size_t>(lang);
  if (index >= static_cast<size_t>(Language::_COUNT)) {
    return "???";
  }
  return LANGUAGE_NAMES[index];
}

Language I18n::languageFromCode(const char* code) {
  for (uint8_t i = 0; i < getLanguageCount(); i++) {
    if (strcmp(code, LANGUAGE_CODES[i]) == 0) return static_cast<Language>(i);
  }
  return Language::EN;
}

void I18n::languageTag(Language lang, char* out, size_t outSize) {
  if (outSize == 0) return;
  const auto index = static_cast<size_t>(lang) < static_cast<size_t>(Language::_COUNT) ? static_cast<size_t>(lang) : 0;
  const char* code = LANGUAGE_CODES[index];
  for (const auto& a : TAG_ALIASES) {
    if (strcmp(code, a.code) == 0) {
      strncpy(out, a.tag, outSize - 1);
      out[outSize - 1] = '\0';
      return;
    }
  }
  size_t i = 0;
  for (; code[i] && i + 1 < outSize; i++) out[i] = static_cast<char>(tolower(static_cast<unsigned char>(code[i])));
  out[i] = '\0';
}

bool I18n::languageFromTag(const char* tag, Language& out) {
  if (!tag || !*tag) return false;
  for (const auto& a : TAG_ALIASES) {
    if (strcasecmp(tag, a.tag) == 0) {
      tag = a.code;
      break;
    }
  }
  for (uint8_t i = 0; i < getLanguageCount(); i++) {
    if (strcasecmp(tag, LANGUAGE_CODES[i]) == 0) {
      out = static_cast<Language>(i);
      return true;
    }
  }
  return false;
}

// Generate character set for a specific language
const char* I18n::getCharacterSet(Language lang) {
  const auto langIndex = static_cast<size_t>(lang);
  if (langIndex >= static_cast<size_t>(Language::_COUNT)) {
    lang = Language::EN;  // Fallback to first language
  }

  return CHARACTER_SETS[static_cast<size_t>(lang)];
}
