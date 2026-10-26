#include "layout.hpp"
#include <algorithm>
#include <array>
#include <tuple>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace k22::layout {
namespace {
constexpr const char* payloadDirs[] = {"bin", "lib", "share"};
void require(bool good, const std::string& message) {
    if (!good) throw std::runtime_error(message);
}
void fail(const std::string& what, const fs::path& path) {
    throw std::runtime_error(what + ": " + path.string() + ": " + std::strerror(errno));
}
bool present(const fs::path& path) { return fs::symlink_status(path).type() != fs::file_type::not_found; }
bool realDir(const fs::path& path) { return fs::symlink_status(path).type() == fs::file_type::directory; }
void syncDir(const fs::path& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) fail("Cannot open directory", path);
    const int result = ::fsync(fd);
    ::close(fd);
    if (result != 0) fail("Cannot sync directory", path);
}
fs::path versionsOf(const fs::path& root) {
    require(fs::symlink_status(root/".kabus22-owned").type() == fs::file_type::regular,
        "Not a Kabus22-owned installation: " + root.string());
    const auto versions = root/"versions";
    if (!present(versions) && ::mkdir(versions.c_str(), 0755) != 0 && errno != EEXIST) fail("Cannot create", versions);
    require(realDir(versions), "versions is not a real directory: " + versions.string());
    return versions;
}
// Temporary symlinks live in versions/ (same filesystem); rename(2) atomically
// replaces the root entry, never a real directory (rename fails with EISDIR).
void swapLink(const fs::path& root, const char* name, const std::string& target) {
    const auto temporary = root/"versions"/(".tmp-" + std::string(name) + "-" + std::to_string(::getpid()));
    ::unlink(temporary.c_str());
    if (::symlink(target.c_str(), temporary.c_str()) != 0) fail("Cannot create symlink", temporary);
    if (::rename(temporary.c_str(), (root/name).c_str()) != 0) {
        const int error = errno;
        ::unlink(temporary.c_str());
        errno = error;
        fail("Cannot replace", root/name);
    }
    syncDir(root);
}
void compatLinks(const fs::path& root) {
    for (const auto* name : payloadDirs) {
        const auto target = std::string("current/") + name;
        std::error_code error;
        if (fs::is_symlink(fs::symlink_status(root/name)) && fs::read_symlink(root/name, error) == target) continue;
        require(!realDir(root/name), "Legacy flat layout; migrate first: " + (root/name).string());
        swapLink(root, name, target);
    }
}
void writeState(const fs::path& root, std::string_view active, std::string_view previous) {
    const auto state = root/"versions/.state", temporary = root/"versions/.state.tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << "active=" << active << "\nprevious=" << previous << '\n';
        out.flush();
        require(bool(out), "Cannot write " + temporary.string());
    }
    const int fd = ::open(temporary.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0 || ::fsync(fd) != 0) { if (fd >= 0) ::close(fd); fail("Cannot sync", temporary); }
    ::close(fd);
    if (::rename(temporary.c_str(), state.c_str()) != 0) fail("Cannot replace", state);
    syncDir(root/"versions");
}
std::pair<std::string, std::string> readState(const fs::path& root) {
    std::ifstream in(root/"versions/.state");
    std::string line, active, previous;
    while (std::getline(in, line)) {
        if (line.starts_with("active=")) active = line.substr(7);
        else if (line.starts_with("previous=")) previous = line.substr(9);
    }
    return {valid_version(active) ? active : "", valid_version(previous) ? previous : ""};
}
bool versionDir(const fs::path& root, const std::string& version) {
    return valid_version(version) && realDir(root/"versions"/version);
}
void prune(const fs::path& root, const std::vector<std::string>& keep) {
    for (const auto& entry : fs::directory_iterator(root/"versions")) {
        const auto name = entry.path().filename().string();
        const bool scratch = name.starts_with(".stage-") || name.starts_with(".tmp-") || name.starts_with(".download-");
        if ((!scratch && !valid_version(name)) || std::find(keep.begin(), keep.end(), name) != keep.end()) continue;
        fs::remove_all(entry.path());
    }
    syncDir(root/"versions");
}
} // namespace

