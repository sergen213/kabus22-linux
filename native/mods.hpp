#pragma once
// Game-file mods (docs/MODDING.md): ${XDG_DATA_HOME:-~/.local/share}/kabus22/mods/<id>/{mod.txt,data/**}.
#include <filesystem>
#include <string>

namespace k22 {
struct ModInfo{std::string id,name,version,author,description,detail;std::filesystem::path path;bool valid{};};
std::filesystem::path mods_root();
bool mod_id(std::string_view id); // same rule as texture pack directory names
ModInfo load_mod(const std::filesystem::path& directory); // validates mod.txt and data/; Assets::add_mod reads data/
}
