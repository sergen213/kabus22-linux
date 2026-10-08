#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace k22::pe_font_detail {
using Bytes = std::span<const std::uint8_t>;
constexpr std::size_t decodedSize = 192 * 72 * 3;
// Observed identically in the user's original ISO app/K22.exe, patched K22.exe,
// and Ghidra's patched 4GB PE, at raw offset 0x22130c:
// length 17063, SHA256 a16eb595d782967bca34d2c8360c147b5ea1c78780d1a0a58e3a9245d3215c41.
// Fingerprint metadata only; no proprietary image bytes are bundled.
constexpr std::size_t fontLength = 17063;
constexpr std::uint32_t fontCrc = 0xf5b48715;
constexpr std::uint32_t decodedFontCrc = 0x339585ef;

[[noreturn]] inline void fail(std::string_view reason) {
    throw std::runtime_error("embedded engine font: " + std::string(reason));
}
inline void bounds(std::uint64_t offset, std::uint64_t length, std::uint64_t size) {
    if (offset > size || length > size - offset) fail("truncated or out-of-bounds PE/PCX data");
}
inline std::uint16_t u16(Bytes b, std::size_t p) {
    bounds(p, 2, b.size());
    return std::uint16_t(b[p]) | (std::uint16_t(b[p + 1]) << 8);
}
inline std::uint32_t u32(Bytes b, std::size_t p) {
    bounds(p, 4, b.size());
    return std::uint32_t(b[p]) | (std::uint32_t(b[p + 1]) << 8)
         | (std::uint32_t(b[p + 2]) << 16) | (std::uint32_t(b[p + 3]) << 24);
}
inline constexpr auto crcTable = [] {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t n = 0; n < table.size(); ++n) {
        auto v = n;
        for (unsigned bit = 0; bit < 8; ++bit) v = (v >> 1) ^ ((v & 1) ? 0xedb88320u : 0u);
        table[n] = v;
    }
    return table;
}();
inline std::uint32_t crcByte(std::uint32_t crc, std::uint8_t value) {
    return crcTable[(crc ^ value) & 255] ^ (crc >> 8);
}
inline std::uint32_t crc32(Bytes b) {
    std::uint32_t crc = 0xffffffff;
    for (auto v : b) crc = crcByte(crc, v);
    return ~crc;
}
struct Rle { std::size_t consumed; std::uint32_t crc; };
inline Rle decodeRle(Bytes b, std::size_t outputSize) {
    std::size_t p = 0, decoded = 0;
    std::uint32_t crc = 0xffffffff;
    while (decoded < outputSize) {
        if (p == b.size()) fail("truncated PCX RLE");
        auto value = b[p++];
        std::size_t count = 1;
        if ((value & 0xc0) == 0xc0) {
            count = value & 63;
            if (!count || p == b.size()) fail("invalid or truncated PCX RLE run");
            value = b[p++];
        }
        if (count > outputSize - decoded) fail("PCX RLE run exceeds image size");
        // Real A6 _a4font runs cross both plane and scanline boundaries.
        decoded += count;
        for (std::size_t i = 0; i < count; ++i) crc = crcByte(crc, value);
    }
    return {p, ~crc};
}
inline bool fontHeader(Bytes b) {
    return b.size() >= 128 && b[0] == 10 && b[1] == 5 && b[2] == 1 && b[3] == 8
        && u16(b, 4) == 0 && u16(b, 6) == 0 && u16(b, 8) == 191 && u16(b, 10) == 71
        && b[64] == 0 && b[65] == 3 && u16(b, 66) == 192 && u16(b, 68) == 1;
}
inline void validateFont(Bytes pcx) {
    if (pcx.size() != fontLength) fail("truncated or wrong-length A6 _a4font PCX");
    if (!fontHeader(pcx)) fail("invalid RGB24 192x72 A6 _a4font PCX header");
    const auto decoded = decodeRle(pcx.subspan(128), decodedSize);
    if (decoded.consumed != pcx.size() - 128) fail("unexpected trailing A6 _a4font PCX data");
    if (crc32(pcx) != fontCrc || decoded.crc != decodedFontCrc)
        fail("PCX does not match the genuine A6 _a4font fingerprint");
}
struct Range { std::size_t offset, size; std::uint32_t virtualOffset, virtualSize; };
struct Sections { std::array<Range, 96> ranges{}; std::size_t count{}; };
inline Sections peSections(Bytes pe) {
    if (pe.size() < 64 || pe.size() > std::numeric_limits<std::uint32_t>::max()
        || u16(pe, 0) != 0x5a4d) fail("missing DOS PE header or invalid PE32 file size");
    const std::size_t nt = u32(pe, 60);
    if (nt < 64) fail("invalid NT header offset");
    bounds(nt, 24, pe.size());
    if (u32(pe, nt) != 0x00004550 || u16(pe, nt + 4) != 0x14c
        || !(u16(pe, nt + 22) & 2)) fail("expected executable x86 PE32");
    const auto count = u16(pe, nt + 6), optionalSize = u16(pe, nt + 20);
    if (!count || count > 96 || optionalSize < 96) fail("invalid PE section/optional header size");
    const auto optional = nt + 24;
    const auto tableSize = std::uint64_t(optionalSize) + std::uint64_t(count) * 40;
    bounds(optional, tableSize, pe.size());
    if (u16(pe, optional) != 0x10b) fail("expected PE32 optional header");
    const auto imageSize = u32(pe, optional + 56), headerSize = u32(pe, optional + 60);
    const auto directories = u32(pe, optional + 92);
    if (!imageSize || headerSize > pe.size() || optional > headerSize
        || tableSize > headerSize - optional || directories > 16
        || std::uint64_t(directories) * 8 > optionalSize - 96u)
        fail("invalid PE image/header dimensions");
    Sections result;
    std::array<Range, 96> all{};
    for (std::size_t i = 0; i < count; ++i) {
        auto h = pe.subspan(optional + optionalSize + i * 40, 40);
        Range r{u32(h, 20), u32(h, 16), u32(h, 12), u32(h, 8)};
        bounds(r.offset, r.size, pe.size());
        if (r.size && r.offset < headerSize) fail("PE section overlaps headers");
        const auto virtualSize = std::max<std::uint64_t>(r.virtualSize, r.size);
        bounds(r.virtualOffset, virtualSize, imageSize);
        for (std::size_t j = 0; j < i; ++j) {
            const auto& a = all[j];
            if (r.size && a.size && r.offset < a.offset + a.size && a.offset < r.offset + r.size)
                fail("overlapping PE raw sections");
            const auto av = std::max<std::uint64_t>(a.virtualSize, a.size);
            if (virtualSize && av && r.virtualOffset < std::uint64_t(a.virtualOffset) + av
                && a.virtualOffset < std::uint64_t(r.virtualOffset) + virtualSize)
                fail("overlapping PE virtual sections");
        }
        all[i] = r;
        // Never scan code, BSS, overlay/certificates, or raw section alignment padding.
        if (!(u32(h, 36) & 0x40) || !r.size) continue;
        if (r.virtualSize) r.size = std::min<std::size_t>(r.size, r.virtualSize);
        result.ranges[result.count++] = r;
    }
    return result;
}

