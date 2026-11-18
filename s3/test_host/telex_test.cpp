// Host tests for the Telex engine. Build and run:
//   c++ -std=c++17 -Wall -Wextra -O1 -I src src/telex.cpp test_host/telex_test.cpp \
//       -o /tmp/telex_test && /tmp/telex_test

#include "telex.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_run = 0;
int g_fail = 0;

void check(bool ok, const std::string& what, const std::string& got, const std::string& want) {
  ++g_run;
  if (!ok) {
    ++g_fail;
    std::printf("FAIL %s: got \"%s\", want \"%s\"\n", what.c_str(), got.c_str(), want.c_str());
  }
}

// Types each key in turn, starting from an empty word.
std::string typeKeys(const std::string& keys) {
  std::u32string w;
  for (char c : keys) w = telex::type(w, char32_t(static_cast<unsigned char>(c)));
  return telex::toUtf8(w);
}

void expectTyped(const std::string& keys, const std::string& want) {
  const std::string got = typeKeys(keys);
  check(got == want, "type " + keys, got, want);
}

// One more key on an existing word.
void expectNext(const std::string& word, char key, const std::string& want) {
  const std::string got = telex::toUtf8(telex::type(telex::fromUtf8(word), char32_t(key)));
  check(got == want, "\"" + word + "\" + " + std::string(1, key), got, want);
}

void expectVariants(char32_t base, const std::string& want) {
  const std::vector<char32_t> v = telex::variants(base);
  const std::string got = telex::toUtf8(std::u32string(v.begin(), v.end()));
  check(got == want, "variants " + telex::toUtf8(std::u32string(1, base)), got, want);
}

struct Case {
  const char* keys;
  const char* want;
};

const Case kCases[] = {
    // Required table.
    {"vieetj", "việt"}, {"hoafn", "hoàn"}, {"gif", "gì"},
    {"tieengs", "tiếng"}, {"thuyr", "thủy"}, {"giaf", "già"},
    {"dduowcj", "được"}, {"quas", "quá"}, {"nguwowif", "người"},
    {"hoaf", "hòa"}, {"tw", "tư"}, {"ass", "as"},
    {"aaa", "aa"}, {"dd", "đ"}, {"Vieetj", "Việt"},
    {"VIEETJ", "VIỆT"}, {"khuyeenr", "khuyển"}, {"tuyeetj", "tuyệt"},
    {"huowng", "hương"}, {"cuar", "của"}, {"muaf", "mùa"},
    {"chuyeenj", "chuyện"}, {"class", "class"}, {"website", "website"},
    {"ddi", "đi"}, {"toans", "toán"}, {"nhaatj", "nhật"},
    {"quyeetx", "quyễt"},  // ê takes the tone, whichever tone key
    {"quyeets", "quyết"},

    // Examples from the spec.
    {"tana", "tân"}, {"uow", "ươ"}, {"uwow", "ươ"}, {"didd", "đi"},
    {"ddd", "dd"}, {"aww", "aw"}, {"aw", "ă"}, {"ow", "ơ"}, {"uw", "ư"},
    {"aa", "â"}, {"ee", "ê"}, {"oo", "ô"},

    // The tone moves when the syllable changes.
    {"hosa", "hóa"}, {"hoasn", "hoán"}, {"thusy", "thúy"}, {"gisa", "giá"},
    {"qusa", "quá"}, {"ngoafi", "ngoài"}, {"tieesng", "tiếng"}, {"vieejt", "việt"},
    {"muafw", "mừa"}, {"hoafng", "hoàng"},

    // Tone placement.
    {"hoef", "hòe"}, {"khuyur", "khuỷu"}, {"quys", "quý"}, {"yeeu", "yêu"},
    {"ddaauf", "đầu"}, {"rowif", "rời"}, {"tooif", "tồi"}, {"muoons", "muốn"},
    {"quaanf", "quần"}, {"tuaafn", "tuần"}, {"xoawn", "xoăn"}, {"giuwx", "giữ"},
    {"giwx", "giữ"}, {"dduwowngf", "đường"}, {"ruwowuj", "rượu"},
    {"lafm", "làm"}, {"cacs", "các"}, {"gieengs", "giếng"}, {"gins", "gín"},
    {"khoer", "khỏe"}, {"uyr", "ủy"}, {"oais", "oái"}, {"thuees", "thuế"},

    // Modifier typed after the rest of the word, and switching modifiers.
    {"nguoiwf", "người"}, {"tuongw", "tương"}, {"muaw", "mưa"}, {"muwa", "mưa"},
    {"aaw", "ă"}, {"awa", "â"}, {"owo", "ô"}, {"uowo", "uô"}, {"tanaw", "tăn"},

    // Case.
    {"DDUWOWCJ", "ĐƯỢC"}, {"Ddi", "Đi"}, {"DDi", "Đi"}, {"NGUWOWIF", "NGƯỜI"},
    {"HOafn", "HOàn"}, {"TIEeNGS", "TIẾNG"}, {"TIEENGS", "TIẾNG"}, {"Tw", "Tư"}, {"TW", "TƯ"},

    // Undo, z.
    {"eee", "ee"}, {"ooo", "oo"}, {"xooong", "xoong"}, {"asss", "ass"},
    {"asf", "à"}, {"asz", "a"}, {"hoafz", "hoa"}, {"z", "z"}, {"az", "az"},
    {"dddd", "đ"}, {"ww", "w"}, {"tww", "tw"},

    // Not Vietnamese: typed as is.
    {"Website", "Website"}, {"tweet", "tweet"}, {"hello", "hello"}, {"jazz", "jazz"},
    {"fax", "fax"}, {"string", "string"}, {"s", "s"}, {"x", "x"},
    {"chrs", "chrs"}, {"bcs", "bcs"}, {"aaas", "aas"}, {"aaaa", "aaa"}, {"deadd", "deadd"},
};