fs::path find_root(const fs::path& realExecutable) {
    std::error_code error;
    auto path = fs::weakly_canonical(realExecutable, error);
    if (error) return {};
    for (int level = 0; level < 4 && path.has_parent_path() && path != path.root_path(); ++level) {
        path = path.parent_path();
        if (fs::symlink_status(path/".kabus22-owned", error).type() == fs::file_type::regular) return path;
    }
    return {};
}
bool valid_version(std::string_view version) {
    static const std::regex pattern(R"((0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?)");
    return version.size() <= 64 && std::regex_match(version.begin(), version.end(), pattern);
}
std::string active_version(const fs::path& root) {
    std::error_code error;
    const auto target = fs::read_symlink(root/"current", error).string();
    if (error || !target.starts_with("versions/")) return {};
    const auto version = target.substr(9);
    return versionDir(root, version) ? version : "";
}
std::string previous_version(const fs::path& root) {
    const auto active = active_version(root);
    const auto [recorded, previous] = readState(root);
    // A crash after the current swap but before .state leaves recorded == old active.
    const auto candidate = recorded == active ? previous : recorded;
    return candidate != active && versionDir(root, candidate) ? candidate : "";
}
std::vector<std::string> installed_versions(const fs::path& root) {
    std::vector<std::string> result;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(root/"versions", error))
        if (const auto name = entry.path().filename().string(); versionDir(root, name)) result.push_back(name);
    // Semantic order (string order put 0.4.10 before 0.4.9): numeric major.minor.patch, then a release after its prereleases.
    // shortcut: prerelease tags compare as plain strings (beta.10 < beta.9); upgrade if numbered prereleases pass 9.
    auto key = [](const std::string& v) {
        std::array<unsigned long, 3> core{};
        const auto dash = v.find('-'), plus = v.find('+');
        const auto end = std::min(dash, plus);
        std::size_t at = 0;
        for (auto& part : core) { part = std::strtoul(v.c_str() + at, nullptr, 10); at = v.find('.', at); if (at == std::string::npos || at >= end) break; ++at; }
        const std::string pre = dash < plus ? v.substr(dash + 1, plus == std::string::npos ? std::string::npos : plus - dash - 1) : std::string{};
        return std::tuple(core, pre.empty(), pre);
    };
    std::sort(result.begin(), result.end(), [&](const std::string& a, const std::string& b) { return key(a) < key(b); });
    return result;
}
std::string running_version(const fs::path& root) {
    std::error_code error;
    const auto self = fs::read_symlink("/proc/self/exe", error);
    const auto versions = fs::weakly_canonical(root/"versions", error);
    if (error) return {};
    const auto relative = self.lexically_relative(versions);
    if (relative.empty() || *relative.begin() == "..") return {};
    const auto name = relative.begin()->string();
    return valid_version(name) ? name : "";
}
fs::path stage_dir(const fs::path& root, std::string_view version) {
    require(valid_version(version), "Invalid version: " + std::string(version));
    auto pattern = (versionsOf(root)/(".stage-" + std::string(version) + "-XXXXXX")).string();
    require(::mkdtemp(pattern.data()) != nullptr, "Cannot create staging directory: " + std::string(std::strerror(errno)));
    return pattern;
}
void commit(const fs::path& root, const fs::path& staged, std::string_view versionView) {
    const std::string version(versionView);
    require(valid_version(version), "Invalid version: " + version);
    const auto versions = versionsOf(root);
    require(staged.parent_path() == versions && staged.filename().string().starts_with(".stage-") && realDir(staged),
        "Staged payload must come from stage_dir: " + staged.string());
    require(realDir(staged/"bin"), "Staged payload lacks bin/");
    for (const auto* name : payloadDirs) fs::create_directory(staged/name);
    for (const auto* name : payloadDirs) require(!realDir(root/name), "Legacy flat layout; migrate first: " + (root/name).string());
    const auto old = active_version(root);
    require(version != old, "Version " + version + " is already active");
    const auto target = versions/version;
    if (present(target)) {
        require(version != running_version(root), "Refusing to replace the running version " + version);
        fs::remove_all(target);
    }
    if (::rename(staged.c_str(), target.c_str()) != 0) fail("Cannot publish version", target);
    syncDir(versions);
    swapLink(root, "current", "versions/" + version);
    compatLinks(root);
    writeState(root, version, old);
    prune(root, {version, old, running_version(root)});
}
void rollback(const fs::path& root) {
    versionsOf(root);
    const auto previous = previous_version(root), active = active_version(root);
    require(!previous.empty(), "No previous version to roll back to");
    swapLink(root, "current", "versions/" + previous);
    compatLinks(root);
    writeState(root, previous, active);
}
void migrate_flat(const fs::path& root, std::string_view versionView) {
    const std::string version(versionView);
    require(valid_version(version), "Invalid version: " + version);
    const auto versions = versionsOf(root);
    const auto target = versions/version;
    const bool flat = std::any_of(std::begin(payloadDirs), std::end(payloadDirs), [&](const char* name) { return realDir(root/name); });
    if (!flat && present(root/"current")) {
        // Resume a migration interrupted after the current swap.
        if (!active_version(root).empty()) { compatLinks(root); if (readState(root).first.empty()) writeState(root, active_version(root), ""); }
        return;
    }
    require(flat || realDir(target/"bin"), "No flat installation payload to migrate in " + root.string());
    require(!present(root/"current"), "Inconsistent layout: current exists beside flat payload");
    if (!present(target) && ::mkdir(target.c_str(), 0755) != 0) fail("Cannot create", target);
    require(realDir(target), "Version path is not a directory: " + target.string());
    for (const auto* name : payloadDirs) require(!present(root/name) || realDir(root/name) || fs::is_symlink(fs::symlink_status(root/name)),
        "Unexpected non-directory entry: " + (root/name).string());
    for (const auto* name : payloadDirs) {
        if (realDir(root/name)) {
            require(!present(target/name), "Both flat and versioned " + std::string(name) + " exist; refusing to merge");
            if (::rename((root/name).c_str(), (target/name).c_str()) != 0) fail("Cannot move", root/name);
            syncDir(root); syncDir(target);
        } else if (!present(target/name)) {
            fs::create_directory(target/name);
        }
    }
    swapLink(root, "current", "versions/" + version);
    // Missing compat links (moved dirs) are created; never replaces unrelated files.
    compatLinks(root);
    writeState(root, version, readState(root).first == version ? readState(root).second : "");
}
Lock::Lock(const fs::path& root) : fd(-1) {
    const auto path = versionsOf(root)/".lock";
    fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) fail("Cannot open lock", path);
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) { ::close(fd); throw std::runtime_error("Another Kabus22 update/installation is in progress"); }
}
Lock::~Lock() { if (fd >= 0) ::close(fd); }
}
