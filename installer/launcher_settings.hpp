#pragma once
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <span>
#include <utility>
#include <vector>

namespace k22::launcher {
namespace fs = std::filesystem;
using Pairs = std::vector<std::pair<std::string, std::string>>;
struct Preferences {
    std::string mode = "campaign", renderer = "opengl", display = "native";
    std::string window = "borderless", resolution, language = "en", audioLanguage = "en";
    std::string bonusLanguage = "en";
    std::string camera = "original"; // original | first-person (engine --camera; original campaign scripts only)
    int bonusAA = 0;
    std::string preset = "original"; // Contract D presets; graphics overrides apply to "custom" only.
    Pairs graphics;
    std::vector<std::string> packs;  // enabled texture packs, priority order
    std::vector<std::string> mods;   // enabled mods, priority order (first wins)
};
// Contract D: `kabus22 --list-capabilities`.
struct Option { std::string key, label, fallback; std::vector<std::string> values, backends; };
struct Pack { std::string name, path, detail; bool valid = false; std::string display; };
struct Capabilities {
    std::string version;
    std::vector<std::pair<std::string, std::pair<bool, std::string>>> backends; // name -> available, detail
    std::vector<std::string> presets;
    std::vector<Option> options;
    std::vector<Pack> packs;
    bool available(std::string_view backend) const;
};
// ${XDG_DATA_HOME:-~/.local/share}/kabus22/mods/<id>/{mod.txt,data/**}; files are normalized data/ names (ASCII lowercase).
struct Mod { std::string id, name, version, author, description, detail; bool valid = false; std::vector<std::string> files; };
fs::path modsRoot();
Mod readMod(const fs::path& directory);
std::vector<Mod> listMods(const fs::path& root);
void createMod(const fs::path& root, const std::string& id, const std::string& name);
// Extracts a .zip (mod.txt at its root or inside exactly one top folder) and atomically renames it into root; returns the id.
std::string installMod(const fs::path& root, const fs::path& zip);
// Model viewer (docs/MODDING.md "Model viewer"). A model is a .mdl; a mod's .gltf/.glb replaces the .mdl of the same name.
std::string modelKey(std::string_view name); // normalized "dir/stem.mdl" for a .mdl/.gltf/.glb name, else empty
// ${XDG_CACHE_HOME:-~/.cache}/kabus22/viewer/<mod id or _original>[/@bonus]/<key>/
fs::path viewerCache(const std::string& mod, const std::string& key, bool bonus);
struct GltfSummary { std::size_t meshes = 0, triangles = 0; std::vector<std::string> uris; }; // uris: external buffers/images, percent-decoded
GltfSummary readGltf(const fs::path& file);
// Copies (source first) of a .gltf/.glb/.mdl and the files its relative URIs name; throws listing absolute or escaping URIs.
struct ModelPlan { std::vector<std::pair<fs::path, fs::path>> copies; std::vector<fs::path> overwritten, removed; std::vector<std::string> warnings; };
ModelPlan planReplacement(const fs::path& mod, const std::string& key, const fs::path& source);
// The mod's files for key plus the side files of its glTFs that no other glTF in the mod names.
std::vector<fs::path> replacementFiles(const fs::path& mod, const std::string& key);
struct Controller {
    std::string mode = "original";
    double deadzone = 0.15, sensitivity = 1.0;
    bool vibration = true;
    std::vector<std::pair<std::string, int>> buttons; // every controllerButtons() entry, 0 = unmapped
    bool operator==(const Controller&) const = default;
};
// Contract B: updates.ini.
struct Updates { std::string repository = "sergen213/kabus22-linux", channel = "stable"; bool operator==(const Updates&) const = default; };
struct Verify { bool ok = false; long long checked = 0; std::vector<std::string> missing, modified; };
struct UpdateCheck { std::string current, latest, channel, notes, published, repository; bool available = false; };
struct UpdateStatus { std::string active, previous; std::vector<std::string> installed; };
struct Snapshot {
    fs::path path;
    std::optional<std::string> bytes;
    unsigned long long device = 0, inode = 0;
    long long seconds = 0, nanos = 0;
};
struct Settings {
    fs::path root, engine, privateRoot, configFile;
    bool originalCampaign = false, bonusAvailable = false;
    std::vector<unsigned char> campaign, defaults;
    std::array<int, 5> bonus{}, bonusDefaults{};
    Preferences preferences;
    std::string initialLanguage;
    std::vector<std::string> languages{"tr","en","de"}; // campaign text languages the installed data supports
    Snapshot campaignPrivate, campaignSource, bonusPrivate, bonusSource, localePrivate, localeSource, config, controllerFile, updatesFile;
    Controller controller;
    Updates updates;
    std::vector<std::string> warnings;
    std::optional<Capabilities> capabilities; // absent: engine without presets, so no graphics arguments are emitted
};
struct Key { int code; const char* name; };
std::span<const Key> keys();
std::span<const char* const> controllerButtons();
void validateCampaign(const std::vector<unsigned char>& bytes);
std::array<int, 5> decodeBonus(const std::string& bytes);
void validatePreferences(const Preferences& preferences);
// tr/en/de always; es/ru/pl (patch DD_Dil 3-5) when K22Widescreen.wrs and _data/language/<folder>/subtitle/general.txt exist.
std::vector<std::string> campaignLanguages(const fs::path& game);
// ygm.k22, Bonus launcher rms0/1/2.txt: first line 111 -> tr, 222 -> de, anything else -> en; written as 000/111/222.
std::string bonusMarkerLanguage(std::string_view marker);
std::string bonusMarker(const std::string& language);
Controller defaultController();
void validateController(const Controller& controller);
Controller parseController(const std::string& bytes, std::vector<std::string>& warnings);
std::string encodeController(const Controller& controller);
void validateUpdates(const Updates& updates);
Updates parseUpdates(const std::string& bytes, std::vector<std::string>& warnings);
std::string encodeUpdates(const Updates& updates);
Capabilities parseCapabilities(const std::string& json);
Verify parseVerify(const std::string& json);
UpdateCheck parseCheck(const std::string& json);
UpdateStatus parseStatus(const std::string& json);
std::optional<std::pair<int, std::string>> progressLine(std::string_view line);
fs::path defaultRoot();
fs::path binDirectory(const fs::path& root);
Settings load(const fs::path& root, const fs::path& engine = {});
void save(Settings& settings);
void saveUpdates(Settings& settings);
void binary(const fs::path& path);
std::vector<std::string> command(const Settings& settings, const std::optional<unsigned long long>& frames = {});
fs::path logDirectory();
// Creates a private, exclusive kind-<timestamp>.log, keeping at most `keep` logs of that kind.
std::pair<fs::path, int> openLog(const fs::path& directory, std::string_view kind, std::size_t keep = 10);
std::vector<fs::path> listLogs(const fs::path& directory);
void selfCheck();
}