// Pure parser checks; no game-font fixtures, filesystem access, or PE execution.
inline void selfCheck() {
    auto require = [](bool value) { if (!value) fail("parser self-check failed"); };
    auto rejects = [&](auto action) {
        bool rejected = false;
        try { action(); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected);
    };
    const std::array<std::uint8_t, 4> rle{0xc2, 7, 8, 99};
    auto decoded = decodeRle(rle, 3);
    const std::array<std::uint8_t, 3> raw{7, 7, 8};
    require(decoded.consumed == 3 && decoded.crc == crc32(raw));
    rejects([&] { decodeRle(Bytes(rle).first(1), 2); });
    rejects([&] { decodeRle(Bytes(rle).first(2), 3); });
    rejects([&] { decodeRle(rle, 1); });
    const std::array<std::uint8_t, 2> zeroRun{0xc0, 0};
    rejects([&] { decodeRle(zeroRun, 1); });
    const std::array<std::uint8_t, 9> check{'1','2','3','4','5','6','7','8','9'};
    require(crc32(check) == 0xcbf43926);
    rejects([&] { u32(raw, 0); });
    rejects([&] { bounds(std::numeric_limits<std::uint64_t>::max(), 1, 16); });
    rejects([&] { bounds(15, std::numeric_limits<std::uint64_t>::max(), 16); });
    rejects([&] { peSections(raw); });
    std::array<std::uint8_t, 240> pe{};
    auto put16 = [&](std::size_t p, std::uint16_t v) {
        pe[p] = static_cast<std::uint8_t>(v); pe[p + 1] = static_cast<std::uint8_t>(v >> 8);
    };
    auto put32 = [&](std::size_t p, std::uint32_t v) {
        put16(p, static_cast<std::uint16_t>(v)); put16(p + 2, static_cast<std::uint16_t>(v >> 16));
    };
    put16(0, 0x5a4d); put32(60, 64); put32(64, 0x4550);
    put16(68, 0x14c); put16(70, 1); put16(84, 96); put16(86, 2);
    put16(88, 0x10b); put32(144, 4096); put32(148, 224);
    put32(192, 16); put32(196, 512); put32(200, 16); put32(204, 224); put32(220, 0x40);
    auto layout = peSections(pe);
    require(layout.count == 1 && layout.ranges[0].offset == 224 && layout.ranges[0].size == 16);
    rejects([&] { peSections(Bytes(pe).first(223)); });
    put32(200, 17); rejects([&] { peSections(pe); }); put32(200, 16);
    put32(204, 223); rejects([&] { peSections(pe); }); put32(204, 224);
    put16(88, 0x20b); rejects([&] { peSections(pe); });
    require(!fontHeader(Bytes(pe).first(127)));
    rejects([&] { validateFont({}); });
    rejects([&] { validateFont(raw); });
    std::array<std::uint8_t, 128> pcxHeader{};
    pcxHeader[0] = 10; pcxHeader[1] = 5; pcxHeader[2] = 1; pcxHeader[3] = 8;
    pcxHeader[8] = 191; pcxHeader[10] = 71; pcxHeader[65] = 3;
    pcxHeader[66] = 192; pcxHeader[68] = 1;
    require(fontHeader(pcxHeader));
    rejects([&] { validateFont(pcxHeader); });
    pcxHeader[65] = 1;
    require(!fontHeader(pcxHeader));
}
} // namespace k22::pe_font_detail

