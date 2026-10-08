#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>

namespace k22::setup {
namespace fs = std::filesystem;
using Progress = std::function<bool(const std::string& stage, std::uint64_t done, std::uint64_t total)>;
struct Request {
    fs::path sourceIso, sourceSetup, installRoot, applicationsDir, desktopDir;
    fs::path enginePayload, extractor, optionalPatch;
};
struct Result {
    fs::path root, dataRoot, executable, shortcut, desktopShortcut;
};
struct Cancelled : std::runtime_error { Cancelled() : std::runtime_error("Installation cancelled") {} };
Request defaultPaths();
// source is an actual ISO or the original loose setup.exe. Scratch must not exist;
// caller owns its removal after the returned image is consumed (also on failure).
fs::path previewArtwork(const fs::path& source, const fs::path& extractor,
                        const fs::path& scratch, const Progress& progress = {});
Result install(const Request& request, const Progress& progress = {});
// Returns the native child PID; no shell, Wine, or Windows execution.
int launch(const Result& result);
// Security regression checks; no media extraction or user-directory writes.
void selfCheck();
}
