#pragma once
// Engine-side launcher.ini writer for the in-game graphics menu (docs/GRAPHICS_MENU.md). The launcher's GKeyFile
// (installer/launcher_settings.cpp) is merged, not regenerated: only the keys the game owns change, every other line stays
// byte for byte. No GLib, so kabus22-launcher compiles this too and round-trips it in --self-check.
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace k22::launcher_config {
struct Edit { std::string group, key; std::optional<std::string> value; }; // nullopt removes the key
// GKeyFile line rules (g_key_file_parse_line): leading whitespace ignored, '#' comments, "[group]", "key = value".
// A key's first occurrence in its group is rewritten in place, later duplicates dropped; missing keys follow the group's last
// key line; a missing group is appended.
std::string merge(std::string_view text, const std::vector<Edit>& edits);
// What the game owns, in launcher encoding. options: {key, value, fallback} for every --graphics key the menu shows.
struct Graphics {
    std::string preset, renderer;                         // [Launcher] preset=original|enhanced|ultra|custom, renderer=opengl|vulkan-raster|vulkan-rt
    std::optional<std::string> window, resolution;        // [Launcher] window=, resolution=WxH (only with native display; resolution not in borderless)
    std::vector<std::array<std::string,3>> options;
};
// Launcher composition rules (launcher_settings.cpp readControls/command): a [Graphics] key is written only when it differs
// from its fallback; outside the custom preset only fps_limit and seamless_doors are the game's (the rest stay the user's
// custom set). backend and texture_packs have dedicated launcher controls and are never [Graphics] keys.
std::vector<Edit> edits(const Graphics&);
std::filesystem::path path();      // ${XDG_CONFIG_HOME:-~/.config}/kabus22/launcher.ini (g_get_user_config_dir)
std::filesystem::path lock_root(); // ${XDG_DATA_HOME:-~/.local/share}/kabus22: the launcher's privateLock folder
// Under the launcher's flock(.launcher.lock): re-read `file`, merge, publish atomically (0600 temp, fsync, rename, fsync dir).
void write(const Graphics&, const std::filesystem::path& file = path(), const std::filesystem::path& lockRoot = lock_root());
}