namespace k22 {
// Validates the exact installed PCX using the same proof as PE extraction.
inline void validate_engine_font(std::span<const std::uint8_t> pcx) {
    pe_font_detail::validateFont(pcx);
}

// The caller owns source safety/lifetime: use a stable read-only regular PE,
// reject symlinks/special files, and verify identity/size/ctime/mtime before and
// after reading or mapping. Never execute PE code. The returned vector contains
// only the exact original encoded PCX bytes, not any trailing section data.
inline std::vector<std::uint8_t> extract_engine_font(std::span<const std::uint8_t> pe) {
    using namespace pe_font_detail;
    const auto sections = peSections(pe);
    Bytes found;
    std::string invalidReason;
    for (std::size_t n = 0; n < sections.count; ++n) {
        const auto& range = sections.ranges[n];
        auto data = pe.subspan(range.offset, range.size);
        for (std::size_t p = 0; p <= data.size() && data.size() - p >= 128; ++p) {
            if (!fontHeader(data.subspan(p, 128))) continue;
            auto bytes = data.subspan(p, std::min(fontLength, data.size() - p));
            try { validate_engine_font(bytes); }
            catch (const std::runtime_error& error) {
                if (invalidReason.empty()) invalidReason = error.what();
                continue;
            }
            if (!found.empty()) fail("ambiguous _a4font: multiple genuine embedded candidates");
            found = bytes;
        }
    }
    if (found.empty()) {
        if (!invalidReason.empty()) throw std::runtime_error(invalidReason);
        fail("genuine embedded A6 _a4font PCX is missing");
    }
    return {found.begin(), found.end()};
}
} // namespace k22
