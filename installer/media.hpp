#pragma once
#include "setup.hpp"
#include <vector>

namespace k22::setup::media {
struct Prepared { fs::path setup; std::uint64_t sourceBytes; };
struct Member { fs::path path; std::uint64_t size; std::string checksum; };
Prepared prepare(const fs::path& sourceIso, const fs::path& sourceSetup,
                 const fs::path& scratch, const Progress& progress = {});
std::vector<Member> list(const Prepared& source, const fs::path& extractor,
                         const Progress& progress = {});
bool selected(const Member& member);
void extract(const Prepared& source, const fs::path& extractor,
             const std::vector<Member>& members, const fs::path& output,
             const Progress& progress = {});
// Extracts only the original app/K22.exe into private temporary output for
// resource recovery. Caller must remove the PE/output immediately afterward.
void extractFontSource(const Prepared& source, const fs::path& extractor,
                       const Member& member, const fs::path& privateOutput,
                       const Progress& progress = {});
void selfCheck();
}
