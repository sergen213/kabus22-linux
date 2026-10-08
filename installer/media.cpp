#include "media.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <glib.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string_view>
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
namespace k22::setup::media {
namespace {
constexpr std::uint64_t memberLimit = 4ULL * 1024 * 1024 * 1024;
constexpr std::uint64_t totalLimit = 32ULL * 1024 * 1024 * 1024;
constexpr std::size_t entryLimit = 32768, listingLimit = 4 * 1024 * 1024;
void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
void report(const Progress& progress, const std::string& stage,
            std::uint64_t done = 0, std::uint64_t total = 0) {
    if (progress && !progress(stage, done, total)) throw Cancelled();
}
struct Fd {
    int value = -1;
    explicit Fd(int fd) : value(fd) {}
    ~Fd() { if (value >= 0) ::close(value); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};
std::string fold(std::string_view value) {
    require(g_utf8_validate(value.data(), static_cast<gssize>(value.size()), nullptr), "Invalid UTF-8 media path");
    std::unique_ptr<gchar, decltype(&g_free)> folded(g_utf8_casefold(value.data(), static_cast<gssize>(value.size())), g_free);
    std::unique_ptr<gchar, decltype(&g_free)> normalized(g_utf8_normalize(folded.get(), -1, G_NORMALIZE_ALL_COMPOSE), g_free);
    require(normalized != nullptr, "Cannot normalize media path");
    return normalized.get();
}
void memberPath(std::string_view value) {
    require(!value.empty() && value.size() <= 1024 && value.front() != '/' && value.back() != '/', "Invalid media member path");
    require(g_utf8_validate(value.data(), static_cast<gssize>(value.size()), nullptr), "Invalid UTF-8 media member path");
    for (const auto ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        require(c >= 32 && c != 127 && std::string_view("\\:\"*?<>|").find(ch) == std::string_view::npos,
                "Unsafe character in media member path");
    }
    for (std::size_t begin = 0; begin < value.size();) {
        auto end = value.find('/', begin);
        if (end == std::string_view::npos) end = value.size();
        const auto part = value.substr(begin, end - begin);
        require(!part.empty() && part != "." && part != ".." && part.size() <= 255 &&
                part.front() != ' ' && part.back() != ' ' && part.back() != '.', "Unsafe media member component");
        begin = end + 1;
    }
    for (const gchar* p = value.data(); p < value.data() + value.size(); p = g_utf8_next_char(p))
        require(!g_unichar_iscntrl(g_utf8_get_char(p)), "Control character in media member path");
}
struct Paths {
    struct Node { std::string exact; bool directory; bool explicitEntry; };
    std::map<std::string, Node> seen;
    void add(const std::string& path, bool directory) {
        memberPath(path);
        std::size_t end = 0;
        do {
            end = path.find('/', end);
            const bool leaf = end == std::string::npos;
            const auto prefix = leaf ? path : path.substr(0, end);
            const bool dir = !leaf || directory;
            const auto key = fold(prefix);
            auto [it, inserted] = seen.emplace(key, Node{prefix, dir, leaf});
            if (!inserted) {
                require(it->second.exact == prefix && it->second.directory == dir,
                        "Case-fold or file/directory conflict: " + path);
                if (leaf) {
                    require(!it->second.explicitEntry, "Duplicate media member: " + path);
                    it->second.explicitEntry = true;
                }
            }
            if (leaf) break;
            ++end;
        } while (true);
    }
};
fs::path checkedPath(const fs::path& input) {
    require(!input.empty(), "Empty source path");
    const auto text = input.string();
    for (unsigned char c : text) require(c >= 32 && c != 127, "Control character in source path");
    require(g_utf8_validate(text.data(), static_cast<gssize>(text.size()), nullptr), "Invalid UTF-8 source path");
    auto path = fs::absolute(input);
    for (const auto& part : path) require(part != "..", "Parent traversal in source path");
    path = path.lexically_normal();
    fs::path current = path.root_path();
    for (const auto& part : path.relative_path()) {
        current /= part;
        struct stat st{};
        require(::lstat(current.c_str(), &st) == 0 && !S_ISLNK(st.st_mode), "Missing or symlink source path: " + current.string());
        if (current != path) require(S_ISDIR(st.st_mode), "Non-directory source ancestor");
    }
    return path;
}
struct stat regular(const fs::path& path, std::uint64_t limit = memberLimit) {
    struct stat st{};
    require(::lstat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_nlink == 1 && st.st_size >= 0 &&
            static_cast<std::uint64_t>(st.st_size) <= limit, "Source must be a bounded, non-linked regular file: " + path.string());
    return st;
}
void unchanged(const fs::path& path, const struct stat& before) {
    const auto after = regular(path, totalLimit);
    require(before.st_dev == after.st_dev && before.st_ino == after.st_ino && before.st_size == after.st_size &&
            before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec &&
            before.st_ctim.tv_sec == after.st_ctim.tv_sec && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec,
            "Source changed while reading: " + path.string());
}
void privateDirectory(const fs::path& input, bool empty) {
    const auto path = checkedPath(input);
    struct stat st{};
    require(::lstat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == geteuid() &&
            !(st.st_mode & (S_IRWXG | S_IRWXO)), "Media staging directory must be private and user-owned");
    if (empty) require(fs::is_empty(path), "Refusing nonempty media output directory");
}
void nativeHelper(const fs::path& helper) {
    const auto st = regular(helper);
    require(!(st.st_mode & (S_IWGRP | S_IWOTH)) && ::access(helper.c_str(), X_OK) == 0,
            "Extraction helper must be a protected executable ELF");
    Fd file(::open(helper.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    require(file.value >= 0, "Cannot open native extraction helper");
    std::array<unsigned char, 20> header{};
    require(::pread(file.value, header.data(), header.size(), 0) == static_cast<ssize_t>(header.size()) &&
            header[0] == 0x7f && header[1] == 'E' && header[2] == 'L' && header[3] == 'F' &&
            header[4] == 2 && header[5] == 1 && header[6] == 1 &&
            (header[16] == 2 || header[16] == 3) && header[17] == 0, "Extraction helper is not a native ELF executable");
#if defined(__x86_64__)
    require(header[18] == 62 && header[19] == 0, "Extraction helper CPU architecture mismatch");
#elif defined(__aarch64__)
    require(header[18] == 183 && header[19] == 0, "Extraction helper CPU architecture mismatch");
#else
#error Native installer helper validation needs the target ELF architecture
#endif
}
struct Child {
    pid_t pid = -1;
    ~Child() {
        if (pid > 0) {
            ::kill(-pid, SIGKILL);
            ::kill(pid, SIGKILL);
            while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
        }
    }
};
std::string run(const fs::path& extractor, std::vector<std::string> options,
                const Progress& progress, const std::string& stage, std::size_t maximum = listingLimit) {
    const auto helper = checkedPath(extractor);
    nativeHelper(helper);
    report(progress, stage);
    std::vector<std::string> args{helper.string(), "--no-extract-unknown", "--color=0", "--progress=0"};
    for (auto& option : options) args.push_back(std::move(option));
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    std::vector<std::string> environment;
    for (char** e = environ; e && *e; ++e) {
        const std::string_view value(*e);
        if (!value.starts_with("LC_ALL=") && !value.starts_with("LANG=")) environment.emplace_back(value);
    }
    environment.emplace_back("LC_ALL=C");
    environment.emplace_back("LANG=C");
    std::vector<char*> env;
    for (auto& e : environment) env.push_back(e.data());
    env.push_back(nullptr);
    int out[2], err[2];
    require(::pipe2(out, O_CLOEXEC) == 0, "Cannot create helper output pipe");
    Fd outRead(out[0]), outWrite(out[1]);
    require(::pipe2(err, O_CLOEXEC) == 0, "Cannot create helper error pipe");
    Fd errRead(err[0]), errWrite(err[1]);
    require(::fcntl(outRead.value, F_SETFL, O_NONBLOCK) == 0 && ::fcntl(errRead.value, F_SETFL, O_NONBLOCK) == 0,
            "Cannot configure helper output pipes");
    posix_spawn_file_actions_t actions;
    require(::posix_spawn_file_actions_init(&actions) == 0, "Cannot initialize helper spawn");
    struct Actions { posix_spawn_file_actions_t* p; ~Actions() { posix_spawn_file_actions_destroy(p); } } actionsGuard{&actions};
    posix_spawnattr_t attr;
    require(::posix_spawnattr_init(&attr) == 0, "Cannot initialize helper attributes");
    struct Attr { posix_spawnattr_t* p; ~Attr() { posix_spawnattr_destroy(p); } } attrGuard{&attr};
    require(::posix_spawn_file_actions_adddup2(&actions, outWrite.value, STDOUT_FILENO) == 0 &&
            ::posix_spawn_file_actions_adddup2(&actions, errWrite.value, STDERR_FILENO) == 0 &&
            ::posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0) == 0 &&
            ::posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP) == 0 && ::posix_spawnattr_setpgroup(&attr, 0) == 0,
            "Cannot configure native helper spawn");
    Child child;
    const auto result = ::posix_spawn(&child.pid, helper.c_str(), &actions, &attr, argv.data(), env.data());
    require(result == 0, "Cannot start native extraction helper: " + std::string(std::strerror(result)));
    ::close(outWrite.value); outWrite.value = -1;
    ::close(errWrite.value); errWrite.value = -1;
    std::string output, errors;
    bool outOpen = true, errOpen = true, exited = false;
    int status = 0;
    std::array<char, 16384> buffer{};
    while (!exited || outOpen || errOpen) {
        report(progress, stage);
        pollfd pipes[2]{{outOpen ? outRead.value : -1, POLLIN, 0}, {errOpen ? errRead.value : -1, POLLIN, 0}};
        const auto ready = ::poll(pipes, 2, 100);
        require(ready >= 0 || errno == EINTR, "Cannot poll native extraction helper");
        auto drain = [&](int fd, bool& open, std::string& text, std::size_t cap) {
            if (!open) return;
            for (;;) {
                const auto n = ::read(fd, buffer.data(), buffer.size());
                if (n < 0 && errno == EINTR) continue;
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                require(n >= 0, "Cannot read extraction helper output");
                if (n == 0) { open = false; break; }
                require(static_cast<std::size_t>(n) <= cap - text.size(), "Extraction helper output exceeded safety limit");
                text.append(buffer.data(), static_cast<std::size_t>(n));
            }
        };
        drain(outRead.value, outOpen, output, maximum);
        drain(errRead.value, errOpen, errors, 65536);
        if (!exited) {
            const auto waited = ::waitpid(child.pid, &status, WNOHANG);
            require(waited >= 0 || errno == EINTR, "Cannot reap extraction helper");
            if (waited == child.pid) { exited = true; child.pid = -1; }
        }
    }
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "Native media helper failed: " + errors.substr(0, 4096));
    require(errors.empty(), "Native media helper reported unsafe or unsupported media: " + errors.substr(0, 4096));
    return output;
}
std::uint64_t decimal(std::string_view text) {
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    require(!text.empty() && result.ec == std::errc{} && result.ptr == text.data() + text.size(), "Invalid media byte count");
    return value;
}
void validateMembers(const std::vector<Member>& members) {
    require(!members.empty() && members.size() <= entryLimit, "Missing or excessive Inno members");
    Paths paths;
    std::uint64_t total = 0;
    for (const auto& member : members) {
        paths.add(member.path.generic_string(), false);
        require(member.size <= memberLimit && member.size <= totalLimit - total, "Inno member size limit exceeded");
        total += member.size;
        require(member.checksum.size() == 32 && std::all_of(member.checksum.begin(), member.checksum.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }), "Missing or invalid MD5 checksum");
    }
}
std::vector<Member> parseList(const std::string& text) {
    require(text.size() <= listingLimit && text.find('\0') == std::string::npos, "Invalid Inno listing");
    std::vector<Member> result;
    for (std::size_t begin = 0; begin < text.size();) {
        auto end = text.find('\n', begin);
        if (end == std::string::npos) end = text.size();
        const std::string_view line(text.data() + begin, end - begin);
        require(!line.empty(), "Unexpected blank Inno listing entry");
        const auto separator = line.find(' ');
        require(separator != std::string_view::npos && line.substr(separator, 5) == " MD5 ", "Unsupported Inno listing entry");
        const auto hash = separator + 5;
        require(line.size() > hash + 33 && line[hash + 32] == ' ', "Malformed Inno checksum listing");
        memberPath(line.substr(hash + 33));
        result.push_back({fs::path(std::string(line.substr(hash + 33))), decimal(line.substr(0, separator)), std::string(line.substr(hash, 32))});
        require(result.size() <= entryLimit, "Excessive Inno listing entries");
        begin = end + 1;
    }
    validateMembers(result);
    return result;
}
void validateVerbosePaths(const std::string& text) {
    Paths paths;
    std::set<std::string> exact;
    std::size_t count = 0;
    for (std::size_t begin = 0; begin < text.size();) {
        auto end = text.find('\n', begin);
        if (end == std::string::npos) end = text.size();
        const std::string_view line(text.data() + begin, end - begin);
        if (line.starts_with(" - \"")) {
            const auto quote = line.rfind('"');
            require(quote != std::string_view::npos && quote >= 4, "Malformed complete Inno path listing");
            std::string path(line.substr(4, quote - 4));
            const bool directory = !path.empty() && path.back() == '/';
            if (directory) path.pop_back();
            memberPath(path);
            require(++count <= entryLimit, "Excessive complete Inno path entries");
            if (!exact.insert(path).second) {
                // Original multilingual ISO overwrites an excluded Windows EXE.
                require(!directory && !selected({path, 0, {}}), "Duplicate selected Inno asset: " + path);
            } else {
                paths.add(path, directory);
            }
        }
        begin = end + 1;
    }
    require(count > 0, "Missing complete Inno path inventory");
}
struct IsoMember { fs::path path; std::uint64_t size; bool directory; };
using Archive = std::unique_ptr<struct archive, decltype(&archive_read_free)>;
Archive isoArchive(int fd) {
    require(::lseek(fd, 0, SEEK_SET) == 0, "Cannot rewind ISO source");
    Archive archive(archive_read_new(), archive_read_free);
    require(archive != nullptr && archive_read_support_filter_none(archive.get()) == ARCHIVE_OK &&
            archive_read_support_format_iso9660(archive.get()) == ARCHIVE_OK &&
            archive_read_open_fd(archive.get(), fd, 128 * 1024) == ARCHIVE_OK, "Cannot open ISO9660 source");
    return archive;
}
IsoMember isoMember(struct archive_entry* entry) {
    require(archive_entry_symlink(entry) == nullptr && archive_entry_hardlink(entry) == nullptr &&
            (archive_entry_filetype(entry) == AE_IFREG || archive_entry_filetype(entry) == AE_IFDIR) &&
            archive_entry_size_is_set(entry) && archive_entry_size(entry) >= 0,
            "ISO contains a linked, special, or unsized member");
    const char* raw = archive_entry_pathname_utf8(entry);
    if (!raw) raw = archive_entry_pathname(entry);
    require(raw != nullptr, "ISO member has no path");
    std::string path(raw);
    const bool directory = archive_entry_filetype(entry) == AE_IFDIR;
    if (directory && path.size() > 1 && path.back() == '/') path.pop_back();
    // ISO9660's format-defined root entry is not an extractable member.
    if (!(directory && path == ".")) memberPath(path);
    const auto size = static_cast<std::uint64_t>(archive_entry_size(entry));
    require(size <= memberLimit, "ISO member size limit exceeded");
    return {path, size, directory};
}
unsigned sliceNumber(const fs::path& setup, const fs::path& candidate) {
    if (setup.parent_path() != candidate.parent_path()) return 0;
    const auto stem = fold(setup.stem().string());
    const auto name = fold(candidate.filename().string());
    const auto prefix = stem + '-';
    if (!name.starts_with(prefix) || !name.ends_with(".bin")) return 0;
    const auto digits = std::string_view(name).substr(prefix.size(), name.size() - prefix.size() - 4);
    if (digits.empty() || digits.front() == '0' || !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) return 0;
    const auto n = decimal(digits);
    require(n <= 128, "Excessive Inno source slices");
    return static_cast<unsigned>(n);
}
std::vector<IsoMember> sourcePair(const std::vector<IsoMember>& all, const fs::path& explicitSetup = {}) {
    std::vector<std::vector<IsoMember>> candidates;
    for (const auto& item : all) {
        if (item.directory || fold(item.path.extension().string()) != ".exe" ||
                (!explicitSetup.empty() && item.path != explicitSetup)) continue;
        std::map<unsigned, IsoMember> slices;
        for (const auto& other : all) if (!other.directory) {
            const auto number = sliceNumber(item.path, other.path);
            if (number) require(slices.emplace(number, other).second, "Duplicate Inno source slice");
        }
        if (slices.empty() || slices.begin()->first != 1) continue;
        std::vector<IsoMember> pair{item};
        unsigned expected = 1;
        for (const auto& [number, slice] : slices) {
            require(number == expected++, "Missing Inno source slice");
            pair.push_back(slice);
        }
        candidates.push_back(std::move(pair));
    }
    require(candidates.size() == 1, "Media must contain one unambiguous installer and contiguous sibling .bin slices");
    return std::move(candidates.front());
}
std::map<fs::path, struct stat> sourceSnapshot(const Prepared& source, const Progress& progress) {
    const auto setup = checkedPath(source.setup);
    std::vector<IsoMember> all;
    std::map<fs::path, struct stat> result;
    Paths paths;
    std::uint64_t bytes = 0;
    for (const auto& entry : fs::directory_iterator(setup.parent_path())) {
        report(progress, "Validating original source slices");
        if (entry.path() != setup && !sliceNumber(setup, entry.path())) continue;
        const auto path = checkedPath(entry.path());
        paths.add(path.filename().string(), false);
        const auto identity = regular(path);
        const auto size = static_cast<std::uint64_t>(identity.st_size);
        require(size > 0 && size <= totalLimit - bytes, "Invalid source slice size");
        bytes += size;
        all.push_back({path, size, false});
        result.emplace(path, identity);
    }
    sourcePair(all, setup);
    require(bytes == source.sourceBytes, "Original media slice sizes changed since preparation");
    return result;
}
void checkSourceHeader(const fs::path& setup) {
    Fd file(::open(setup.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    std::array<char, 2> magic{};
    require(file.value >= 0 && ::pread(file.value, magic.data(), magic.size(), 0) == 2 && magic[0] == 'M' && magic[1] == 'Z',
            "Source installer is not an original Windows setup file");
}
void writeAll(int fd, const char* bytes, std::size_t size) {
    while (size) {
        const auto n = ::write(fd, bytes, size);
        if (n < 0 && errno == EINTR) continue;
        require(n > 0, "Cannot write staged media");
        bytes += n; size -= static_cast<std::size_t>(n);
    }
}
} // namespace

Prepared prepare(const fs::path& sourceIso, const fs::path& sourceSetup,
                 const fs::path& scratch, const Progress& progress) {
    require(sourceIso.empty() != sourceSetup.empty(), "Choose exactly one ISO or loose setup source");
    privateDirectory(scratch, false);
    if (!sourceSetup.empty()) {
        const auto setup = checkedPath(sourceSetup);
        regular(setup);
        std::vector<IsoMember> all;
        Paths paths;
        for (const auto& entry : fs::directory_iterator(setup.parent_path())) {
            report(progress, "Inspecting original setup slices");
            const auto name = entry.path().filename().string();
            if (entry.path() != setup && !sliceNumber(setup, entry.path())) continue;
            paths.add(name, false);
            const auto path = checkedPath(entry.path());
            const auto st = regular(path);
            all.push_back({path, static_cast<std::uint64_t>(st.st_size), false});
        }
        const auto pair = sourcePair(all, setup);
        std::uint64_t bytes = 0;
        for (const auto& item : pair) {
            require(item.size > 0 && item.size <= totalLimit - bytes, "Invalid loose source size");
            bytes += item.size;
        }
        checkSourceHeader(setup);
        return {setup, bytes};
    }
    const auto iso = checkedPath(sourceIso);
    const auto identity = regular(iso, totalLimit);
    Fd source(::open(iso.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    require(source.value >= 0, "Cannot open original ISO");
    struct stat opened{};
    require(::fstat(source.value, &opened) == 0 && opened.st_dev == identity.st_dev && opened.st_ino == identity.st_ino,
            "ISO source changed while opening");
    std::vector<IsoMember> all;
    Paths paths;
    std::uint64_t total = 0;
    bool root = false;
    {
        auto archive = isoArchive(source.value);
        struct archive_entry* entry = nullptr;
        int status;
        while ((status = archive_read_next_header(archive.get(), &entry)) == ARCHIVE_OK) {
            report(progress, "Inspecting ISO9660 media", all.size(), 0);
            require((archive_format(archive.get()) & ARCHIVE_FORMAT_BASE_MASK) == ARCHIVE_FORMAT_ISO9660,
                    "Only ISO9660 media is supported");
            const auto item = isoMember(entry);
            if (item.directory && item.path == ".") {
                require(!root, "Duplicate ISO root entry"); root = true;
            } else {
                paths.add(item.path.generic_string(), item.directory);
                if (!item.directory) {
                    require(item.size <= totalLimit - total, "ISO aggregate size limit exceeded"); total += item.size;
                }
                all.push_back(item);
                require(all.size() <= entryLimit, "Excessive ISO members");
            }
            require(archive_read_data_skip(archive.get()) == ARCHIVE_OK, "Cannot skip ISO member data");
        }
        require(status == ARCHIVE_EOF, "Truncated or invalid ISO directory");
    }
    const auto pair = sourcePair(all);
    std::uint64_t bytes = 0;
    for (const auto& item : pair) {
        require(item.size > 0 && item.size <= totalLimit - bytes, "Invalid ISO setup slice size"); bytes += item.size;
    }
    require(fs::space(scratch).available >= bytes + 64 * 1024 * 1024, "Insufficient disk space for ISO setup slices");
    const auto staging = checkedPath(scratch) / "media";
    require(::mkdir(staging.c_str(), 0700) == 0, "Refusing existing ISO media staging directory");
    std::map<std::string, const IsoMember*> wanted;
    for (const auto& item : pair) wanted.emplace(item.path.generic_string(), &item);
    std::set<std::string> copied;
    std::uint64_t done = 0;
    auto archive = isoArchive(source.value);
    std::array<char, 256 * 1024> buffer{};
    struct archive_entry* entry = nullptr;
    int status;
    std::size_t index = 0;
    bool secondRoot = false;
    while ((status = archive_read_next_header(archive.get(), &entry)) == ARCHIVE_OK) {
        report(progress, "Staging original ISO setup slices", done, bytes);
        const auto item = isoMember(entry);
        if (item.directory && item.path == ".") {
            require(root && !secondRoot, "ISO root changed between reads"); secondRoot = true;
        } else {
            require(index < all.size() && item.path == all[index].path && item.size == all[index].size &&
                    item.directory == all[index].directory, "ISO directory changed between reads"); ++index;
        }
        const auto match = wanted.find(item.path.generic_string());
        if (match == wanted.end()) {
            require(archive_read_data_skip(archive.get()) == ARCHIVE_OK, "Cannot skip ISO member data"); continue;
        }
        require(!item.directory && copied.insert(match->first).second, "Duplicate staged ISO member");
        const auto output = staging / item.path.filename();
        Fd target(::open(output.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        require(target.value >= 0, "Cannot stage original setup slice");
        std::uint64_t count = 0;
        for (;;) {
            report(progress, "Staging original ISO setup slices", done, bytes);
            const auto n = archive_read_data(archive.get(), buffer.data(), buffer.size());
            require(n >= 0, "Cannot read ISO setup slice");
            if (!n) break;
            require(static_cast<std::uint64_t>(n) <= item.size - count, "ISO member exceeds declared size");
            writeAll(target.value, buffer.data(), static_cast<std::size_t>(n));
            count += static_cast<std::uint64_t>(n); done += static_cast<std::uint64_t>(n);
        }
        require(count == item.size, "Truncated ISO setup slice");
    }
    require(status == ARCHIVE_EOF && index == all.size() && copied.size() == pair.size() && secondRoot == root,
            "Incomplete ISO staging");
    unchanged(iso, identity);
    const auto setup = staging / pair.front().path.filename();
    checkSourceHeader(setup);
    report(progress, "Staging original ISO setup slices", bytes, bytes);
    return {setup, bytes};
}

std::vector<Member> list(const Prepared& source, const fs::path& extractor, const Progress& progress) {
    const auto setup = checkedPath(source.setup);
    const auto before = sourceSnapshot(source, progress);
    checkSourceHeader(setup);
    const auto version = run(extractor, {"--data-version", "--silent", setup.string()}, progress, "Inspecting Inno data version", 1024);
    auto value = std::string_view(version);
    if (value.ends_with('\n')) value.remove_suffix(1);
    require(!value.empty() && value.size() <= 64 && std::count(value.begin(), value.end(), '.') == 2 &&
            std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || c == '.'; }),
            "Unsupported or unknown Inno data version");
    // --no-extract-unknown is the helper's version gate, including original 5.1.10.
    validateVerbosePaths(run(extractor, {"--list", setup.string()}, progress, "Validating all original media paths"));
    auto members = parseList(run(extractor, {"--list", "--list-sizes", "--list-checksums", "--silent", setup.string()},
                                 progress, "Listing original game media"));
    for (const auto& [path, identity] : before) unchanged(path, identity);
    return members;
}

bool selected(const Member& member) {
    const auto path = member.path.generic_string();
    memberPath(path);
    const auto lower = fold(path);
    const auto extension = fold(member.path.extension().string());
    if (lower.starts_with("app/") && (extension == ".k22" || extension == ".wrs" || extension == ".dat")) return true;
    if (lower.starts_with("app/help/")) return extension == ".jpg" || extension == ".html";
    if (lower.starts_with("app/gallery/")) return extension == ".jpg";
    if (lower.starts_with("app/menu_sound/") || lower.starts_with("app/kabus 22 mode/menu_ses/")) return extension == ".ogg";
    return false;
}

namespace {
bool fontSource(const Member& member) {
    return member.path.generic_string() == "app/K22.exe" && member.size > 0 &&
           member.size <= 64ULL * 1024 * 1024;
}
void extractChecked(const Prepared& source, const fs::path& extractor,
                    const std::vector<Member>& members, const fs::path& output,
                    const Progress& progress) {
    validateMembers(members);
    privateDirectory(output, true);
    const auto destination = checkedPath(output);
    const auto setup = checkedPath(source.setup);
    const auto before = sourceSnapshot(source, progress);
    std::map<std::string, const Member*> expected;
    std::set<std::string> directories;
    std::uint64_t total = 0;
    std::vector<std::string> args{"--test", "--extract", "--collisions=error", "--silent", "--output-dir", destination.string()};
    for (const auto& member : members) {
        expected.emplace(member.path.generic_string(), &member);
        for (auto dir = member.path.parent_path(); !dir.empty(); dir = dir.parent_path()) directories.insert(dir.generic_string());
        total += member.size;
        args.emplace_back("--include"); args.push_back('/' + member.path.generic_string());
    }
    require(fs::space(destination).available >= total + 64 * 1024 * 1024, "Insufficient space for selected game assets");
    args.push_back(setup.string());
    run(extractor, std::move(args), progress, "Extracting and testing source members", 65536);
    for (const auto& [path, identity] : before) unchanged(path, identity);
    std::set<std::string> found;
    std::uint64_t verified = 0;
    std::array<guchar, 256 * 1024> buffer{};
    for (const auto& entry : fs::recursive_directory_iterator(destination)) {
        report(progress, "Verifying extracted source members", verified, total);
        const auto relative = entry.path().lexically_relative(destination).generic_string();
        memberPath(relative);
        struct stat st{};
        require(::lstat(entry.path().c_str(), &st) == 0 && !S_ISLNK(st.st_mode), "Linked or missing extracted member");
        if (S_ISDIR(st.st_mode)) {
            require(directories.contains(relative), "Unexpected extracted directory: " + relative); continue;
        }
        const auto match = expected.find(relative);
        require(match != expected.end() && found.insert(relative).second && S_ISREG(st.st_mode) && st.st_nlink == 1 &&
                st.st_size >= 0 && static_cast<std::uint64_t>(st.st_size) == match->second->size,
                "Unexpected, linked, special, or wrong-size extracted file: " + relative);
        Fd file(::open(entry.path().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
        struct stat opened{};
        require(file.value >= 0 && ::fstat(file.value, &opened) == 0 && opened.st_dev == st.st_dev && opened.st_ino == st.st_ino,
                "Extracted file changed while opening");
        std::unique_ptr<GChecksum, decltype(&g_checksum_free)> checksum(g_checksum_new(G_CHECKSUM_MD5), g_checksum_free);
        require(checksum != nullptr, "Cannot initialize media checksum");
        std::uint64_t count = 0;
        for (;;) {
            report(progress, "Verifying extracted source members", verified, total);
            const auto n = ::read(file.value, buffer.data(), buffer.size());
            if (n < 0 && errno == EINTR) continue;
            require(n >= 0, "Cannot read extracted game asset");
            if (!n) break;
            require(static_cast<std::uint64_t>(n) <= match->second->size - count, "Extracted file grew during verification");
            g_checksum_update(checksum.get(), buffer.data(), n);
            count += static_cast<std::uint64_t>(n); verified += static_cast<std::uint64_t>(n);
        }
        require(count == match->second->size && match->second->checksum == g_checksum_get_string(checksum.get()),
                "Extracted asset checksum mismatch: " + relative);
        unchanged(entry.path(), st);
        require(::chmod(entry.path().c_str(), 0600) == 0, "Cannot protect extracted asset");
    }
    require(found.size() == expected.size(), "Native media extraction omitted selected members");
    report(progress, "Verifying extracted source members", total, total);
}
} // namespace

void extract(const Prepared& source, const fs::path& extractor,
             const std::vector<Member>& members, const fs::path& output,
             const Progress& progress) {
    for (const auto& member : members)
        require(selected(member), "Refusing non-native asset extraction: " + member.path.string());
    extractChecked(source, extractor, members, output, progress);
}

void extractFontSource(const Prepared& source, const fs::path& extractor,
                       const Member& member, const fs::path& privateOutput,
                       const Progress& progress) {
    require(fontSource(member), "Only the bounded original app/K22.exe may be read for font recovery");
    const auto inventory = list(source, extractor, progress);
    const auto original = std::find_if(inventory.begin(), inventory.end(), [&](const Member& entry) {
        return entry.path == member.path && entry.size == member.size && entry.checksum == member.checksum;
    });
    require(original != inventory.end(), "Font-source member does not match the original media listing");
    extractChecked(source, extractor, {member}, privateOutput, progress);
}

void selfCheck() {
    auto rejected = [](auto operation) {
        bool failed = false;
        try { operation(); } catch (const std::runtime_error&) { failed = true; }
        require(failed, "Media security boundary regression");
    };
    require(fontSource({"app/K22.exe", 2347008, {}}) && !selected({"app/K22.exe", 2347008, {}}),
            "Temporary font source must remain excluded from native assets");
    require(!fontSource({"app/Kabus22.exe", 1244972, {}}) && !fontSource({"app/k22.exe", 2347008, {}}) &&
            !fontSource({"app/K22.exe", 0, {}}) && !fontSource({"app/K22.exe", 64ULL * 1024 * 1024 + 1, {}}),
            "Temporary font-source allowlist regression");
    for (const auto* path : {"", "/app/file", "../file", "app/../file", "app//file", "app/./file", "app\\file", "C:/file", "app/a\nfile", "app/file.", "app/ file"})
        rejected([&] { memberPath(path); });
    memberPath("app/Help/Help_Files/fon_inzar enerji.jpg");
    rejected([] { Paths paths; paths.add("app/FILE.k22", false); paths.add("app/file.k22", false); });
    rejected([] { Paths paths; paths.add("app/File/a.k22", false); paths.add("app/file/b.k22", false); });
    rejected([] { Paths paths; paths.add("app/a", false); paths.add("app/a/b", false); });
    rejected([] { Paths paths; paths.add("app/a", false); paths.add("app/a", false); });
    Paths paths; paths.add("app/a/b", false); paths.add("app/a", true);
    const auto parsed = parseList("976276272 MD5 653c42ebd759d9908243dbdaea217eed app/Kaynak.k22\n");
    require(parsed.size() == 1 && parsed.front().size == 976276272 && selected(parsed.front()), "Large original member regression");
    rejected([] { parseList("1 MD5 00000000000000000000000000000000 ../bad\n"); });
    rejected([] { parseList("18446744073709551616 MD5 00000000000000000000000000000000 app/a.k22\n"); });
    rejected([] { parseList("1 MD5 00000000000000000000000000000000 app/a.k22\n1 MD5 00000000000000000000000000000000 app/A.k22\n"); });
    rejected([] { parseList("1 MD5 00000000000000000000000000000000 app//bad.k22\n"); });
    validateVerbosePaths("Listing media\n - \"app/Kabus22.exe\" - overwritten\n - \"app/Kabus22.exe\"\n - \"app/yazi_de.k22\"\nDone.\n");
    rejected([] { validateVerbosePaths(" - \"app/data.k22\" - overwritten\n - \"app/data.k22\"\n"); });
    rejected([] { validateVerbosePaths(" - \"app/A.exe\" - overwritten\n - \"app/a.exe\"\n"); });
    for (const auto* path : {"app/K22.exe", "app/a.dll", "app/kayit0.sav", "app/a.url", "Drivers/a.exe", "app/Help/a.exe", "app/Kabus 22 Mode/acknex.wdf"})
        require(!selected({path, 0, {}}), "Unsafe native asset allowlist regression");
    for (const auto* path : {"app/K22.wrs", "app/yoldata.DAT", "app/Help/Help_TR.html", "app/Gallery/a.JPG", "app/Menu_sound/menu_fon.ogg", "app/Kabus 22 Mode/K22_mode.wrs", "app/Kabus 22 Mode/Menu_ses/menu_fon.ogg"})
        require(selected({path, 0, {}}), "Original asset allowlist regression");
    const std::vector<IsoMember> iso{{"JPN-K22.exe", 344953, false}, {"JPN-K22-1.bin", 729654912, false},
                                    {"JPN-K22-2.bin", 730000000, false}, {"JPN-K22-3.bin", 565412946, false},
                                    {"DirectX/DXSETUP.exe", 503144, false}};
    require(sourcePair(iso).size() == 4, "Actual ISO slice layout regression");
    rejected([] { sourcePair({{"setup.exe", 1, false}, {"setup-1.bin", 1, false}, {"setup-3.bin", 1, false}}); });
}
} // namespace k22::setup::media
