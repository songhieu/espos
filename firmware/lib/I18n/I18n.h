#pragma once

#include <cstddef>
#include <cstdint>

#include "I18nKeys.h"
/**
 * Internationalization (i18n) system for CrossPoint Reader
 */

class I18n {
 public:
  static I18n& getInstance();

  // Disable copy
  I18n(const I18n&) = delete;
  I18n& operator=(const I18n&) = delete;

  // Get localized string by ID
  const char* get(StrId id) const;

  const char* operator[](StrId id) const { return get(id); }

  Language getLanguage() const { return _language; }
  void setLanguage(Language lang);
  const char* getLanguageName(Language lang) const;
  static Language languageFromCode(const char* code);
  // The language's web tag ("en", "de", "pt-BR", "sl", "ca-valencia"): the firmware code in lower case, or the ISO tag
  // where the firmware code differs from it. Writes at most outSize - 1 characters.
  static void languageTag(Language lang, char* out, size_t outSize);
  // The language for a web tag or a firmware code, compared without case; false when the reader has no such language.
  static bool languageFromTag(const char* tag, Language& out);

  // Get all unique characters used in a specific language
  // Returns a sorted string of unique characters
  static const char* getCharacterSet(Language lang);

 private:
  I18n() : _language(Language::EN) {}

  Language _language;
};

// Convenience macros
#define tr(id) I18n::getInstance().get(StrId::id)
#define I18N I18n::getInstance()
