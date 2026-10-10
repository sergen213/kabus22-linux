#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Signed GitHub-release updates (Contract B). Network I/O is HTTPS-only with TLS
// verification; the test-only build (K22_UPDATER_TEST_HTTP) also admits http://127.0.0.1.
namespace k22::update {
namespace fs = std::filesystem;
using Progress = std::function<void(int percent, const std::string& message)>;
struct Settings { std::string repository = "sergen213/kabus22-linux", channel = "stable"; };
// ${XDG_CONFIG_HOME:-~/.config}/kabus22/updates.ini; invalid values warn and keep defaults.
Settings load_settings();
bool valid_repository(std::string_view repository);
// Semantic-version precedence: <0, 0, >0. Both must satisfy layout::valid_version.
int compare_versions(std::string_view a, std::string_view b);
struct Asset { std::string name, url; std::uint64_t size = 0; };
struct Release { std::string version, notes, published; bool prerelease = false; std::vector<Asset> assets; };
// GitHub /releases JSON; drafts and tags that are not "[v]SEMVER" are skipped.
std::vector<Release> parse_releases(std::string_view json);
// Highest release in channel (stable excludes prereleases), or the exact version; null if none.
const Release* select(const std::vector<Release>& releases, std::string_view channel, std::string_view version = {});
struct Manifest {
    std::string version, channel, minGlibc, payloadName, sha256;
    std::uint64_t size = 0;
    std::vector<std::string> components;
};
// Verifies the Ed25519 signature over the exact bytes before parsing; throws on any defect.
Manifest verify_manifest(std::string_view json, std::string_view signatureBase64, std::string_view publicKeyBase64);
// Throws unless the running glibc is at least minimum ("MAJOR.MINOR").
void require_glibc(std::string_view minimum);
// Extracts a bin/lib/share tar.gz into an empty stage, refusing absolute/../escaping
// paths and links, hard links, devices and setuid/setgid/sticky bits; then writes
// stage/.kabus22-payload.json and requires the four Kabus22 executables (engine, launcher,
// setup for the launcher's Installation page, updater).
void extract_payload(const fs::path& archive, const fs::path& stage, std::string_view version);
std::string sha256_file(const fs::path& path);
// HTTP(S) GET into memory, bounded by limit bytes.
std::string fetch(const std::string& url, std::size_t limit);
// GET into a new file; size must equal expected; returns lowercase hex SHA-256.
std::string download(const std::string& url, const fs::path& target, std::uint64_t expected, const Progress& progress);
// GitHub API base ("https://api.github.com"; test build: $KABUS22_UPDATER_TEST_API).
std::string api_base();
// Build-time K22_UPDATE_PUBLIC_KEY (test build: $KABUS22_UPDATER_TEST_PUBLIC_KEY).
std::string public_key();
}
