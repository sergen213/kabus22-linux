// Asset-free regressions for k22::layout and k22::update (no network).
#include "layout.hpp"
#include "update.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <openssl/evp.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace {
namespace fs = std::filesystem;
using namespace k22;
int failures = 0;
void check(bool good, const std::string& what) {
    if (!good) { ++failures; std::cerr << "FAIL: " << what << '\n'; }
}
template <class F> void throws(F&& body, const std::string& what) {
    try { body(); check(false, what + " (did not throw)"); } catch (const std::exception&) {}
}
void put(const fs::path& path, const std::string& data, mode_t mode = 0644) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << data;
    ::chmod(path.c_str(), mode);
}
std::string get(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream out; out << in.rdbuf(); return out.str();
}
// Everything in root except layout-owned entries, with content: must never change.
std::map<std::string, std::string> foreign(const fs::path& root) {
    std::map<std::string, std::string> result;
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
        const auto relative = it->path().lexically_relative(root);
        const auto top = relative.begin()->string();
        if (top == "bin" || top == "lib" || top == "share" || top == "current" || top == "versions") { if (it.depth() == 0) it.disable_recursion_pending(); continue; }
        result[relative.string()] = it->is_regular_file() ? get(it->path()) : "<dir>";
    }
    return result;
}
fs::path stage(const fs::path& root, const std::string& version, const std::string& marker) {
    const auto staged = layout::stage_dir(root, version);
    put(staged/"bin/kabus22", marker, 0755);
    put(staged/"lib/libx.so.1", marker);
    put(staged/"share/licenses/n.txt", marker);
    return staged;
}
void layoutChecks(const fs::path& base) {
    const auto root = base/"install";
    put(root/".kabus22-owned", "Kabus22 native installation 1\n");
    put(root/"bin/kabus22", "old engine", 0755);
    put(root/"bin/kabus22-launcher", "old launcher", 0755);
    put(root/"lib/libold.so.1", "old lib");
    put(root/"share/licenses/kabus22/n.txt", "notice");
    put(root/"game/K22.wrs", "original asset");
    put(root/"kayit1.sav", "save slot");
    put(root/"options.k22", "options");
    put(root/"kabus22.png", "icon");
    const auto untouched = foreign(root);
    throws([&] { layout::commit(root, layout::stage_dir(root, "0.3.0"), "0.3.0"); }, "commit on flat layout");
    for (const auto& entry : fs::directory_iterator(root/"versions")) fs::remove_all(entry.path());
    check(layout::active_version(root).empty(), "flat root has no active version");
    // Crash after moving bin only: migration resumes.
    fs::create_directories(root/"versions/0.2.0");
    fs::rename(root/"bin", root/"versions/0.2.0/bin");
    layout::migrate_flat(root, "0.2.0");
    check(layout::active_version(root) == "0.2.0", "migrated active version");
    check(fs::read_symlink(root/"current") == "versions/0.2.0" && fs::read_symlink(root/"bin") == "current/bin", "relative current/compat links");
    check(get(root/"bin/kabus22") == "old engine" && get(root/"lib/libold.so.1") == "old lib", "payload reachable through compat links");
    layout::migrate_flat(root, "0.2.0");
    check(layout::active_version(root) == "0.2.0" && layout::installed_versions(root) == std::vector<std::string>{"0.2.0"}, "migration is idempotent");
    // Crash after the current swap, before compat links and .state.
    fs::remove(root/"lib"); fs::remove(root/"versions/.state");
    layout::migrate_flat(root, "0.2.0");
    check(fs::read_symlink(root/"lib") == "current/lib", "resumed compat link");
    check(layout::find_root(root/"versions/0.2.0/bin/kabus22") == root, "find_root from versioned executable");
    check(layout::find_root(root/"bin/kabus22") == root, "find_root through compat link");
    check(layout::find_root(base/"elsewhere/bin/x").empty(), "find_root without marker");
    // Commit, retention and rollback.
    layout::commit(root, stage(root, "0.2.1", "engine 0.2.1"), "0.2.1");
    check(layout::active_version(root) == "0.2.1" && layout::previous_version(root) == "0.2.0", "commit 0.2.1");
    check(get(root/"bin/kabus22") == "engine 0.2.1", "compat link follows current");
    fs::create_directories(root/"versions/.stage-0.9.9-crashed");
    put(root/"versions/.stage-0.9.9-crashed.tar.gz", "partial");
    ::symlink("versions/0.2.0", (root/"versions/.tmp-current-1").c_str());
    layout::commit(root, stage(root, "0.3.0-beta.1", "engine beta"), "0.3.0-beta.1");
    check(layout::installed_versions(root) == std::vector<std::string>{"0.2.1", "0.3.0-beta.1"}, "retains only active+previous");
    check(!fs::exists(fs::symlink_status(root/"versions/.stage-0.9.9-crashed")) && !fs::exists(fs::symlink_status(root/"versions/.tmp-current-1")), "crash leftovers pruned");
    throws([&] { layout::commit(root, stage(root, "0.3.0-beta.1", "dup"), "0.3.0-beta.1"); }, "commit of active version");
    throws([&] { layout::commit(root, base, "0.4.0"); }, "commit of foreign directory");
    layout::rollback(root);
    check(layout::active_version(root) == "0.2.1" && layout::previous_version(root) == "0.3.0-beta.1", "rollback");
    check(get(root/"bin/kabus22") == "engine 0.2.1", "rollback payload");
    layout::rollback(root);
    check(layout::active_version(root) == "0.3.0-beta.1", "rollback toggles");
    // Crash after current swap but before .state: previous is the recorded active.
    std::ofstream(root/"versions/.state") << "active=0.2.1\nprevious=0.2.0\n";
    check(layout::previous_version(root) == "0.2.1", "stale .state after swap");
    {
        layout::Lock lock(root);
        throws([&] { layout::Lock second(root); }, "concurrent lock");
    }
    layout::Lock again(root);
    check(foreign(root) == untouched, "saves, options, game and icon untouched");
    throws([&] { layout::migrate_flat(base/"nomarker", "0.2.0"); }, "unowned root");
    throws([&] { layout::stage_dir(root, "../x"); }, "invalid version");
}
void versionChecks() {
    const char* order[] = {"0.9.0", "0.10.0", "1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                           "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0", "1.0.1"};
    for (std::size_t i = 0; i + 1 < std::size(order); ++i)
        check(update::compare_versions(order[i], order[i + 1]) < 0 && update::compare_versions(order[i + 1], order[i]) > 0, std::string("semver ") + order[i]);
    check(update::compare_versions("1.0.0+build.1", "1.0.0") == 0, "build metadata ignored");
    throws([] { update::compare_versions("1.0", "1.0.0"); }, "invalid semver");
    const auto releases = update::parse_releases(R"([
        {"tag_name":"v0.4.0","draft":true,"prerelease":false,"assets":[]},
        {"tag_name":"v0.3.0-beta.1","draft":false,"prerelease":true,"body":"beta","published_at":"2026-10-02T00:00:00Z","assets":[]},
        {"tag_name":"nightly","draft":false,"prerelease":true,"assets":[]},
        {"tag_name":"v0.2.1","draft":false,"prerelease":false,"body":"notes","published_at":"2026-10-01T00:00:00Z",
         "assets":[{"name":"kabus22-update.json","browser_download_url":"https://example.invalid/m","size":10}]},
        {"tag_name":"0.2.0","draft":false,"prerelease":false,"body":null,"assets":[]}])");
    check(releases.size() == 3, "drafts and non-semver tags skipped");
    const auto* stable = update::select(releases, "stable");
    check(stable && stable->version == "0.2.1" && stable->notes == "notes" && stable->assets.size() == 1 && stable->assets[0].size == 10, "stable channel");
    const auto* experimental = update::select(releases, "experimental");
    check(experimental && experimental->version == "0.3.0-beta.1", "experimental channel");
    check(!update::select(releases, "stable", "0.3.0-beta.1"), "stable excludes prerelease by version");
    check(update::select(releases, "stable", "0.2.0") != nullptr, "exact version");
    throws([] { update::parse_releases("{\"message\":\"rate limited\"}"); }, "non-array API response");
    check(update::valid_repository("sergen213/kabus22-linux") && !update::valid_repository("a/../b") && !update::valid_repository("a/b/c"), "repository validation");
}
std::string b64(const std::string& data) {
    std::string out(4 * ((data.size() + 2) / 3) + 1, '\0');
    out.resize(static_cast<std::size_t>(EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()), reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size()))));
    return out;
}
struct Key {
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey{nullptr, EVP_PKEY_free};
    Key() { pkey.reset(EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519")); }
    std::string publicKey() const { std::string raw(32, '\0'); std::size_t size = 32; EVP_PKEY_get_raw_public_key(pkey.get(), reinterpret_cast<unsigned char*>(raw.data()), &size); return b64(raw); }
    std::string sign(const std::string& data) const {
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        std::string signature(64, '\0'); std::size_t size = 64;
        EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, pkey.get());
        EVP_DigestSign(context.get(), reinterpret_cast<unsigned char*>(signature.data()), &size, reinterpret_cast<const unsigned char*>(data.data()), data.size());
        return b64(signature) + "\n";
    }
};
void signatureChecks() {
    const Key key, other;
    const std::string manifest = R"({"format":1,"version":"0.2.1","channel":"stable","min_glibc":"2.38","payload":{"name":"kabus22-0.2.1-linux-x86_64.tar.gz","sha256":")"
        + std::string(64, 'a') + R"(","size":123},"components":["engine","launcher"]})";
    const auto signature = key.sign(manifest);
    const auto parsed = update::verify_manifest(manifest, signature, key.publicKey());
    check(parsed.version == "0.2.1" && parsed.size == 123 && parsed.sha256 == std::string(64, 'a') && parsed.components.size() == 2, "valid manifest accepted");
    auto tampered = manifest; tampered[tampered.find("2.38") + 3] = '9';
    throws([&] { update::verify_manifest(tampered, signature, key.publicKey()); }, "tampered manifest");
    auto hash = manifest; hash.replace(hash.find(std::string(64, 'a')), 64, std::string(64, 'b'));
    throws([&] { update::verify_manifest(hash, signature, key.publicKey()); }, "tampered payload hash");
    throws([&] { update::verify_manifest(manifest, signature, other.publicKey()); }, "wrong key");
    throws([&] { update::verify_manifest(manifest, b64(std::string(63, 'x')), key.publicKey()); }, "short signature");
    auto badName = manifest; badName.replace(badName.find("0.2.1-linux"), 5, "0.2.2");
    throws([&] { update::verify_manifest(badName, key.sign(badName), key.publicKey()); }, "signed but inconsistent payload name");
    check(update::public_key().size() == 44, "embedded public key present");
    update::require_glibc("2.17");
    throws([] { update::require_glibc("99.0"); }, "glibc minimum");
}
struct Entry { std::string name; mode_t type; mode_t mode = 0644; std::string data, link; bool hard = false; };
void archiveOf(const fs::path& path, const std::vector<Entry>& entries) {
    std::unique_ptr<archive, decltype(&archive_write_free)> out(archive_write_new(), archive_write_free);
    archive_write_add_filter_gzip(out.get());
    archive_write_set_format_pax_restricted(out.get());
    archive_write_open_filename(out.get(), path.c_str());
    for (const auto& item : entries) {
        std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(), archive_entry_free);
        archive_entry_set_pathname(entry.get(), item.name.c_str());
        archive_entry_set_filetype(entry.get(), item.type);
        archive_entry_set_perm(entry.get(), item.mode);
        if (item.hard) archive_entry_set_hardlink(entry.get(), item.link.c_str());
        else if (!item.link.empty()) archive_entry_set_symlink(entry.get(), item.link.c_str());
        archive_entry_set_size(entry.get(), item.type == AE_IFREG && !item.hard ? static_cast<la_int64_t>(item.data.size()) : 0);
        archive_write_header(out.get(), entry.get());
        if (!item.data.empty()) archive_write_data(out.get(), item.data.data(), item.data.size());
    }
    archive_write_close(out.get());
}
void archiveChecks(const fs::path& base) {
    const std::vector<Entry> valid = {{"bin", AE_IFDIR, 0755}, {"bin/kabus22", AE_IFREG, 0755, "engine"},
        {"./bin/kabus22-launcher", AE_IFREG, 0755, "launcher"}, {"bin/kabus22-setup", AE_IFREG, 0755, "setup"},
        {"bin/kabus22-updater", AE_IFREG, 0755, "updater"},
        {"lib/libx.so.1", AE_IFREG, 0644, "lib"}, {"lib/libx.so", AE_IFLNK, 0777, "", "libx.so.1"}, {"share/n.txt", AE_IFREG, 0644, "n"}};
    fs::create_directories(base/"archives");
    int index = 0;
    const auto run = [&](const std::vector<Entry>& entries) {
        const auto archivePath = base/"archives"/(std::to_string(++index) + ".tar.gz");
        const auto stageDir = base/"archives"/("stage" + std::to_string(index));
        archiveOf(archivePath, entries);
        fs::create_directory(stageDir);
        update::extract_payload(archivePath, stageDir, "0.2.1");
        return stageDir;
    };
    const auto good = run(valid);
    check(get(good/"bin/kabus22") == "engine" && fs::read_symlink(good/"lib/libx.so") == "libx.so.1", "valid payload extracted");
    const auto payload = get(good/".kabus22-payload.json");
    check(payload.find("\"path\":\"bin/kabus22\"") != std::string::npos && payload.find("\"version\":\"0.2.1\"") != std::string::npos &&
          payload.find(update::sha256_file(good/"bin/kabus22")) != std::string::npos, "payload inventory written");
    const std::vector<std::vector<Entry>> hostile = {
        {{"../evil", AE_IFREG, 0644, "x"}}, {{"/tmp/k22-evil", AE_IFREG, 0644, "x"}}, {{"bin/../../evil", AE_IFREG, 0644, "x"}},
        {{"game/K22.wrs", AE_IFREG, 0644, "x"}}, {{"kayit1.sav", AE_IFREG, 0644, "x"}},
        {{"lib/escape", AE_IFLNK, 0777, "", "../../etc"}}, {{"lib/abs", AE_IFLNK, 0777, "", "/etc/passwd"}},
        {{"lib/d", AE_IFLNK, 0777, "", "../share"}, {"lib/d/x", AE_IFREG, 0644, "x"}},
        {{"bin/kabus22", AE_IFREG, 0755, "e"}, {"bin/hard", AE_IFREG, 0644, "", "bin/kabus22", true}},
        {{"bin/dev", AE_IFCHR, 0644}}, {{"bin/fifo", AE_IFIFO, 0644}}, {{"bin/suid", AE_IFREG, 04755, "x"}},
        {{"bin/kabus22", AE_IFREG, 0755, "only engine"}}};
    // Each hostile entry follows an otherwise valid payload, so only it can cause rejection.
    for (std::size_t i = 0; i < hostile.size(); ++i) {
        auto entries = i + 1 == hostile.size() ? std::vector<Entry>{} : valid;
        if (i == 8) entries.erase(entries.begin() + 1);
        entries.insert(entries.end(), hostile[i].begin(), hostile[i].end());
        std::string why;
        try { run(entries); } catch (const std::exception& error) { why = error.what(); }
        std::cout << "hostile archive " << i << " rejected: " << why << '\n';
        check(!why.empty() && why.find("lacks executable") == std::string::npos ? true : i + 1 == hostile.size() && !why.empty(), "hostile archive " + std::to_string(i) + ": " + why);
    }
    check(!fs::exists(base/"evil") && !fs::exists("/tmp/k22-evil") && !fs::exists(base/"archives/share/x"), "nothing written outside stage");
}
}

int main() {
    char pattern[] = "/tmp/k22-update-check-XXXXXX";
    const fs::path base = ::mkdtemp(pattern);
    try { layoutChecks(base); versionChecks(); signatureChecks(); archiveChecks(base); }
    catch (const std::exception& error) { std::cerr << "FAIL: unexpected exception: " << error.what() << '\n'; ++failures; }
    fs::remove_all(base);
    if (failures) { std::cerr << failures << " updater check(s) failed\n"; return 1; }
    std::cout << "Updater layout/version/signature/archive checks passed\n";
}