void testTyping() {
  for (const Case& c : kCases) expectTyped(c.keys, c.want);
}

// Every vowel form with every tone, both cases: checks the composition table.
void testVowelTable() {
  const char* keys[12] = {"a", "aw", "aa", "e", "ee", "i", "o", "oo", "ow", "u", "uw", "y"};
  const char* lower[12] = {"aáàảãạ", "ăắằẳẵặ", "âấầẩẫậ", "eéèẻẽẹ", "êếềểễệ", "iíìỉĩị",
                           "oóòỏõọ", "ôốồổỗộ", "ơớờởỡợ", "uúùủũụ", "ưứừửữự", "yýỳỷỹỵ"};
  const char* upper[12] = {"AÁÀẢÃẠ", "ĂẮẰẲẴẶ", "ÂẤẦẨẪẬ", "EÉÈẺẼẸ", "ÊẾỀỂỄỆ", "IÍÌỈĨỊ",
                           "OÓÒỎÕỌ", "ÔỐỒỔỖỘ", "ƠỚỜỞỠỢ", "UÚÙỦŨỤ", "ƯỨỪỬỮỰ", "YÝỲỶỸỴ"};
  const char* toneKeys[6] = {"", "s", "f", "r", "x", "j"};
  for (int f = 0; f < 12; ++f) {
    const std::u32string lo = telex::fromUtf8(lower[f]);
    const std::u32string up = telex::fromUtf8(upper[f]);
    for (int t = 0; t < 6; ++t) {
      std::string k = std::string(keys[f]) + toneKeys[t];
      expectTyped(k, telex::toUtf8(lo.substr(size_t(t), 1)));
      for (char& ch : k) ch = char(ch - 'a' + 'A');
      expectTyped(k, telex::toUtf8(up.substr(size_t(t), 1)));
      // Every composed letter is a letter, and decomposes back to itself.
      check(telex::isLetter(lo[size_t(t)]) && telex::isLetter(up[size_t(t)]),
            "isLetter " + telex::toUtf8(lo.substr(size_t(t), 1)), "false", "true");
    }
  }
}

