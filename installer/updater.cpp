#include "layout.hpp"
#include "update.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <unistd.h>

namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace k22;
constexpr const char* usage =
    "Usage: kabus22-updater [--install-dir ROOT] COMMAND [--channel stable|experimental]\n"
    "                       [--repository OWNER/NAME] [--json]\n"
    "Commands:\n"
    "  --check            Query GitHub releases for the newest version in the channel\n"
    "  --apply [--version V]  Download, verify the Ed25519-signed manifest and payload\n"
    "                     SHA-256, stage and atomically activate versions/<V>\n"
    "  --rollback         Reactivate the previous installed version\n"
    "  --status           Print active, previous and installed versions as JSON\n"
    "  --version, --help\n"
    "Settings: ${XDG_CONFIG_HOME:-~/.config}/kabus22/updates.ini [Updates] repository=, channel=.\n"
    "Only bin/, lib/, share/, current and versions/ change; game/, saves and options never do.\n";
struct Options {
    std::string command, channel, repository, version;
    fs::path root;
    bool json = false;
};
void require(bool good, const std::string& message) {
    if (!good) throw std::runtime_error(message);
}
void progress(int percent, const std::string& message) {
    std::cout << "progress " << percent << ' ' << message << std::endl;
}
fs::path resolveRoot(const Options& options) {
    fs::path root = options.root;
    if (root.empty()) {
        std::error_code error;
        root = layout::find_root(fs::read_symlink("/proc/self/exe", error));
        require(!root.empty(), "Cannot locate the installation; pass --install-dir ROOT");
    }
    root = fs::absolute(root).lexically_normal();
    require(fs::is_regular_file(fs::symlink_status(root/".kabus22-owned")), "Not a Kabus22-owned installation: " + root.string());
    return root;
}
std::string currentVersion(const fs::path& root) {
    const auto active = layout::active_version(root);
    return active.empty() ? K22_VERSION : active; // legacy flat install = this updater's own payload
}
std::vector<update::Release> releases(const Options& options) {
    return update::parse_releases(update::fetch(update::api_base() + "/repos/" + options.repository + "/releases?per_page=50", 8 << 20));
}
const update::Asset& asset(const update::Release& release, const std::string& name) {
    for (const auto& item : release.assets) if (item.name == name) return item;
    throw std::runtime_error("Release " + release.version + " lacks asset " + name);
}
int check(const Options& options) {
    std::error_code error;
    const bool located = !options.root.empty() || !layout::find_root(fs::read_symlink("/proc/self/exe", error)).empty();
    const auto current = located ? currentVersion(resolveRoot(options)) : std::string(K22_VERSION);
    const auto list = releases(options);
    const auto* latest = update::select(list, options.channel);
    const bool available = latest && update::compare_versions(latest->version, current) > 0;
    if (options.json) {
        std::cout << json{{"current", current}, {"latest", latest ? latest->version : current}, {"channel", options.channel},
            {"update_available", available}, {"notes", latest ? latest->notes : ""}, {"published", latest ? latest->published : ""},
            {"repository", options.repository}}.dump() << '\n';
    } else if (!latest) {
        std::cout << "No " << options.channel << " releases found in " << options.repository << "; installed " << current << '\n';
    } else {
        std::cout << "Installed " << current << ", latest " << options.channel << ' ' << latest->version
                  << (available ? ": update available\n" : ": up to date\n");
    }
    return 0;
}
// Removes leftover staging/download files on failure; committed stages are renamed away.
struct Scratch {
    fs::path stage, download;
    ~Scratch() { std::error_code error; if (!stage.empty()) fs::remove_all(stage, error); if (!download.empty()) fs::remove(download, error); }
};
int apply(const Options& options) {
    const auto root = resolveRoot(options);
    layout::Lock lock(root);
    progress(0, "Checking installation layout");
    if (layout::active_version(root).empty()) {
        progress(2, "Migrating flat installation to versions/" + std::string(K22_VERSION));
        layout::migrate_flat(root, K22_VERSION);
    }
    const auto current = layout::active_version(root);
    progress(4, "Querying " + options.repository + " (" + options.channel + ")");
    const auto list = releases(options);
    const auto* release = update::select(list, options.channel, options.version);
    require(release, options.version.empty() ? "No " + options.channel + " release found"
        : "Release " + options.version + " not found in channel " + options.channel);
    if (release->version == current || (options.version.empty() && update::compare_versions(release->version, current) <= 0)) {
        std::cout << "Already up to date (" << current << ")\n";
        return 0;
    }
    progress(6, "Verifying signed manifest for " + release->version);
    const auto manifestText = update::fetch(asset(*release, "kabus22-update.json").url, 64 << 10);
    const auto signature = update::fetch(asset(*release, "kabus22-update.json.sig").url, 4 << 10);
    const auto manifest = update::verify_manifest(manifestText, signature, update::public_key());
    require(manifest.version == release->version, "Signed manifest version " + manifest.version + " does not match release " + release->version);
    require(options.channel == "experimental" || manifest.channel == "stable", "Signed manifest is not a stable release");
    update::require_glibc(manifest.minGlibc);
    const auto& payload = asset(*release, manifest.payloadName);
    require(!payload.size || payload.size == manifest.size, "Release asset size differs from signed manifest");
    Scratch scratch;
    scratch.stage = layout::stage_dir(root, manifest.version);
    scratch.download = scratch.stage.string() + ".tar.gz"; // unique sibling; prune removes leftovers
    progress(10, "Downloading " + manifest.payloadName);
    const auto digest = update::download(payload.url, scratch.download, manifest.size, progress);
    require(digest == manifest.sha256, "Payload SHA-256 mismatch");
    progress(75, "Extracting payload");
    update::extract_payload(scratch.download, scratch.stage, manifest.version);
    progress(95, "Activating " + manifest.version);
    layout::commit(root, scratch.stage, manifest.version);
    scratch.stage.clear();
    progress(100, "Updated " + (current.empty() ? std::string("installation") : current) + " -> " + manifest.version);
    return 0;
}
int rollback(const Options& options) {
    const auto root = resolveRoot(options);
    layout::Lock lock(root);
    const auto from = layout::active_version(root);
    layout::rollback(root);
    std::cout << "Rolled back " << from << " -> " << layout::active_version(root) << '\n';
    return 0;
}
int status(const Options& options) {
    const auto root = resolveRoot(options);
    std::cout << json{{"active", layout::active_version(root)}, {"previous", layout::previous_version(root)},
        {"installed", layout::installed_versions(root)}}.dump() << '\n';
    return 0;
}
std::optional<Options> parse(int argc, char** argv) {
    Options options;
    const auto settings = update::load_settings();
    options.channel = settings.channel;
    options.repository = settings.repository;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::invalid_argument(flag + " requires a value");
            return argv[++i];
        };
        if (flag == "--help" || flag == "-h") { std::cout << usage; return std::nullopt; }
        // "--apply --version V" selects a release; a bare --version prints the identity.
        if (flag == "--version" && (i + 1 >= argc || argv[i + 1][0] == '-')) { std::cout << "kabus22 native updater " << K22_VERSION << '\n'; return std::nullopt; }
        if (flag == "--check" || flag == "--apply" || flag == "--rollback" || flag == "--status") {
            if (!options.command.empty()) throw std::invalid_argument("Choose exactly one command");
            options.command = flag;
        } else if (flag == "--install-dir") options.root = value();
        else if (flag == "--channel") options.channel = value();
        else if (flag == "--repository") options.repository = value();
        else if (flag == "--version") options.version = value();
        else if (flag == "--json") options.json = true;
        else throw std::invalid_argument("Unknown argument: " + flag);
    }
    if (options.command.empty()) throw std::invalid_argument("Choose a command");
    if (options.channel != "stable" && options.channel != "experimental") throw std::invalid_argument("--channel must be stable or experimental");
    if (!update::valid_repository(options.repository)) throw std::invalid_argument("--repository must be OWNER/NAME");
    if (!options.version.empty() && (options.command != "--apply" || !layout::valid_version(options.version)))
        throw std::invalid_argument("--version V needs --apply and a semantic version");
    return options;
}
}

int main(int argc, char** argv) {
    std::optional<Options> options;
    try { options = parse(argc, argv); }
    catch (const std::invalid_argument& error) { std::cerr << "kabus22-updater: " << error.what() << '\n' << usage; return 2; }
    if (!options) return 0;
    try {
        require(::geteuid() != 0, "Run the updater as your ordinary user, not root");
        require(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK, "Cannot initialize libcurl");
        if (options->command == "--check") return check(*options);
        if (options->command == "--apply") return apply(*options);
        if (options->command == "--rollback") return rollback(*options);
        return status(*options);
    } catch (const std::exception& error) {
        std::cerr << "kabus22-updater: " << error.what() << '\n';
        return 1;
    }
}
