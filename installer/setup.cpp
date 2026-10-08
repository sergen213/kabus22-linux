#include "setup.hpp"
#include "media.hpp"
#include "../native/pe_font.hpp"
#include <glib.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <elf.h>
#include <exception>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string_view>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>
#include <utility>

namespace k22::setup {
namespace {
constexpr const char* marker = "Kabus22 native installation 1\n";
constexpr const char* shortcutName = "kabus22-native.desktop";
constexpr std::uint64_t maxPayload = 4ULL * 1024 * 1024 * 1024;
void require(bool good, const std::string& message) {
    if (!good) throw std::runtime_error(message);
}
void report(const Progress& callback, const std::string& stage, std::uint64_t done = 0, std::uint64_t total = 0) {
    if (callback && !callback(stage, done, total)) throw Cancelled();
}
void cleanText(const std::string& text) {
    for (unsigned char c : text) require(c >= 32 && c != 127, "Paths cannot contain control characters");
}
fs::path checkedAbsolute(const fs::path& path) {
    require(!path.empty(), "Required path is empty");
    cleanText(path.string());
    auto result = fs::absolute(path).lexically_normal();
    while (result != result.root_path() && result.filename().empty()) result = result.parent_path();
    return result;
}
bool within(const fs::path& parent, const fs::path& child) {
    auto a = parent.begin(), b = child.begin();
    for (; a != parent.end(); ++a, ++b) if (b == child.end() || *a != *b) return false;
    return true;
}
struct Identity {
    fs::path path;
    struct stat info{};
};
bool same(const struct stat& a, const struct stat& b, bool contents = false) {
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino && (a.st_mode & S_IFMT) == (b.st_mode & S_IFMT) &&
        (!contents || (a.st_size == b.st_size && a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec));
}
bool sameSnapshot(const struct stat& expected, const struct stat& current) {
    return same(expected, current, true) && expected.st_nlink == current.st_nlink &&
        expected.st_ctim.tv_sec == current.st_ctim.tv_sec && expected.st_ctim.tv_nsec == current.st_ctim.tv_nsec;
}
struct stat lstatPath(const fs::path& path, bool missing = false) {
    struct stat value{};
    if (::lstat(path.c_str(), &value) != 0) {
        if (missing && errno == ENOENT) return {};
        throw std::runtime_error("Cannot inspect " + path.string() + ": " + std::strerror(errno));
    }
    require(!S_ISLNK(value.st_mode), "Refusing symbolic link: " + path.string());
    return value;
}
struct Created {
    std::vector<Identity> directories;
    ~Created() {
        for (auto it = directories.rbegin(); it != directories.rend(); ++it) {
            struct stat now{};
            if (::lstat(it->path.c_str(), &now) == 0 && same(it->info, now)) ::rmdir(it->path.c_str());
        }
    }
};
void chain(const fs::path& path, Created* created = nullptr) {
    fs::path current = path.root_path();
    for (const auto& part : path.relative_path()) {
        current /= part;
        auto value = lstatPath(current, true);
        if (!value.st_mode) {
            if (!created) return;
            require(::mkdir(current.c_str(), 0700) == 0, "Cannot create private directory: " + current.string());
            value = lstatPath(current);
            created->directories.push_back({current, value});
        }
        require(S_ISDIR(value.st_mode), "Directory component is not a directory: " + current.string());
        const bool shared = value.st_mode & (S_IWGRP | S_IWOTH);
        const bool sticky = (value.st_mode & S_ISVTX) && (value.st_uid == 0 || value.st_uid == geteuid());
        require(!shared || sticky, "Unsafe writable ancestor: " + current.string());
    }
}
fs::path writableAncestor(fs::path path) {
    chain(path);
    while (!lstatPath(path, true).st_mode) path = path.parent_path();
    require(::access(path.c_str(), W_OK | X_OK) == 0, "Folder is not writable: " + path.string());
    return path;
}
struct Fd {
    int value = -1;
    explicit Fd(int fd) : value(fd) { require(value >= 0, "Cannot open file: " + std::string(std::strerror(errno))); }
    ~Fd() { if (value >= 0) ::close(value); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};
struct Temporary {
    fs::path path;
    struct stat identity{};
    bool retain = false;
    explicit Temporary(const fs::path& parent) {
        auto name = (parent/".kabus22-setup-XXXXXX").string();
        require(::mkdtemp(name.data()) != nullptr, "Cannot create private staging directory: " + std::string(std::strerror(errno)));
        path = name; identity = lstatPath(path);
    }
    ~Temporary() {
        struct stat now{};
        if (!retain && ::lstat(path.c_str(), &now) == 0 && same(identity, now)) {
            std::error_code error; fs::remove_all(path, error);
        }
    }
};
void mkdirPrivate(const fs::path& path) {
    require(::mkdir(path.c_str(), 0700) == 0, "Cannot create private staging directory: " + path.string());
}
void writeAll(int fd, const char* data, std::size_t size) {
    while (size) {
        const auto count = ::write(fd, data, size);
        if (count < 0 && errno == EINTR) continue;
        require(count > 0, "Cannot write staged file: " + std::string(std::strerror(errno)));
        data += count; size -= static_cast<std::size_t>(count);
    }
}
void writeFile(const fs::path& path, std::string_view data, mode_t mode = 0600) {
    Fd out(::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode));
    writeAll(out.value, data.data(), data.size());
    require(::fsync(out.value) == 0, "Cannot synchronize file: " + path.string());
}
std::string smallFile(const fs::path& path, std::size_t maximum = 65536) {
    Fd in(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    struct stat value{};
    require(::fstat(in.value, &value) == 0 && S_ISREG(value.st_mode) && value.st_size >= 0 &&
        static_cast<std::uint64_t>(value.st_size) <= maximum && value.st_uid == geteuid() && value.st_nlink == 1 &&
        !(value.st_mode & (S_IWGRP | S_IWOTH)), "Invalid private ownership record: " + path.string());
    std::string data(static_cast<std::size_t>(value.st_size), '\0');
    std::size_t offset = 0;
    while (offset < data.size()) {
        const auto count = ::read(in.value, data.data() + offset, data.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        require(count > 0, "Cannot read ownership record"); offset += static_cast<std::size_t>(count);
    }
    return data;
}
void nativeElf(const fs::path& file, bool executable) {
    Fd in(::open(file.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    struct stat value{}; Elf64_Ehdr header{};
    require(::fstat(in.value, &value) == 0 && S_ISREG(value.st_mode) && value.st_size >= static_cast<off_t>(sizeof(header)) &&
        static_cast<std::uint64_t>(value.st_size) <= maxPayload, "Invalid native ELF payload: " + file.string());
    require(::pread(in.value, &header, sizeof(header), 0) == sizeof(header) &&
        std::memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_ident[EI_CLASS] == ELFCLASS64 &&
        header.e_ident[EI_DATA] == ELFDATA2LSB && header.e_ident[EI_VERSION] == EV_CURRENT &&
        header.e_machine == EM_X86_64 && header.e_version == EV_CURRENT && header.e_ehsize == sizeof(header) &&
        (header.e_type == ET_EXEC || header.e_type == ET_DYN) && (!executable || (header.e_entry != 0 && ::access(file.c_str(), X_OK) == 0)),
        "Payload must be a real native x86-64 ELF, not Windows, Wine, or a shell script: " + file.string());
}
std::uint64_t copyDescriptor(int in, const struct stat& initial, const fs::path& relative,
                             const fs::path& to, mode_t mode, const Progress& progress) {
    Fd out(::open(to.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode));
    std::array<char, 128 * 1024> buffer{};
    std::uint64_t done = 0;
    for (;;) {
        const auto count = ::read(in, buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR) continue;
        require(count >= 0, "Cannot read source file: " + relative.string());
        if (!count) break;
        require(done <= static_cast<std::uint64_t>(initial.st_size) && static_cast<std::uint64_t>(count) <= static_cast<std::uint64_t>(initial.st_size) - done, "Source grew while copying");
        writeAll(out.value, buffer.data(), static_cast<std::size_t>(count));
        done += static_cast<std::uint64_t>(count); report(progress, "Copying " + relative.filename().string(), done, initial.st_size);
    }
    struct stat current{};
    require(::fstat(in, &current) == 0 && sameSnapshot(initial, current) && done == static_cast<std::uint64_t>(initial.st_size),
        "Source changed while copying: " + relative.string());
    require(::fsync(out.value) == 0, "Cannot synchronize copied file"); return done;
}
std::uint64_t copyFile(const fs::path& from, const fs::path& to, mode_t mode,
                       const Progress& progress, std::uint64_t maximum = maxPayload) {
    Fd in(::open(from.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    struct stat initial{};
    require(::fstat(in.value, &initial) == 0 && S_ISREG(initial.st_mode) && initial.st_size >= 0 &&
        static_cast<std::uint64_t>(initial.st_size) <= maximum, "Invalid source file: " + from.string());
    return copyDescriptor(in.value, initial, from, to, mode, progress);
}
std::string lower(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return text;
}
std::string desktopValue(const std::string& text) {
    std::string output;
    for (char c : text) {
        switch (c) {
        case '\\': output += "\\\\"; break;
        case ' ': output += "\\s"; break;
        case '\t': output += "\\t"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        default: output += c;
        }
    }
    return output;
}
std::string execArgument(const std::string& text) {
    std::string quoted = "\"";
    for (char c : text) {
        if (c == '%') quoted += "%%";
        else { if (c == '\\' || c == '"' || c == '`' || c == '$') quoted += '\\'; quoted += c; }
    }
    quoted += '"'; return desktopValue(quoted);
}
std::string desktopEntry(const Result& result) {
    // env avoids GIO treating %% in argv[0] as an unexpanded executable path.
    const auto command = "/usr/bin/env -- " + execArgument(result.executable.string()) +
        " --data " + execArgument(result.dataRoot.string());
    return "[Desktop Entry]\nType=Application\nVersion=1.0\nName=Kabus22\nComment=Play Kabus22 natively\nExec=" + command +
        "\nIcon=" + desktopValue((result.root/"kabus22.png").string()) + "\nPath=" + desktopValue(result.dataRoot.string()) +
        "\nTerminal=false\nCategories=Game;\nActions=VulkanRayTracing;VulkanRaster;\nX-Kabus22-Native=true\nX-Kabus22-InstallRoot=" + desktopValue(result.root.string()) +
        "\n\n[Desktop Action VulkanRayTracing]\nName=Vulkan (ray tracing)\nExec=" + command + " --renderer vulkan" +
        "\n\n[Desktop Action VulkanRaster]\nName=Vulkan (raster only)\nExec=" + command + " --renderer vulkan --ray-tracing off\n";
}
std::string json(const std::string& text) {
    std::string out = "\"";
    for (char c : text) { if (c == '\\' || c == '"') out += '\\'; out += c; }
    return out + '"';
}
std::string sha256(const fs::path& path, const Progress& progress) {
    Fd in(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    std::unique_ptr<GChecksum, decltype(&g_checksum_free)> checksum(g_checksum_new(G_CHECKSUM_SHA256), g_checksum_free);
    require(bool(checksum), "Cannot allocate inventory checksum");
    std::array<guchar, 128 * 1024> bytes{};
    std::uint64_t done = 0;
    for (;;) {
        const auto count = ::read(in.value, bytes.data(), bytes.size());
        if (count < 0 && errno == EINTR) continue;
        require(count >= 0, "Cannot read inventory member"); if (!count) break;
        g_checksum_update(checksum.get(), bytes.data(), count); done += static_cast<std::uint64_t>(count);
        report(progress, "Recording installed inventory", done, fs::file_size(path));
    }
    return g_checksum_get_string(checksum.get());
}
void renameNew(const fs::path& from, const fs::path& to) {
    require(::syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), 1U) == 0,
        "Cannot publish without replacing existing files: " + to.string() + ": " + std::strerror(errno));
}
std::vector<Identity> tree(const fs::path& root) {
    std::vector<Identity> result{{root, lstatPath(root)}};
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        const auto value = lstatPath(entry.path());
        require(S_ISDIR(value.st_mode) || (S_ISREG(value.st_mode) && value.st_nlink == 1), "Unexpected link or special file in staging: " + entry.path().string());
        cleanText(entry.path().string()); result.push_back({entry.path(), value});
    }
    return result;
}
Fd directoryFd(const fs::path& path) {
    Fd current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    for (const auto& part : path.relative_path()) {
        const int next = ::openat(current.value, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        require(next >= 0, "Cannot pin directory without following links: " + path.string());
        ::close(current.value); current.value = next;
    }
    return Fd(std::exchange(current.value, -1));
}
void rollbackOwned(const fs::path& source, const fs::path& destination, const std::vector<Identity>& identities) {
    auto parent = directoryFd(destination.parent_path());
    Fd root(::openat(parent.value, destination.filename().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    struct stat now{};
    require(::fstat(root.value, &now) == 0 && same(identities.front().info, now), "Concurrent replacement preserved: " + destination.string());
    std::map<fs::path, struct stat> directories;
    for (const auto& item : identities) if (S_ISDIR(item.info.st_mode))
        directories.emplace(item.path == source ? fs::path{} : item.path.lexically_relative(source), item.info);
    for (auto it = identities.rbegin(); it != identities.rend(); ++it) {
        if (it->path == source) continue;
        const auto relative = it->path.lexically_relative(source);
        Fd cursor(::dup(root.value));
        fs::path prefix;
        bool unchanged = true;
        for (const auto& component : relative.parent_path()) {
            prefix /= component;
            const int next = ::openat(cursor.value, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (next < 0) { unchanged = false; break; }
            ::close(cursor.value); cursor.value = next;
            const auto known = directories.find(prefix);
            if (::fstat(cursor.value, &now) != 0 || known == directories.end() || !same(known->second, now)) { unchanged = false; break; }
        }
        if (!unchanged || ::fstatat(cursor.value, relative.filename().c_str(), &now, AT_SYMLINK_NOFOLLOW) != 0) continue;
        if (!same(it->info, now, !S_ISDIR(now.st_mode))) continue;
        ::unlinkat(cursor.value, relative.filename().c_str(), S_ISDIR(now.st_mode) ? AT_REMOVEDIR : 0);
    }
    const auto name = destination.filename().string();
    if (::fstatat(parent.value, name.c_str(), &now, AT_SYMLINK_NOFOLLOW) == 0 && same(identities.front().info, now))
        ::unlinkat(parent.value, name.c_str(), AT_REMOVEDIR);
    require(::fstatat(parent.value, name.c_str(), &now, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT,
        "Rollback preserved changed or unowned files at " + destination.string());
}
std::uint64_t addBytes(std::uint64_t total, std::uint64_t size) {
    require(size <= std::numeric_limits<std::uint64_t>::max() - total, "Installation size overflow"); return total + size;
}
void enough(const fs::path& where, std::uint64_t needed) {
    require(fs::space(where).available >= needed, "Insufficient free space at " + where.string() + ": need " + std::to_string(needed) + " bytes");
}
struct Payload { fs::path source, relative; std::uint64_t bytes; };
std::vector<Payload> payload(const fs::path& executable) {
    nativeElf(executable, true);
    std::vector<Payload> result{{executable, "bin/kabus22", fs::file_size(executable)}};
    const auto libraries = executable.parent_path().parent_path()/"lib";
    if (!fs::exists(libraries)) return result; // Developer engine may use audited host dependencies.
    require(S_ISDIR(lstatPath(libraries).st_mode), "Payload lib path is not a directory");
    static const std::regex library(R"(lib[A-Za-z0-9_+.-]+\.so(?:\.[0-9]+)*)");
    static const std::regex host(R"((?:ld-linux[^.]*|lib(?:c|m|dl|pthread|rt|resolv|util|anl|nss_[^.]+|GL|GLX|GLdispatch|OpenGL|EGL|GLESv[12]|vulkan|drm(?:_[^.]+)?|gbm|cuda|nvidia[^.]*|vdpau|va(?:-x11|-drm|-wayland)?))\.so(?:\.[0-9]+)*)");
    std::uint64_t total = result.front().bytes;
    for (const auto& entry : fs::directory_iterator(libraries)) {
        const auto name = entry.path().filename().string();
        if (!std::regex_match(name, library)) continue;
        require(!std::regex_match(name, host), "Do not bundle host OS/GPU libraries: " + name);
        const auto resolved = fs::canonical(entry.path());
        require(within(fs::canonical(libraries), resolved), "Library link leaves trusted payload: " + name);
        nativeElf(resolved, false);
        const auto bytes = fs::file_size(resolved); total = addBytes(total, bytes);
        require(total <= maxPayload && result.size() < 1024, "Native dependency payload exceeds bounds");
        result.push_back({resolved, fs::path("lib")/name, bytes});
    }
    const auto notices = executable.parent_path().parent_path()/"share/licenses/kabus22/dependencies";
    if (result.size() > 1) {
        require(fs::exists(notices) && S_ISDIR(lstatPath(notices).st_mode), "Bundled native libraries require their dependency license notices");
        for (const auto& entry : fs::recursive_directory_iterator(notices)) {
            const auto value = lstatPath(entry.path());
            require(S_ISDIR(value.st_mode) || (S_ISREG(value.st_mode) && value.st_nlink == 1), "Invalid dependency notice member");
            if (S_ISDIR(value.st_mode)) continue;
            const auto relative = entry.path().lexically_relative(notices);
            cleanText(relative.string());
            require(value.st_size >= 0 && value.st_size <= 64 * 1024 * 1024, "Oversized dependency notice");
            total = addBytes(total, value.st_size);
            require(total <= maxPayload && result.size() < 10000, "Dependency notices exceed payload bounds");
            result.push_back({entry.path(), fs::path("share/licenses/kabus22/dependencies")/relative, static_cast<std::uint64_t>(value.st_size)});
        }
    }
    return result;
}
std::string foldPath(std::string_view value) {
    require(g_utf8_validate(value.data(), static_cast<gssize>(value.size()), nullptr), "Invalid UTF-8 patch path");
    std::unique_ptr<gchar, decltype(&g_free)> folded(g_utf8_casefold(value.data(), static_cast<gssize>(value.size())), g_free);
    std::unique_ptr<gchar, decltype(&g_free)> normalized(g_utf8_normalize(folded.get(), -1, G_NORMALIZE_ALL_COMPOSE), g_free);
    require(normalized != nullptr, "Cannot normalize patch path");
    return normalized.get();
}
struct PatchFile {
    fs::path relative;
    struct stat info{};
    bool replace = false;
};
struct PatchSnapshot {
    fs::path path;
    Fd parent, root;
    std::map<fs::path, struct stat> directories;
    std::vector<PatchFile> files;
    std::optional<PatchFile> font;
    std::uint64_t bytes = 0;

    static Fd pinRoot(int parent, const fs::path& name) {
        struct stat named{}, pinned{};
        require(::fstatat(parent, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(named.st_mode),
            "Optional patch must be a directory, not a link");
        Fd directory(::openat(parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        require(::fstat(directory.value, &pinned) == 0 && sameSnapshot(named, pinned),
            "Optional patch root changed while pinning");
        return Fd(std::exchange(directory.value, -1));
    }

    explicit PatchSnapshot(const fs::path& input)
        : path(checkedAbsolute(input)), parent(directoryFd(path.parent_path())),
          root(pinRoot(parent.value, path.filename())) {
        struct stat pinned{};
        require(::fstat(root.value, &pinned) == 0, "Cannot inspect pinned patch root");
        directories.emplace(fs::path{}, pinned);
        std::set<std::string> names;
        std::uint64_t total = 0;
        std::size_t count = 0;
        enumerate(root.value, {}, 0, names, total, count);
        checkRoot();
    }

    void checkRoot() const {
        struct stat named{}, pinned{};
        const auto& expected = directories.at(fs::path{});
        require(::fstatat(parent.value, path.filename().c_str(), &named, AT_SYMLINK_NOFOLLOW) == 0 &&
            ::fstat(root.value, &pinned) == 0 && sameSnapshot(expected, named) && sameSnapshot(expected, pinned),
            "Patch root changed after snapshot");
    }

    Fd openDirectory(const fs::path& relative) const {
        checkRoot();
        Fd cursor(::dup(root.value));
        fs::path prefix;
        for (const auto& component : relative) {
            prefix /= component;
            const auto known = directories.find(prefix);
            require(known != directories.end(), "Unknown patch directory: " + prefix.string());
            Fd next(::openat(cursor.value, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
            struct stat current{};
            require(::fstat(next.value, &current) == 0 && sameSnapshot(known->second, current),
                "Patch directory changed after snapshot: " + prefix.string());
            ::close(cursor.value); cursor.value = std::exchange(next.value, -1);
        }
        return Fd(std::exchange(cursor.value, -1));
    }

    Fd openSource(const PatchFile& file) const {
        auto directory = openDirectory(file.relative.parent_path());
        Fd in(::openat(directory.value, file.relative.filename().c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
        struct stat current{};
        require(::fstat(in.value, &current) == 0 && S_ISREG(current.st_mode) && current.st_nlink == 1 &&
            sameSnapshot(file.info, current), "Patch file changed after snapshot: " + file.relative.string());
        return Fd(std::exchange(in.value, -1));
    }

    void copy(const PatchFile& file, const fs::path& to, const Progress& progress) const {
        auto in = openSource(file);
        copyDescriptor(in.value, file.info, file.relative, to, 0600, progress);
    }

    void verify() const {
        checkRoot();
        for (const auto& [relative, info] : directories) {
            auto directory = openDirectory(relative);
            struct stat current{};
            require(::fstat(directory.value, &current) == 0 && sameSnapshot(info, current),
                "Patch directory changed after snapshot: " + relative.string());
        }
        for (const auto& file : files) { auto in = openSource(file); }
        if (font) { auto in = openSource(*font); }
        checkRoot();
    }

    void matchGame(const fs::path& game) {
        std::map<std::string, fs::path> names;
        for (const auto& entry : fs::recursive_directory_iterator(game)) {
            const auto relative = entry.path().lexically_relative(game);
            require(names.emplace(foldPath(relative.generic_string()), relative).second,
                "Game has case-fold duplicate paths");
        }
        const auto match = [&](const fs::path& relative, bool directory) {
            const auto found = names.find(foldPath(relative.generic_string()));
            if (found == names.end()) return false;
            require(found->second == relative, "Patch changes existing filename case: " + relative.string());
            const auto value = lstatPath(game/relative);
            require(directory ? S_ISDIR(value.st_mode) : S_ISREG(value.st_mode),
                "Patch conflicts with a file or directory: " + relative.string());
            return true;
        };
        for (const auto& [relative, info] : directories)
            if (!relative.empty()) match(relative, true);
        for (auto& file : files) file.replace = match(file.relative, false);
    }

    void enumerate(int directory, const fs::path& prefix, std::size_t depth,
                   std::set<std::string>& names, std::uint64_t& total, std::size_t& count) {
        Fd duplicate(::dup(directory));
        std::unique_ptr<DIR, decltype(&closedir)> entries(::fdopendir(duplicate.value), closedir);
        require(bool(entries), "Cannot enumerate pinned patch directory");
        duplicate.value = -1;
        for (;;) {
            errno = 0;
            const auto* entry = ::readdir(entries.get());
            if (!entry) { require(errno == 0, "Cannot read pinned patch directory"); break; }
            const std::string component = entry->d_name;
            if (component == "." || component == "..") continue;
            const auto relative = prefix/component;
            const auto text = relative.generic_string();
            cleanText(text);
            require(text.size() <= 1024 && component.find('\\') == std::string::npos,
                "Unsafe patch path: " + text);
            const auto folded = foldPath(text);
            for (const gchar* p = text.c_str(); *p; p = g_utf8_next_char(p))
                require(!g_unichar_iscntrl(g_utf8_get_char(p)), "Control character in patch path");
            require(names.insert(folded).second, "Patch has case-fold duplicate names");
            require(++count <= 200000, "Patch exceeds safe entry limit");
            struct stat value{};
            require(::fstatat(directory, component.c_str(), &value, AT_SYMLINK_NOFOLLOW) == 0 &&
                (S_ISDIR(value.st_mode) || (S_ISREG(value.st_mode) && value.st_nlink == 1)),
                "Patch contains a link or special file: " + text);
            const bool data = *relative.begin() == "_data";
            if (S_ISDIR(value.st_mode)) {
                require(data && depth < 64, "Patch directories are restricted to bounded _data trees");
                Fd child(::openat(directory, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
                struct stat pinned{};
                require(::fstat(child.value, &pinned) == 0 && sameSnapshot(value, pinned),
                    "Patch directory changed while enumerating: " + text);
                directories.emplace(relative, pinned);
                enumerate(child.value, relative, depth + 1, names, total, count);
                require(::fstat(child.value, &pinned) == 0 && sameSnapshot(value, pinned),
                    "Patch directory changed while enumerating: " + text);
                continue;
            }
            require(value.st_size >= 0, "Invalid patch size");
            total = addBytes(total, static_cast<std::uint64_t>(value.st_size));
            require(total <= maxPayload, "Patch exceeds safe aggregate size limit");
            if (relative == "K22.exe") {
                require(value.st_size > 0 && value.st_size <= 64 * 1024 * 1024, "Invalid patch font source");
                font = PatchFile{relative, value};
            }
            const auto ext = lower(relative.extension().string());
            const bool rootAsset = prefix.empty() && (ext == ".wrs" || ext == ".ini");
            const bool nativeAsset = data && (ext == ".wdl" || ext == ".wrs" || ext == ".ini" || ext == ".wmb" || ext == ".mdl" || ext == ".pcx" || ext == ".bmp" || ext == ".tga" || ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".dds" || ext == ".ogg" || ext == ".wav" || ext == ".mp3" || ext == ".avi" || ext == ".txt" || ext == ".fx" || ext == ".dat" || ext == ".k22" || ext == ".ttf");
            if (prefix.empty() && !rootAsset) continue; // Validate but never install Windows components/readme.
            require(rootAsset || nativeAsset, "Patch only accepts WRS/INI and native assets inside _data: " + text);
            bytes = addBytes(bytes, static_cast<std::uint64_t>(value.st_size));
            files.push_back({relative, value});
        }
    }
};
void encodeIcon(const fs::path& source, const fs::path& destination) {
    require(fs::file_size(source) <= 4 * 1024 * 1024, "Original logo exceeds preview bound");
    std::ifstream input(source, std::ios::binary);
    std::vector<std::uint8_t> encoded(static_cast<std::size_t>(fs::file_size(source)) + AV_INPUT_BUFFER_PADDING_SIZE, 0);
    const auto length = encoded.size() - AV_INPUT_BUFFER_PADDING_SIZE;
    input.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(length)); require(bool(input), "Cannot read original logo");
    auto codecDelete = [](AVCodecContext* p) { avcodec_free_context(&p); };
    auto frameDelete = [](AVFrame* p) { av_frame_free(&p); };
    auto packetDelete = [](AVPacket* p) { av_packet_free(&p); };
    const auto* jpeg = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
    const auto* png = avcodec_find_encoder(AV_CODEC_ID_PNG);
    require(jpeg && png, "FFmpeg JPEG decoder/PNG encoder missing");
    std::unique_ptr<AVCodecContext, decltype(codecDelete)> decoder(avcodec_alloc_context3(jpeg), codecDelete), encoder(avcodec_alloc_context3(png), codecDelete);
    std::unique_ptr<AVFrame, decltype(frameDelete)> frame(av_frame_alloc(), frameDelete), rgba(av_frame_alloc(), frameDelete);
    std::unique_ptr<AVPacket, decltype(packetDelete)> packet(av_packet_alloc(), packetDelete);
    require(decoder && encoder && frame && rgba && packet, "Cannot allocate artwork codec");
    decoder->max_pixels = 4096 * 4096;
    require(avcodec_open2(decoder.get(), jpeg, nullptr) >= 0, "Cannot open logo decoder");
    packet->data = encoded.data(); packet->size = static_cast<int>(length);
    require(avcodec_send_packet(decoder.get(), packet.get()) >= 0 && avcodec_receive_frame(decoder.get(), frame.get()) >= 0 &&
        frame->width > 0 && frame->height > 0 && frame->width <= 4096 && frame->height <= 4096, "Invalid original logo");
    packet->data = nullptr; packet->size = 0;
    rgba->format = AV_PIX_FMT_RGBA; rgba->width = frame->width; rgba->height = frame->height;
    require(av_frame_get_buffer(rgba.get(), 32) >= 0, "Cannot allocate icon pixels");
    std::unique_ptr<SwsContext, decltype(&sws_freeContext)> scaler(sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format), rgba->width, rgba->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr), sws_freeContext);
    require(scaler && sws_scale(scaler.get(), frame->data, frame->linesize, 0, frame->height, rgba->data, rgba->linesize) == rgba->height, "Cannot convert original logo");
    encoder->width = rgba->width; encoder->height = rgba->height; encoder->pix_fmt = AV_PIX_FMT_RGBA; encoder->time_base = {1, 1};
    require(avcodec_open2(encoder.get(), png, nullptr) >= 0 && avcodec_send_frame(encoder.get(), rgba.get()) >= 0 && avcodec_receive_packet(encoder.get(), packet.get()) >= 0, "Cannot encode desktop icon");
    writeFile(destination, {reinterpret_cast<const char*>(packet->data), static_cast<std::size_t>(packet->size)});
}
void installFont(const Request& request, const media::Prepared& source,
                 const std::vector<media::Member>& listing, const fs::path& stage,
                 const fs::path& game, const PatchSnapshot* patch, const Progress& progress) {
    const auto scratch = stage/"font-source";
    mkdirPrivate(scratch);
    fs::path pe;
    std::string origin;
    if (patch && patch->font) {
        patch->copy(*patch->font, scratch/"K22.exe", progress);
        pe = scratch/"K22.exe"; origin = "user-patch/K22.exe";
    }
    if (pe.empty()) {
        std::vector<media::Member> candidates;
        for (const auto& member : listing)
            if (lower(member.path.generic_string()) == "app/k22.exe") candidates.push_back(member);
        require(candidates.size() == 1, "Original media must contain one engine K22.exe for genuine font recovery");
        media::extractFontSource(source, checkedAbsolute(request.extractor), candidates.front(), scratch, progress);
        pe = scratch/candidates.front().path;
        origin = candidates.front().path.generic_string();
    }
    report(progress, "Recovering original engine font");
    Fd file(::open(pe.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    struct stat before{};
    require(::fstat(file.value, &before) == 0 && S_ISREG(before.st_mode) && before.st_nlink == 1 &&
        before.st_size > 0 && before.st_size <= 64 * 1024 * 1024, "Invalid private engine font source");
    struct Mapping {
        void* data;
        std::size_t size;
        ~Mapping() { if (data != MAP_FAILED) ::munmap(data, size); }
    } mapping{::mmap(nullptr, before.st_size, PROT_READ, MAP_PRIVATE, file.value, 0), static_cast<std::size_t>(before.st_size)};
    require(mapping.data != MAP_FAILED, "Cannot map private engine font source");
    const auto font = k22::extract_engine_font(
        {static_cast<const std::uint8_t*>(mapping.data), mapping.size});
    const auto sourceHash = sha256(pe, progress);
    struct stat after{};
    require(::fstat(file.value, &after) == 0 && same(before, after, true) &&
        before.st_ctim.tv_sec == after.st_ctim.tv_sec && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec,
        "Engine font source changed during recovery");
    const auto provenance =
        "{\"source\":" + json(origin) + ",\"source_sha256\":" + json(sourceHash) +
        ",\"pcx_bytes\":" + std::to_string(font.size()) +
        ",\"pcx_crc32\":" + std::to_string(k22::pe_font_detail::fontCrc) +
        ",\"format\":\"A6 _a4font RGB24 PCX 192x72\"}\n";
    const auto publishFont = [&](const fs::path& dataRoot) {
        mkdirPrivate(dataRoot/"engine");
        writeFile(dataRoot/"engine/_a4font.pcx",
            {reinterpret_cast<const char*>(font.data()), font.size()});
        writeFile(dataRoot/"engine/_a4font-source.json", provenance);
    };
    publishFont(game);
    const auto mode = game/"Kabus 22 Mode";
    if (lstatPath(mode, true).st_mode) {
        require(S_ISDIR(lstatPath(mode).st_mode), "Original Mode asset root is not a directory");
        publishFont(mode);
    }
    std::error_code error;
    fs::remove_all(scratch, error);
    require(!error, "Cannot remove private Windows font source: " + error.message());
}
} // namespace

Request defaultPaths() {
    const auto* homeEnv = g_get_home_dir();
    require(homeEnv && fs::path(homeEnv).is_absolute(), "Absolute HOME is required");
    const auto* dataEnv = g_getenv("XDG_DATA_HOME");
    const fs::path data = dataEnv && *dataEnv ? fs::path(dataEnv) : fs::path(homeEnv)/".local/share";
    require(data.is_absolute(), "XDG_DATA_HOME must be absolute");
    const auto* desktop = g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP);
    Request result;
    result.installRoot = checkedAbsolute(data/"kabus22"); result.applicationsDir = checkedAbsolute(data/"applications");
    result.desktopDir = checkedAbsolute(desktop ? fs::path(desktop) : fs::path(homeEnv)/"Desktop");
    return result;
}
fs::path previewArtwork(const fs::path& source, const fs::path& extractor, const fs::path& scratch, const Progress& progress) {
    require(geteuid() != 0, "Run installer as your ordinary user, not root/sudo");
    const auto destination = checkedAbsolute(scratch);
    chain(destination.parent_path());
    require(!lstatPath(destination, true).st_mode, "Preview scratch already exists");
    mkdirPrivate(destination);
    mkdirPrivate(destination/"source");
    mkdirPrivate(destination/"artwork");
    const bool loose = lower(source.extension().string()) == ".exe";
    const auto prepared = media::prepare(loose ? fs::path{} : checkedAbsolute(source), loose ? checkedAbsolute(source) : fs::path{}, destination/"source", progress);
    const auto members = media::list(prepared, checkedAbsolute(extractor), progress);
    std::vector<media::Member> selected;
    for (const auto& member : members) if (lower(member.path.generic_string()) == "app/help/help_files/fon_logo.jpg") selected.push_back(member);
    require(selected.size() == 1, "Original game logo is not present in supplied media");
    media::extract(prepared, checkedAbsolute(extractor), selected, destination/"artwork", progress);
    return destination/"artwork"/selected.front().path;
}
Result install(const Request& request, const Progress& progress) {
    require(geteuid() != 0, "Run installer as your ordinary user, not root/sudo");
    require(request.sourceIso.empty() != request.sourceSetup.empty(), "Choose one ISO or original loose setup.exe, not both");
    const auto root = checkedAbsolute(request.installRoot), applications = checkedAbsolute(request.applicationsDir), desktop = checkedAbsolute(request.desktopDir);
    require(root != root.root_path() && !within(root, applications) && !within(root, desktop), "Shortcut folders must be outside installation root");
    chain(root.parent_path()); chain(applications); chain(desktop);
    require(!lstatPath(root, true).st_mode, "Destination exists; choose a new folder. Existing games are never adopted or overwritten.");
    for (const auto& folder : {applications, desktop}) {
        require(!lstatPath(folder/shortcutName, true).st_mode, "Refusing to overwrite an existing shortcut: " + (folder/shortcutName).string());
        enough(writableAncestor(folder), 65536);
    }
    const auto files = payload(checkedAbsolute(request.enginePayload));
    std::uint64_t payloadBytes = 0;
    for (const auto& file : files) payloadBytes = addBytes(payloadBytes, file.bytes);
    enough(writableAncestor(root.parent_path()), addBytes(payloadBytes, 65536));
    Created created;
    chain(root.parent_path(), &created); chain(applications, &created); chain(desktop, &created);
    Temporary stage(root.parent_path());
    mkdirPrivate(stage.path/"source");
    mkdirPrivate(stage.path/"extracted");
    const auto prepared = media::prepare(request.sourceIso.empty() ? fs::path{} : checkedAbsolute(request.sourceIso), request.sourceSetup.empty() ? fs::path{} : checkedAbsolute(request.sourceSetup), stage.path/"source", progress);
    const auto listed = media::list(prepared, checkedAbsolute(request.extractor), progress);
    std::vector<media::Member> members;
    std::uint64_t assetBytes = 0;
    for (const auto& member : listed) if (media::selected(member)) { members.push_back(member); assetBytes = addBytes(assetBytes, member.size); }
    require(!members.empty(), "Media has no native Kabus22 assets");
    std::unique_ptr<PatchSnapshot> patch;
    if (!request.optionalPatch.empty()) patch = std::make_unique<PatchSnapshot>(request.optionalPatch);
    std::uint64_t fontSourceBytes = 0;
    for (const auto& member : listed)
        if (lower(member.path.generic_string()) == "app/k22.exe") fontSourceBytes = member.size;
    if (patch && patch->font) fontSourceBytes = static_cast<std::uint64_t>(patch->font->info.st_size);
    require(fontSourceBytes > 0 && fontSourceBytes <= 64 * 1024 * 1024, "Missing or oversized engine font source");
    const auto fontReserve = addBytes(fontSourceBytes, 64 * 1024 * 1024);
    enough(stage.path, addBytes(addBytes(assetBytes, payloadBytes), fontReserve));
    media::extract(prepared, checkedAbsolute(request.extractor), members, stage.path/"extracted", progress);
    const auto installation = stage.path/"install";
    mkdirPrivate(installation);
    renameNew(stage.path/"extracted/app", installation/"game");
    if (patch) patch->matchGame(installation/"game");
    enough(stage.path, addBytes(addBytes(patch ? patch->bytes : 0, payloadBytes), fontReserve));
    Created patchDirectories;
    if (patch) for (const auto& file : patch->files) {
        const auto to = installation/"game"/file.relative;
        chain(to.parent_path(), &patchDirectories);
        if (file.replace) require(::unlink(to.c_str()) == 0, "Cannot replace staged asset with user patch");
        patch->copy(file, to, progress);
    }
    installFont(request, prepared, listed, stage.path, installation/"game", patch.get(), progress);
    mkdirPrivate(installation/"bin"); mkdirPrivate(installation/"lib");
    Created payloadDirectories;
    for (const auto& file : files) {
        chain((installation/file.relative).parent_path(), &payloadDirectories);
        copyFile(file.source, installation/file.relative, file.relative == "bin/kabus22" ? 0700 : 0600, progress);
    }
    fs::path logo;
    for (const auto& member : members) if (lower(member.path.generic_string()) == "app/help/help_files/fon_logo.jpg") logo = installation/"game"/member.path.lexically_relative("app");
    require(!logo.empty(), "Original logo is required for installed desktop icon");
    encodeIcon(logo, installation/"kabus22.png");
    writeFile(installation/".kabus22-owned", marker);
    std::ostringstream inventory;
    inventory << "{\"format\":1,\"root\":" << json(root.string()) << ",\"source\":" << json((request.sourceIso.empty() ? request.sourceSetup : request.sourceIso).string()) << ",\"files\":[";
    bool first = true;
    for (const auto& item : tree(installation)) {
        if (!S_ISREG(item.info.st_mode)) continue;
        if (!first) inventory << ',';
        first = false;
        inventory << "{\"path\":" << json(item.path.lexically_relative(installation).generic_string()) << ",\"size\":" << item.info.st_size << ",\"sha256\":" << json(sha256(item.path, progress)) << '}';
    }
    inventory << "],\"shortcuts\":[" << json((applications/shortcutName).string()) << ',' << json((desktop/shortcutName).string()) << "]}\n";
    writeFile(installation/".kabus22-inventory.json", inventory.str());
    const auto owned = tree(installation);
    Result result{root, root/"game", root/"bin/kabus22", applications/shortcutName, desktop/shortcutName};
    std::vector<std::unique_ptr<Temporary>> shortcuts;
    std::vector<Identity> published;
    for (const auto& folder : {applications, desktop}) {
        if (folder == desktop && desktop == applications && !shortcuts.empty()) continue;
        shortcuts.push_back(std::make_unique<Temporary>(folder));
        writeFile(shortcuts.back()->path/shortcutName, desktopEntry(result), 0700);
    }
    bool rootPublished = false;
    const auto checkRoot = [&] {
        chain(root.parent_path());
        require(same(owned.front().info, lstatPath(root)), "Installation root changed concurrently; refusing its replacement");
    };
    try {
        report(progress, "Publishing native installation");
        chain(root.parent_path());
        if (patch) patch->verify();
        renameNew(installation, root); rootPublished = true;
        checkRoot();
        for (std::size_t index = 0; index < shortcuts.size(); ++index) {
            report(progress, "Creating desktop shortcuts", index, shortcuts.size());
            checkRoot();
            const auto folder = index == 0 ? applications : desktop;
            chain(folder);
            const auto file = shortcuts[index]->path/shortcutName;
            auto identity = lstatPath(file);
            renameNew(file, folder/shortcutName); published.push_back({folder/shortcutName, identity});
        }
        checkRoot();
        // Completion is non-cancellable after atomic publications have committed.
    } catch (...) {
        const auto original = std::current_exception();
        std::string recovery;
        for (const auto& file : published) {
            try {
                auto folder = directoryFd(file.path.parent_path());
                struct stat now{};
                if (::fstatat(folder.value, file.path.filename().c_str(), &now, AT_SYMLINK_NOFOLLOW) == 0) {
                    if (same(file.info, now, true)) {
                        if (::unlinkat(folder.value, file.path.filename().c_str(), 0) != 0)
                            recovery += "\nCannot remove owned shortcut: " + file.path.string();
                    } else recovery += "\nChanged shortcut preserved: " + file.path.string();
                }
            } catch (const std::exception& error) { recovery += "\n" + std::string(error.what()); }
        }
        if (rootPublished) {
            try { rollbackOwned(installation, root, owned); }
            catch (const std::exception& error) { recovery += "\n" + std::string(error.what()); }
        }
        if (!recovery.empty()) throw std::runtime_error("Installation did not commit; manual recovery required:" + recovery);
        std::rethrow_exception(original);
    }
    return result;
}
int launch(const Result& result) {
    const auto root = checkedAbsolute(result.root);
    chain(root);
    const auto value = lstatPath(root);
    require(S_ISDIR(value.st_mode) && value.st_uid == geteuid() && !(value.st_mode & (S_IWGRP | S_IWOTH)), "Installation is not private and user-owned");
    require(smallFile(root/".kabus22-owned") == marker, "Missing native installation ownership marker");
    require(checkedAbsolute(result.executable) == root/"bin/kabus22" && checkedAbsolute(result.dataRoot) == root/"game", "Launch paths do not match native installation");
    chain(root/"bin");
    chain(root/"game");
    require(S_ISDIR(lstatPath(root/"game").st_mode), "Installed game data directory is missing");
    nativeElf(result.executable, true);
    const auto exe = result.executable.string(), data = result.dataRoot.string();
    gchar* argv[] = {const_cast<gchar*>(exe.c_str()), const_cast<gchar*>("--data"), const_cast<gchar*>(data.c_str()), nullptr};
    GError* error = nullptr; GPid child = 0;
    const auto ok = g_spawn_async(data.c_str(), argv, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, &child, &error);
    const std::string message = error ? error->message : "Cannot launch native game";
    if (error) g_error_free(error);
    require(ok, message); return static_cast<int>(child);
}
void selfCheck() {
    const auto fails = [](auto action) { try { action(); } catch (const std::exception&) { return true; } return false; };
    require(fails([] { checkedAbsolute(""); }), "Empty paths must fail");
    require(fails([] { checkedAbsolute(std::string("bad\npath")); }), "Control paths must fail");
    require(fails([] { checkedAbsolute(std::string("a\0b", 3)); }), "NUL paths must fail");
    require(within("/a/b", "/a/b/c") && !within("/a/b", "/a/bc"), "Component containment failed");
    require(execArgument("a% b") == R"("a%%\sb")", "Desktop percent/space quoting regression");
    require(execArgument("\\") == R"("\\\\")", "Desktop backslash quoting regression");
    require(execArgument("\"") == R"("\\"")", "Desktop quote escaping regression");
    require(execArgument("$`") == R"("\\$\\`")", "Desktop shell metacharacter quoting regression");
    require(fails([] { addBytes(std::numeric_limits<std::uint64_t>::max(), 1); }), "Size overflow must fail");
    struct stat a{}, b{}; a.st_ino = b.st_ino = 1; a.st_dev = b.st_dev = 2; a.st_mode = b.st_mode = S_IFREG; a.st_size = 1; b.st_size = 2;
    require(same(a, b) && !same(a, b, true), "Rollback must preserve modified files");
    b = a; ++b.st_ctim.tv_nsec;
    require(!sameSnapshot(a, b), "Source snapshot must include change time");
    const auto temporaryParent = checkedAbsolute(g_get_tmp_dir());
    chain(temporaryParent);
    Temporary sandbox(temporaryParent);
    const auto installation = sandbox.path/"install";
    mkdirPrivate(installation); mkdirPrivate(installation/"nested");
    writeFile(installation/"nested/owned", "preserve me");
    const auto identities = tree(installation);
    renameNew(installation/"nested", sandbox.path/"outside");
    fs::create_directory_symlink(sandbox.path/"outside", installation/"nested");
    require(fails([&] { rollbackOwned(installation, installation, identities); }),
        "Rollback must retain a replaced intermediate directory");
    require(smallFile(sandbox.path/"outside/owned") == "preserve me",
        "Rollback followed a replacement symlink outside installation");
    const auto shared = sandbox.path/"shared";
    mkdirPrivate(shared);
    require(::chmod(shared.c_str(), 0777) == 0, "Cannot create shared source sandbox");
    const auto patchRoot = shared/"patch";
    mkdirPrivate(patchRoot); mkdirPrivate(patchRoot/"_data"); mkdirPrivate(patchRoot/"_data/nested");
    writeFile(patchRoot/"K22Widescreen.wrs", "root asset");
    writeFile(patchRoot/"K22.ini", "configuration");
    writeFile(patchRoot/"K22.exe", "font source only");
    writeFile(patchRoot/"K22_4GB.exe", "excluded Windows executable");
    writeFile(patchRoot/"readme.txt", "excluded readme");
    writeFile(patchRoot/"_data/nested/asset.dat", "preserve source");
    require(fails([&] { chain(patchRoot); }), "Destination guard must reject a shared writable ancestor");
    PatchSnapshot patch(patchRoot);
    require(patch.files.size() == 3 && patch.font && patch.font->relative == "K22.exe",
        "Patch snapshot must select only native assets and pin the font source separately");
    for (const auto& file : patch.files)
        patch.copy(file, sandbox.path/file.relative.filename(), {});
    patch.copy(*patch.font, sandbox.path/"private-font-copy", {});
    patch.verify();
    require(smallFile(sandbox.path/"asset.dat") == "preserve source" &&
        smallFile(sandbox.path/"private-font-copy") == "font source only",
        "Read-only shared source snapshot copy failed");
    require(!fs::exists(sandbox.path/"K22_4GB.exe") && !fs::exists(sandbox.path/"readme.txt"),
        "Excluded patch components were installed");
    const auto nestedFile = std::find_if(patch.files.begin(), patch.files.end(),
        [](const auto& file) { return file.relative == "_data/nested/asset.dat"; });
    require(nestedFile != patch.files.end(), "Missing nested snapshot asset");
    renameNew(patchRoot/"_data/nested", sandbox.path/"patch-outside");
    fs::create_directory_symlink(sandbox.path/"patch-outside", patchRoot/"_data/nested");
    require(fails([&] { patch.copy(*nestedFile, sandbox.path/"escaped-copy", {}); }) &&
        fails([&] { patch.verify(); }), "Patch must reject a replaced intermediate directory");
    require(!fs::exists(sandbox.path/"escaped-copy") &&
        smallFile(sandbox.path/"patch-outside/asset.dat") == "preserve source",
        "Patch copy followed a source replacement symlink");
    const auto replacementRoot = shared/"replacement";
    mkdirPrivate(replacementRoot);
    writeFile(replacementRoot/"K22.ini", "original configuration");
    PatchSnapshot replaced(replacementRoot);
    renameNew(replacementRoot/"K22.ini", replacementRoot/"saved.ini");
    writeFile(replacementRoot/"K22.ini", "replacement configuration");
    require(fails([&] { replaced.copy(replaced.files.front(), sandbox.path/"replaced-copy", {}); }),
        "Patch must reject a replaced source file");
    require(smallFile(replacementRoot/"saved.ini") == "original configuration" &&
        smallFile(replacementRoot/"K22.ini") == "replacement configuration",
        "Patch file replacement guard modified source bytes");
    PatchSnapshot mutated(replacementRoot);
    const auto mutableFile = std::find_if(mutated.files.begin(), mutated.files.end(),
        [](const auto& file) { return file.relative == "K22.ini"; });
    require(mutableFile != mutated.files.end(), "Missing mutable snapshot asset");
    {
        Fd changed(::open((replacementRoot/"K22.ini").c_str(), O_WRONLY | O_NOFOLLOW | O_CLOEXEC));
        require(::pwrite(changed.value, "X", 1, mutableFile->info.st_size) == 1, "Cannot mutate disposable source file");
    }
    require(fails([&] { mutated.copy(*mutableFile, sandbox.path/"mutated-copy", {}); }),
        "Patch must reject source content mutation");
    PatchSnapshot moved(replacementRoot);
    renameNew(replacementRoot, shared/"moved");
    mkdirPrivate(replacementRoot);
    require(fails([&] { moved.verify(); }), "Patch must reject named root replacement");
    writeFile(replacementRoot/"K22.ini", "copy original");
    PatchSnapshot inFlight(replacementRoot);
    bool changedWhileCopying = false;
    require(fails([&] {
        inFlight.copy(inFlight.files.front(), sandbox.path/"in-flight-copy",
            [&](const auto&, auto, auto) {
                if (!changedWhileCopying) {
                    Fd changed(::open((replacementRoot/"K22.ini").c_str(), O_WRONLY | O_NOFOLLOW | O_CLOEXEC));
                    require(::pwrite(changed.value, "X", 1, inFlight.files.front().info.st_size) == 1,
                        "Cannot mutate disposable source during copy");
                    changedWhileCopying = true;
                }
                return true;
            });
    }) && changedWhileCopying, "Patch must reject source mutation during streaming");
    require(smallFile(replacementRoot/"K22.ini") == "copy originalX",
        "Patch mutation guard modified source bytes");
    fs::create_symlink(shared/"moved/K22.ini", replacementRoot/"excluded.dll");
    require(fails([&] { PatchSnapshot invalid(replacementRoot); }), "Excluded patch symlinks must fail");
    require(::unlink((replacementRoot/"excluded.dll").c_str()) == 0, "Cannot remove disposable symlink");
    fs::create_hard_link(replacementRoot/"K22.ini", replacementRoot/"readme.txt");
    require(fails([&] { PatchSnapshot invalid(replacementRoot); }), "Excluded patch hardlinks must fail");
    require(::unlink((replacementRoot/"readme.txt").c_str()) == 0, "Cannot remove disposable hardlink");
    writeFile(replacementRoot/"Readme-\xc3\x89.txt", "excluded composed name");
    writeFile(replacementRoot/"readme-E\xcc\x81.txt", "excluded decomposed name");
    require(fails([&] { PatchSnapshot invalid(replacementRoot); }),
        "Excluded patch names must reject Unicode case-fold/normalization conflicts");
    media::selfCheck();
    k22::pe_font_detail::selfCheck();
}
}
