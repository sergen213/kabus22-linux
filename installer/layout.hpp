#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Versioned installation layout (Contract A). Mutations only ever create, move,
// replace or delete root/{bin,lib,share,current} and entries inside root/versions/;
// saves, options, game/ and every other root entry are never touched.
// Every mutating call requires root/.kabus22-owned. Callers serialize mutations by
// holding a Lock (not taken internally: flock is per open file description).
namespace k22::layout {
namespace fs = std::filesystem;
// Walks up to 4 parents of the executable's directory for .kabus22-owned; empty if none.
fs::path find_root(const fs::path& realExecutable);
// "MAJOR.MINOR.PATCH[-pre][+build]", at most 64 characters.
bool valid_version(std::string_view version);
// Target of root/current ("versions/<v>"); empty for legacy flat or unversioned roots.
std::string active_version(const fs::path& root);
// Rollback target; empty when none is installed.
std::string previous_version(const fs::path& root);
std::vector<std::string> installed_versions(const fs::path& root);
// Version directory the calling process executes from; empty otherwise.
std::string running_version(const fs::path& root);
// New private (0700) directory inside root/versions; fill it with bin/lib/share.
fs::path stage_dir(const fs::path& root, std::string_view version);
// Publishes staged as versions/<version>, swaps current atomically, records .state
// (previous = old active) and prunes all versions but active, previous and running.
void commit(const fs::path& root, const fs::path& staged, std::string_view version);
// Swaps current back to the previous version; active becomes the new previous.
void rollback(const fs::path& root);
// Moves legacy flat root/{bin,lib,share} into versions/<version> plus compat symlinks.
// Idempotent and resumable after a crash at any step; no-op once versioned.
void migrate_flat(const fs::path& root, std::string_view version);
struct Lock {
    explicit Lock(const fs::path& root); // throws if another process holds it
    ~Lock();
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    int fd;
};
}
