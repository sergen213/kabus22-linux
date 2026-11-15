// Xbox UWP Dev Mode entry (docs/XBOX.md). SDL owns the CoreApplication view (SDL_WinRTRunApp); this file only builds the
// engine's command line, which a console app cannot receive: --data <package>/game, Direct3D 12, fullscreen, then one
// argument per line from LocalState/args.txt (optional, for testing). An args.txt whose first line is --standalone
// replaces those defaults with its remaining lines (e.g. --list-capabilities). stdout/stderr go to LocalState/kabus22.log.
#include "platform.hpp"
#include "render_policy.hpp"
#include <SDL.h>
#include <SDL_main.h>
#include <windows.h>
#include <winrt/Windows.System.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
std::filesystem::path from_utf8(const char* text) { return std::filesystem::path(reinterpret_cast<const char8_t*>(text)); }
std::string utf8(const std::filesystem::path& path) { const auto text = path.u8string(); return std::string(text.begin(), text.end()); }
// The in-game graphics menu saves into config/launcher.ini (native/launcher_config.cpp); on Linux the launcher turns that file
// into the command line (installer/launcher_settings.cpp command). The Xbox has no launcher, so the same rules apply here:
// [Launcher] preset -> --preset (not for original); [Graphics] key=value -> --graphics key=value, under the custom preset for
// every key and otherwise only fps_limit and seamless_doors. Renderer, window and resolution are fixed on the console. Keys
// the engine does not list (launcher_config::retiredGraphicsKeys) are skipped, as the launcher skips them.
std::vector<std::string> launcher_arguments(const std::filesystem::path& file) {
    std::ifstream in(file);
    std::string group, preset = "original";
    std::vector<std::pair<std::string, std::string>> graphics;
    auto trim = [](std::string s) { const auto b = s.find_first_not_of(" \t\r"); if (b == std::string::npos) return std::string{}; return s.substr(b, s.find_last_not_of(" \t\r") - b + 1); };
    for (std::string line; std::getline(in, line);) {
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        if (line.front() == '[') { group = line.back() == ']' ? line.substr(1, line.size() - 2) : std::string{}; continue; }
        const auto equal = line.find('=');
        if (equal == std::string::npos) continue;
        const auto key = trim(line.substr(0, equal)), value = trim(line.substr(equal + 1));
        if (group == "Launcher" && key == "preset") preset = value;
        else if (group == "Graphics" && !key.empty() && !value.empty() && std::any_of(k22::render_policy::graphicsOptions.begin(), k22::render_policy::graphicsOptions.end(), [&](const auto& o) { return o.key == key; })) graphics.emplace_back(key, value);
    }
    std::vector<std::string> args;
    if (preset != "original") args.insert(args.end(), {"--preset", preset});
    bool fpsLimit = false;
    for (const auto& [key, value] : graphics)
        if (preset == "custom" || key == "fps_limit" || key == "seamless_doors") { args.insert(args.end(), {"--graphics", key + "=" + value}); fpsLimit = fpsLimit || key == "fps_limit"; }
    // The console runs the TV at up to 120 Hz (request_refresh below); pace to it unless the graphics menu chose a limit.
    if (!fpsLimit) args.insert(args.end(), {"--graphics", "fps_limit=120"});
    return args;
}
int engine_main(int, char**) {
    const char* installed = SDL_WinRTGetFSPathUTF8(SDL_WINRT_PATH_INSTALLED_LOCATION);
    const char* local = SDL_WinRTGetFSPathUTF8(SDL_WINRT_PATH_LOCAL_FOLDER);
    if (!installed || !local) return 1;
    const std::filesystem::path localFolder = from_utf8(local);
    const std::filesystem::path log = localFolder / "kabus22.log";
    std::ofstream(log, std::ios::trunc).close();
    // Both streams append, so each write lands at the end instead of overwriting the other stream's output.
    _wfreopen(log.c_str(), L"a", stdout);
    _wfreopen(log.c_str(), L"a", stderr);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    // App mode allows about 1 GB, Game mode (Dev Home App type, or the console's DefaultUWPContentTypeToGame) about 5 GB.
    std::fprintf(stderr, "kabus22: UWP memory limit %llu MB\n", static_cast<unsigned long long>(winrt::Windows::System::MemoryManager::AppMemoryUsageLimit() >> 20));
    // Xbox Series X|S HDMI output tops out at 120 Hz (4K120 on Series X).
    k22::platform::request_refresh(120);
    std::vector<std::string> args{"kabus22", "--data", utf8(from_utf8(installed) / "game"), "--renderer", "d3d12", "--window-mode", "fullscreen"};
    for (auto& arg : launcher_arguments(localFolder / "config" / "launcher.ini")) args.push_back(std::move(arg));
    std::ifstream extra(localFolder / "args.txt");
    bool first = true;
    for (std::string line; std::getline(extra, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (first && line == "--standalone") args.resize(1);
        // K22_NAME=value sets the engine's diagnostic environment variable (K22_FRAME_TIMING, K22_STATE_DIGEST, ...), which
        // a UWP app cannot be given otherwise; a value starting with "local:" is a path inside LocalState.
        else if (line.starts_with("K22_") && line.find('=') != std::string::npos) {
            const auto equal = line.find('=');
            std::string value = line.substr(equal + 1);
            if (value.starts_with("local:")) {
                // Only a relative path that stays inside LocalState: no root/drive, no leading ".." after normalisation.
                const auto relative = from_utf8(value.c_str() + 6).lexically_normal();
                if (relative.empty() || relative.has_root_path() || *relative.begin() == "..") { std::fprintf(stderr, "kabus22: args.txt: ignoring %s (local: path leaves LocalState)\n", line.c_str()); first = false; continue; }
                value = utf8(localFolder / relative);
            }
            _putenv_s(line.substr(0, equal).c_str(), value.c_str());
        }
        else args.push_back(line);
        first = false;
    }
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    const int status = SDL_main(int(args.size()), argv.data());
    std::fprintf(stderr, "kabus22: exit status %d\n", status);
    return status;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    // The controller's B button is the game's "back"; without this the system treats it as app navigation.
    SDL_SetHint(SDL_HINT_WINRT_HANDLE_BACK_BUTTON, "1");
    return SDL_WinRTRunApp(engine_main, nullptr);
}
