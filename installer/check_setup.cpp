#include "setup.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <cstdlib>
#include <sys/stat.h>

using namespace k22::setup;
namespace {
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
void put(const fs::path& path, std::string_view data) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc) << data;
}
std::string get(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
template <class F> bool fails(F action) {
    try { action(); } catch (const std::exception&) { return true; }
    return false;
}
using Paths = std::vector<std::string>;

// Asset-free fixtures: source detection, versioned layout, verify and directory repair.
void fixtures(const fs::path& base) {
    std::string iso(32769 + 5, '\0');
    iso.replace(32769, 5, "CD001");
    put(base/"media/game.iso", iso);
    put(base/"media/notes.txt", "not media");
    put(base/"cd/SETUP.EXE", "MZ fake setup");
    put(base/"cd/setup-1.bin", "slice");
    put(base/"lonely/setup.exe", "MZ without slices");
    require(detectSource(base/"media/game.iso").kind == SourceKind::Iso, "ISO9660 magic not detected");
    require(detectSource(base/"cd/SETUP.EXE").kind == SourceKind::Setup, "Setup pair not detected");
    const auto mounted = detectSource(base/"cd");
    require(mounted.kind == SourceKind::Setup && mounted.path.filename() == "SETUP.EXE", "Media folder with one setup pair not detected");
    require(fails([&] { detectSource(base/"media/notes.txt"); }) && fails([&] { detectSource(base/"lonely/setup.exe"); }) &&
        fails([&] { detectSource(base/"lonely"); }) && fails([&] { detectSource(base/"missing"); }), "Unrecognised sources must fail");

    const auto win = base/"Kabus 22";
    const std::vector<std::pair<std::string, std::string>> game{
        {"K22.wrs", "WRS2 campaign"}, {"Kaynak.k22", "kaynak"}, {"turkce.k22", "language"},
        {"Help/Help_Files/fon_LOGO.jpg", "logo"}, {"Menu_sound/menu_fon.ogg", "ogg"}, {"K22Widescreen.wrs", "widescreen"}};
    for (const auto& [name, data] : game) put(win/name, data);
    put(win/"K22.exe", "MZ engine");
    for (const auto* name : {"kabus_dll.dll", "d3d9.dll", "kayit0.sav", "ftz.k22", "unins000.dat", "_data/saves/slot.k22"}) put(win/name, name);
    fs::create_symlink("/nonexistent", win/"ReShade_shaders");
    const auto detected = detectSource(win);
    require(detected.kind == SourceKind::Directory && detected.edition == "merscom+kabus22patch",
        "Windows folder/edition detection failed: " + detected.edition);
    const auto preview = previewArtwork(win, {}, base/"preview");
    require(get(preview) == "logo", "Directory artwork preview failed");

    // A staged flat installation becomes versions/<v> + current + compat links with both inventories.
    const auto staging = base/"stage", root = base/"install/kabus22";
    for (const auto& [name, data] : game) put(staging/"game"/name, data);
    put(staging/"bin/kabus22", "engine"); put(staging/"bin/kabus22-launcher", "launcher");
    put(staging/"lib/libdemo.so.1", "library"); put(staging/"share/licenses/kabus22/notice", "notice");
    put(staging/"kabus22.png", "icon");
    finalizeInstallation(staging, {root, win, kindName(detected.kind), detected.edition, {base/"apps/kabus22-native.desktop"}});
    fs::create_directories(root.parent_path());
    fs::rename(staging, root);
    const std::string version = K22_VERSION;
    require(fs::read_symlink(root/"current") == fs::path("versions")/version && fs::read_symlink(root/"bin") == "current/bin" &&
        fs::read_symlink(root/"lib") == "current/lib" && fs::read_symlink(root/"share") == "current/share" &&
        fs::is_regular_file(root/"versions"/version/"bin/kabus22") && fs::is_regular_file(root/"bin/kabus22-launcher") &&
        fs::is_regular_file(root/"versions"/version/".kabus22-payload.json"), "New install layout is not versioned");
    const auto inventory = get(root/".kabus22-inventory.json");
    require(inventory.find("\"path\":\"game/K22.wrs\"") != std::string::npos && inventory.find("\"path\":\"bin/") == std::string::npos &&
        inventory.find("\"edition\":\"merscom+kabus22patch\"") != std::string::npos, "Root inventory must list game files only");
    const auto clean = verify(root);
    require(clean.ok() && clean.checked == game.size() + 2 + 4, "Fresh installation must verify: " + std::to_string(clean.checked));

    put(root/"kayit0.sav", "my save");
    put(root/"options.k22", "my options");
    struct stat saveBefore{}, optionsBefore{};
    require(::stat((root/"kayit0.sav").c_str(), &saveBefore) == 0 && ::stat((root/"options.k22").c_str(), &optionsBefore) == 0, "stat");
    fs::remove_all(root/"game/Help");
    fs::remove(root/"game/Kaynak.k22");
    put(root/"game/K22.wrs", "tampered");
    put(root/"game/turkce.k22", "tampered");
    put(win/"turkce.k22", "another edition");
    put(root/"versions"/version/"bin/kabus22-launcher", "tampered payload");
    const auto broken = verify(root);
    const auto payloadPath = "versions/" + version + "/bin/kabus22-launcher";
    require(broken.missing == Paths{"game/Help/Help_Files/fon_LOGO.jpg", "game/Kaynak.k22"} &&
        broken.modified == Paths{"game/K22.wrs", "game/turkce.k22", payloadPath}, "Verify must report missing and modified files");

    const auto repaired = repair(root, win, {});
    require(get(root/"game/K22.wrs") == "WRS2 campaign" && get(root/"game/Kaynak.k22") == "kaynak" &&
        get(root/"game/Help/Help_Files/fon_LOGO.jpg") == "logo", "Repair did not restore game files");
    // Different source bytes and payload are never written; saves/options untouched.
    require(repaired.missing.empty() && repaired.modified == Paths{"game/turkce.k22", payloadPath} &&
        get(root/"game/turkce.k22") == "tampered" && get(root/"versions"/version/"bin/kabus22-launcher") == "tampered payload",
        "Repair must restore only inventory-identical game bytes");
    struct stat saveAfter{}, optionsAfter{};
    require(::stat((root/"kayit0.sav").c_str(), &saveAfter) == 0 && ::stat((root/"options.k22").c_str(), &optionsAfter) == 0 &&
        get(root/"kayit0.sav") == "my save" && get(root/"options.k22") == "my options" &&
        saveAfter.st_ino == saveBefore.st_ino && saveAfter.st_mtim.tv_nsec == saveBefore.st_mtim.tv_nsec &&
        optionsAfter.st_ino == optionsBefore.st_ino && optionsAfter.st_mtim.tv_nsec == optionsBefore.st_mtim.tv_nsec,
        "Repair touched saves or options");
    require(std::none_of(fs::directory_iterator(root), fs::directory_iterator(), [](const auto& entry) {
        return entry.path().filename().string().starts_with(".kabus22-setup-"); }), "Repair left its staging directory");
    put(win/"turkce.k22", "language");
    put(root/"versions"/version/"bin/kabus22-launcher", "launcher");
    require(repair(root, win, {}).ok() && verify(root).ok(), "Repair from the matching source must fully verify");
    fs::remove(root/".kabus22-owned");
    require(fails([&] { verify(root); }), "Verify must refuse a root without the ownership marker");
}

// Reinstall commit: empty and owned folders are replaced keeping user files, shortcuts are replaced,
// failures roll back, foreign folders and directory shortcuts are refused untouched.
void reinstall(const fs::path& base, const std::vector<std::pair<std::string, std::string>>& game) {
    const auto root = base/"re/kabus22", apps = base/"re/apps/kabus22-native.desktop", desk = base/"re/desktop/kabus22-native.desktop";
    const auto stage = [&](const fs::path& at, const std::string& extra) {
        for (const auto& [name, data] : game) put(at/"game"/name, data);
        put(at/"game"/extra, extra);
        put(at/"bin/kabus22", "engine"); put(at/"bin/kabus22-launcher", "launcher");
        put(at/"lib/libdemo.so.1", "library"); put(at/"share/licenses/kabus22/notice", "notice"); put(at/"kabus22.png", "icon");
        finalizeInstallation(at, {root, base, "iso", "merscom", {apps, desk}});
        return get(at/".kabus22-inventory.json");
    };
    const auto names = [](const fs::path& folder) {
        std::set<std::string> result;
        for (const auto& entry : fs::directory_iterator(folder)) result.insert(entry.path().filename().string());
        return result;
    };
    fs::create_directories(root);
    const auto first = stage(base/"s1", "first-only.k22");
    const auto owned = names(base/"s1");
    publish(base/"s1", root, {});
    require(verify(root).ok() && get(root/".kabus22-inventory.json") == first && !fs::exists(base/"s1"), "Empty folder must receive the installation");

    put(root/"options.k22", "my options"); put(root/"_data/saves/kayit0.sav", "my save"); put(root/".launcher.lock", "");
    put(apps, "old entry"); fs::create_directories(desk.parent_path()); fs::create_symlink("/nonexistent", desk);
    put(base/"sc/a", "new entry"); put(base/"sc/b", "new entry");
    const auto userKept = [&] {
        return get(root/"options.k22") == "my options" && get(root/"_data/saves/kayit0.sav") == "my save" && fs::exists(root/".launcher.lock");
    };
    const auto second = stage(base/"s2", "second-only.k22");
    require(fails([&] { publish(base/"s2", root, {{base/"sc/a", apps}, {base/"sc/missing", desk}}); }), "Missing staged shortcut must fail");
    require(get(root/".kabus22-inventory.json") == first && fs::exists(root/"game/first-only.k22") && verify(root).ok() && userKept() &&
        get(apps) == "old entry" && fs::is_symlink(desk) && get(base/"sc/a") == "new entry" && fs::exists(base/"s2/game/second-only.k22"),
        "Failed reinstall must restore the old installation, shortcuts and staging");

    publish(base/"s2", root, {{base/"sc/a", apps}, {base/"sc/b", desk}});
    auto expected = owned;
    expected.insert({"options.k22", "_data", ".launcher.lock"});
    require(verify(root).ok() && get(root/".kabus22-inventory.json") == second && !fs::exists(root/"game/first-only.k22") &&
        fs::exists(root/"game/second-only.k22") && names(root) == expected && userKept(),
        "Reinstall must leave exactly the new installation plus user files");
    require(get(apps) == "new entry" && !fs::is_symlink(desk) && get(desk) == "new entry" &&
        !fs::exists(base/"s2") && !fs::exists(base/"sc/a") && !fs::exists(base/"sc/b"), "Reinstall must replace shortcuts and remove staging");

    put(base/"foreign/notes.txt", "mine");
    stage(base/"s3", "third.k22");
    require(fails([&] { publish(base/"s3", base/"foreign", {}); }) && names(base/"foreign") == std::set<std::string>{"notes.txt"} &&
        get(base/"foreign/notes.txt") == "mine" && fs::exists(base/"s3/.kabus22-owned"), "Foreign non-empty folder must be refused untouched");
    put(base/"sc/c", "new entry"); fs::create_directories(base/"dirapps/kabus22-native.desktop");
    require(fails([&] { publish(base/"s3", base/"fresh", {{base/"sc/c", base/"dirapps/kabus22-native.desktop"}}); }) &&
        !fs::exists(base/"fresh") && fs::exists(base/"s3/.kabus22-owned") && get(base/"sc/c") == "new entry", "Directory shortcut must be refused before changes");
    publish(base/"s3", base/"fresh", {});
    require(verify(base/"fresh").ok() && !fs::exists(base/"s3"), "Missing root must receive the installation");
}
}

int main() {
    try {
        selfCheck();
        auto pattern = (fs::temp_directory_path()/"k22-check-setup-XXXXXX").string();
        if (!::mkdtemp(pattern.data())) throw std::runtime_error("Cannot create test directory");
        const fs::path base = pattern;
        struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{base};
        fixtures(base);
        reinstall(base, {{"K22.wrs", "WRS2 campaign"}, {"Help/Help_Files/fon_LOGO.jpg", "logo"}});
        std::cout << "Installer checks passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
