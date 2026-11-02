#pragma once
// Launcher UI text: English is the key; Turkish comes from a plain table (launcher_text.cpp).
// check_launcher_text.py fails when a literal passed to tx/txn/fmt has no Turkish entry.
#include <initializer_list>
#include <span>
#include <string>

namespace k22::launcher {
struct Text { const char* en; const char* tr; };
std::span<const Text> textTable();
void useTurkish(bool on);
bool turkish();
// Turkish text when the UI is Turkish and the table knows it, else the English text unchanged.
const char* tx(const char* english);
std::string tx(const std::string& english);
// Marks a literal for the table without translating it (arrays translated where shown).
constexpr const char* txn(const char* english) { return english; }
// tx, then {0}..{9} replaced by args (Turkish may reorder them).
std::string fmt(const char* english, std::initializer_list<std::string> args);
// Saved interface-language ("auto", "en", "tr") -> "en" or "tr": auto follows KABUS22_UI_LANG, then LC_ALL/LC_MESSAGES/LANG.
std::string uiLanguage(const std::string& preference);
}