void testExistingWords() {
  expectNext("hòa", 'n', "hoàn");
  expectNext("Việ", 't', "Việt");
  expectNext("tiê", 'S', "tiế");
  expectNext("dì", 'd', "dìd");
  expectNext("dìd", 'd', "đì");
  expectNext("x1", 's', "x1s");      // not a syllable: appended
  expectNext("a-b", 'a', "a-ba");
  expectNext("người", 'f', "ngươif");  // same tone again: undo
  expectNext("người", 's', "ngưới");
  expectNext("người", 'z', "ngươi");
  expectNext("hoàk", 'z', "hoak");   // z clears a tone even off a syllable
  expectNext("hoàk", 's', "hoàks");
  expectNext("abcdefghijklmnopq", 's', "abcdefghijklmnopqs");
}

void testVariants() {
  expectVariants('a', "áàảãạâăấầậắặ");
  expectVariants('U', "ÚÙỦŨỤƯỨỪỬỮỰ");
  expectVariants('o', "óòỏõọôơốồộớợ");
  expectVariants('E', "ÉÈẺẼẸÊẾỀỂỄỆ");
  expectVariants('i', "íìỉĩị");
  expectVariants('Y', "ÝỲỶỸỴ");
  expectVariants('d', "đ");
  expectVariants('D', "Đ");
  expectVariants('b', "");
  expectVariants('1', "");
  check(telex::variants('a').size() == 12 && telex::variants('O').size() == 12, "variants size",
        "", "12");
}

void testIsLetter() {
  const char32_t yes[] = {'a', 'Z', 0x111, 0x110, 0x1EEF, 0x1EEE, 0x103, 0x1AF};  // đ Đ ữ Ữ ă Ư
  const char32_t no[] = {'1', ' ', '-', '_', 0xE4, 0xE7, 0x300, 0x1F642};          // ä ç ◌̀ 🙂
  for (char32_t c : yes)
    check(telex::isLetter(c), "isLetter " + std::to_string(unsigned(c)), "false", "true");
  for (char32_t c : no)
    check(!telex::isLetter(c), "isLetter " + std::to_string(unsigned(c)), "true", "false");
}

void testUtf8() {
  const std::string text = "Tiếng Việt: người, được, hương. ĐƯỢC 🙂 ok €";
  const std::u32string u = telex::fromUtf8(text);
  check(telex::toUtf8(u) == text, "utf8 round trip", telex::toUtf8(u), text);
  check(u.size() == 43, "utf8 code point count", std::to_string(u.size()), "43");
  check(u.find(char32_t(0x1F642)) != std::u32string::npos, "utf8 4-byte", "missing", "found");

  const std::u32string all = U"aáẠđĐ€\U0001F642 ";
  check(telex::fromUtf8(telex::toUtf8(all)) == all, "utf32 round trip", telex::toUtf8(all),
        telex::toUtf8(all));

  const std::u32string bad1 = telex::fromUtf8("a\xC3");          // truncated
  const std::u32string bad2 = telex::fromUtf8("\xC0\xAF" "b");    // overlong
  const std::u32string bad3 = telex::fromUtf8("\xED\xA0\x80");    // surrogate
  check(bad1 == U"a�", "utf8 truncated", telex::toUtf8(bad1), "a\xEF\xBF\xBD");
  check(bad2 == U"��b", "utf8 overlong", telex::toUtf8(bad2), "??b");
  check(bad3.size() == 3 && bad3[0] == 0xFFFD, "utf8 surrogate", telex::toUtf8(bad3), "???");
}

}  // namespace

int main() {
  testTyping();
  testVowelTable();
  testExistingWords();
  testVariants();
  testIsLetter();
  testUtf8();
  std::printf("%d tests, %d passed, %d failed\n", g_run, g_run - g_fail, g_fail);
  return g_fail ? 1 : 0;
}
