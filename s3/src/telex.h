#pragma once
// Vietnamese Telex input engine for the on-screen keyboard.
// Portable C++17, standard library only (no Arduino/ESP-IDF, no exceptions, no RTTI).

#include <string>
#include <vector>

namespace telex {

// The word being composed: the characters right before the cursor, back to the last separator.
// Returns the new word after typing `key`, which is ASCII a-z or A-Z; the caller only calls
// this for letters. If the key makes no Vietnamese change, the result is word + key.
std::u32string type(const std::u32string& word, char32_t key);

// ASCII letters and the Vietnamese letters (à…ỹ, đ, ă, â, ê, ô, ơ, ư, both cases).
bool isLetter(char32_t c);

std::u32string fromUtf8(const std::string& s);
std::string toUtf8(const std::u32string& s);

// Long-press accent picker: variants of a base vowel, for example
// a -> {á,à,ả,ã,ạ,â,ă,ấ,ầ,ậ,ắ,ặ}. At most 12, most useful first. d -> {đ}.
// Keeps the base's case. Empty for letters without variants.
std::vector<char32_t> variants(char32_t base);

}  // namespace telex
