#pragma once
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace k22::saves {
namespace fs=std::filesystem;
// ${XDG_DATA_HOME:-~/.local/share}/kabus22; original scripts write kayitN.sav/kysd.sav/ftz.k22 relative to it
// (patched K22Widescreen scripts use _data/saves/ for kayit/kysd).
fs::path root();
// temp + fsync + rename + directory fsync; never leaves a partial target.
void write_atomic(const fs::path&,std::span<const std::uint8_t>);
int slot_number(std::string_view filename); // kayit<0-98>.sav (YukleMenu.wdl SLG_MaxKayit 99) or -1
bool rotated(std::string_view filename);    // slot saves and the kysd.sav index keep backups
// Copies the existing ROOT/relative into ROOT/backups/relative.1, shifting .1->.2->.3; the oldest beyond keep drops.
void backup(const fs::path& root,const fs::path& relative,int keep=3);
constexpr int MetaFormat=1;
// Sidecar kayitN.sav.meta; the 2030-byte original payload is never extended. Format 0 = no sidecar (original save).
struct Meta {int format=MetaFormat;std::string engine,written,entry,script,origin;};
std::optional<Meta> parse_meta(std::string_view);
std::string format_meta(const Meta&);
std::string now_utc();
std::string fnv1a64(std::span<const std::uint8_t>);
void write_meta(const fs::path& save,const Meta&);
// Version-keyed migration hook run before a slot is read; false if the sidecar is newer/malformed (left untouched).
bool migrate_meta(const fs::path& save,const Meta& current,std::ostream& warnings);
// "" when the original-format file is acceptable, otherwise the refusal reason.
std::string validate(std::string_view filename,std::span<const std::uint8_t>);
struct ImportResult {std::vector<std::string> imported,backedUp;fs::path source;};
// Imports a validated original Windows save set (kayit*.sav + kysd.sav [+ ftz.k22]) as a whole or not at all.
ImportResult import_windows(const fs::path& windowsGame,const fs::path& root,const fs::path& slotDirectory,const Meta& meta);
}
