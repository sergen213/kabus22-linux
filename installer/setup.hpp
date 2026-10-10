#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace k22::setup {
namespace fs = std::filesystem;
using Progress = std::function<bool(const std::string& stage, std::uint64_t done, std::uint64_t total)>;
struct Request {
    // source is auto-detected (ISO, original setup.exe + setup-1.bin, or installed Windows
    // game directory); sourceIso/sourceSetup are the explicit legacy routes. Exactly one is set.
    fs::path source, sourceIso, sourceSetup, installRoot, applicationsDir, desktopDir;
    fs::path enginePayload, launcherPayload, updaterPayload, extractor, optionalPatch;
};
struct Result {
    fs::path root, dataRoot, executable, shortcut, desktopShortcut;
};
struct Cancelled : std::runtime_error { Cancelled() : std::runtime_error("Installation cancelled") {} };
enum class SourceKind { Iso, Setup, Directory };
struct Source {
    SourceKind kind;
    fs::path path;       // ISO file, setup.exe, or Windows game directory
    std::string edition; // Directory only (media editions are known after listing)
};
const char* kindName(SourceKind kind);
// Validates by content: ISO9660 magic, MZ setup with sibling <stem>-1.bin, or a directory
// holding K22.wrs + K22.exe (installed Windows game) or exactly one setup.exe pair.
Source detectSource(const fs::path& path);
struct Verification {
    std::uint64_t checked = 0;
    std::vector<std::string> missing, modified; // root-relative paths
    bool ok() const { return missing.empty() && modified.empty(); }
};
// Read-only check of root/.kabus22-inventory.json (game + root files) and the active
// version's versions/<v>/.kabus22-payload.json.
Verification verify(const fs::path& root, const Progress& progress = {});
// Restores missing/modified inventory-listed game/ files from source (staged, hash-checked,
// atomically renamed per file). Never touches saves, options, settings or versions/.
// Returns the post-repair verification.
Verification repair(const fs::path& root, const fs::path& source, const fs::path& extractor,
                    const Progress& progress = {});
Request defaultPaths();
// source is an ISO, original setup.exe or Windows game directory. Scratch must not exist;
// caller owns its removal after the returned images are consumed (also on failure).
// wizard/wizardSmall are the original Inno wizard images as PNG, empty when unavailable
// (installed Windows folders carry no setup).
struct Artwork { fs::path logo, wizard, wizardSmall; };
Artwork previewArtwork(const fs::path& source, const fs::path& extractor,
                       const fs::path& scratch, const Progress& progress = {});
Result install(const Request& request, const Progress& progress = {});
// Converts a staged flat installation (bin/lib/share + game/) into the versioned layout
// (versions/K22_VERSION + current + compat links) and writes the ownership marker and both
// inventories. Part of install(); exposed for asset-free tests.
struct Provenance { fs::path root, source; std::string kind, edition; std::vector<fs::path> shortcuts; };
void finalizeInstallation(const fs::path& installation, const Provenance& provenance, const Progress& progress = {});
// Commit step of install(), exposed for asset-free tests. Publishes a finalized staged installation at
// root and each staged desktop entry (staged file, target). A missing root receives the whole tree; an
// empty folder or earlier Kabus22 installation has its installation-owned top-level entries atomically
// exchanged, so saves, options and launcher state stored in the same folder stay untouched. Any other
// non-empty folder, or a directory at a shortcut target, is refused before changes. Existing shortcut
// files/symlinks are replaced. Failures roll everything back; on success the staged paths are removed.
void publish(const fs::path& installation, const fs::path& root,
             const std::vector<std::pair<fs::path, fs::path>>& shortcuts, const Progress& progress = {});
// Returns the settings launcher PID; no shell, Wine, or Windows execution.
int launch(const Result& result);
// Security regression checks; no media extraction or user-directory writes.
void selfCheck();
}
