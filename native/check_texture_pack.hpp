#pragma once
#include "texture_pack.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace k22 {
// Asset-free Contract E checks; throws std::runtime_error on failure.
inline void texture_pack_checks() {
    namespace fs = std::filesystem;
    auto require = [](bool ok, const char* what) { if (!ok) throw std::runtime_error(std::string("texture pack check: ") + what); };
    auto image = [](std::uint32_t w, std::uint32_t h, std::uint8_t seed) {
        Image result{w, h, std::vector<std::uint8_t>(std::size_t(w) * h * 4)};
        for (std::size_t i = 0; i < result.rgba.size(); ++i) result.rgba[i] = std::uint8_t(seed + i * 37);
        return result;
    };

    // FNV-1a-64 over LE width, LE height, RGBA bytes.
    require(texture_key(Image{1, 1, {1, 2, 3, 4}}) == 0xef73dc3a80c8da6dULL, "1x1 FNV literal");
    auto small = image(2, 1, 9);
    std::uint64_t expected = 0xcbf29ce484222325ULL;
    const std::uint8_t prefix[8] = {2, 0, 0, 0, 1, 0, 0, 0};
    for (auto b : prefix) expected = (expected ^ b) * 0x100000001b3ULL;
    for (auto b : small.rgba) expected = (expected ^ b) * 0x100000001b3ULL;
    require(texture_key(small) == expected, "2x1 FNV loop");
    require(texture_key_hex(0x1abULL) == "00000000000001ab" && texture_key_hex(~0ULL) == "ffffffffffffffff", "hex format");
    bool threw = false;
    try { (void)texture_key(Image{2, 2, {1, 2, 3, 4}}); } catch (const std::runtime_error&) { threw = true; }
    require(threw, "invalid image key rejected");

    // Lossless RGBA round trip, then a gray8 PNG decodes to R=G=B.
    auto rgba = image(3, 2, 200);
    rgba.rgba[3] = 0; rgba.rgba[7] = 128;
    auto rgbaPng = encode_png(rgba);
    auto decoded = decode_png(rgbaPng);
    require(decoded.width == 3 && decoded.height == 2 && decoded.rgba == rgba.rgba, "PNG round trip");
    threw = false;
    try { const std::uint8_t junk[12] = {1, 2, 3}; (void)decode_png(junk); } catch (const std::runtime_error&) { threw = true; }
    require(threw, "non-PNG rejected");
    auto gray_png = [&](std::uint32_t w, std::uint32_t h, const std::vector<std::uint8_t>& values) {
        const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_PNG);
        AVCodecContext* codec = encoder ? avcodec_alloc_context3(encoder) : nullptr;
        AVFrame* frame = av_frame_alloc(); AVPacket* packet = av_packet_alloc();
        std::vector<std::uint8_t> bytes;
        bool ok = codec && frame && packet;
        if (ok) {
            codec->width = int(w); codec->height = int(h); codec->pix_fmt = AV_PIX_FMT_GRAY8; codec->time_base = {1, 1};
            frame->width = int(w); frame->height = int(h); frame->format = AV_PIX_FMT_GRAY8;
            ok = avcodec_open2(codec, encoder, nullptr) >= 0 && av_frame_get_buffer(frame, 0) >= 0;
        }
        if (ok) {
            for (std::uint32_t y = 0; y < h; ++y) std::memcpy(frame->data[0] + std::ptrdiff_t(y) * frame->linesize[0], values.data() + std::size_t(y) * w, w);
            ok = avcodec_send_frame(codec, frame) >= 0 && avcodec_send_frame(codec, nullptr) >= 0 && avcodec_receive_packet(codec, packet) >= 0;
        }
        if (ok) bytes.assign(packet->data, packet->data + packet->size);
        av_packet_free(&packet); av_frame_free(&frame); avcodec_free_context(&codec);
        require(ok, "gray PNG encode");
        return bytes;
    };
    const std::vector<std::uint8_t> grays{0, 1, 127, 128, 254, 255, 3, 77};
    auto gray = decode_png(gray_png(4, 2, grays));
    require(gray.width == 4 && gray.height == 2, "gray PNG dims");
    for (std::size_t i = 0; i < grays.size(); ++i)
        require(gray.rgba[i * 4] == grays[i] && gray.rgba[i * 4 + 1] == grays[i] && gray.rgba[i * 4 + 2] == grays[i] && gray.rgba[i * 4 + 3] == 255, "gray PNG R=G=B");

    // Manifest/pack boundaries in a private temporary tree.
    char pattern[] = "/tmp/k22-texture-pack-XXXXXX";
    if (!mkdtemp(pattern)) throw std::runtime_error("texture pack check: mkdtemp failed");
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{pattern};
    struct Capture { std::ostringstream text; std::streambuf* old{std::cerr.rdbuf(text.rdbuf())}; ~Capture() { std::cerr.rdbuf(old); } };
    struct Env { std::optional<std::string> old; Env() { if (auto* v = std::getenv("XDG_DATA_HOME")) old = v; }
        ~Env() { if (old) setenv("XDG_DATA_HOME", old->c_str(), 1); else unsetenv("XDG_DATA_HOME"); } } env;
    const fs::path tmp = cleanup.path;
    auto write = [](const fs::path& path, std::string_view bytes) {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        if (!out.write(bytes.data(), std::streamsize(bytes.size()))) throw std::runtime_error("texture pack check: write failed");
    };
    auto text = [](const std::vector<std::uint8_t>& bytes) { return std::string(bytes.begin(), bytes.end()); };
    auto ihdr = [](std::uint32_t w, std::uint32_t h) {
        std::string bytes("\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR", 16);
        for (auto v : {w, h}) for (int s = 24; s >= 0; s -= 8) bytes.push_back(char(v >> s));
        bytes.append("\x08\x06\0\0\0\0\0\0\0", 9);
        return bytes;
    };

    setenv("XDG_DATA_HOME", (tmp / "xdg").c_str(), 1);
    require(texture_pack_root() == tmp / "xdg/kabus22/packs", "XDG_DATA_HOME root");
    if (const char* home = std::getenv("HOME")) {
        setenv("XDG_DATA_HOME", "relative", 1);
        require(texture_pack_root() == fs::path(home) / ".local/share/kabus22/packs", "relative XDG_DATA_HOME ignored");
        setenv("XDG_DATA_HOME", "", 1);
        require(texture_pack_root() == fs::path(home) / ".local/share/kabus22/packs", "empty XDG_DATA_HOME ignored");
    }
    setenv("XDG_DATA_HOME", (tmp / "xdg").c_str(), 1);
    require(discover_texture_packs().empty(), "absent root discovers nothing");
    const fs::path root = texture_pack_root();

    auto original = image(2, 1, 50), other = image(2, 1, 90), mismatched = image(2, 1, 130);
    const auto key = texture_key_hex(texture_key(original)), otherKey = texture_key_hex(texture_key(other)), mismatchKey = texture_key_hex(texture_key(mismatched));
    const std::string bad[] = {"1111111111111111", "2222222222222222", "3333333333333333", "4444444444444444", "5555555555555555"};
    auto albedo = image(4, 2, 7);
    auto normal = image(4, 2, 11); for (std::size_t i = 3; i < normal.rgba.size(); i += 4) normal.rgba[i] = 7;
    const std::vector<std::uint8_t> rough{10, 20, 30, 40, 50, 60, 70, 80};
    const fs::path good = root / "b-good";
    write(good / "tex/a.png", text(encode_png(albedo)));
    write(good / "tex/n.png", text(encode_png(normal)));
    write(good / "r.png", text(gray_png(4, 2, rough)));
    write(good / "s3.png", text(encode_png(image(6, 3, 1))));
    write(good / "m1.png", text(encode_png(image(4, 2, 2))));
    write(good / "m2.png", text(encode_png(image(2, 2, 3))));
    write(good / "big.png", ihdr(9000, 10));
    write(good / "t.jpg", text(encode_png(albedo)));
    write(tmp / "outside.png", text(encode_png(albedo)));
    fs::create_symlink(tmp / "outside.png", good / "escape.png");
    write(good / "pack.txt", "kabus22-texture-pack 1\r\nname\tGood Pack\r\nlicense\tCC0\r\n\r\n" +
        key + "\talbedo\ttex/a.png\r\n" + key + "\tnormal\ttex/n.png\r\n" + key + "\troughness\tr.png\r\n" +
        otherKey + "\talbedo\ts3.png\n" + bad[0] + "\talbedo\t../outside.png\n" + bad[1] + "\talbedo\t" + (tmp / "outside.png").string() + "\n" +
        bad[2] + "\talbedo\tescape.png\n" + bad[3] + "\talbedo\tt.jpg\n" + bad[4] + "\talbedo\tbig.png\n" +
        mismatchKey + "\talbedo\tm1.png\n" + mismatchKey + "\tnormal\tm2.png\n");
    {
        Capture capture;
        auto pack = load_texture_pack(good);
        require(pack.valid && pack.detail == "2 textures" && pack.name == "b-good" && pack.displayName == "Good Pack" && pack.license == "CC0", "valid pack");
        require(pack.entries.size() == 2 && pack.entries.contains(texture_key(original)) && pack.entries.contains(texture_key(other)), "valid entries kept");
        const auto log = capture.text.str();
        for (const auto& k : bad) require(log.find(k + " albedo: ") != log.npos, "invalid entry logged");
        require(log.find(mismatchKey + ": ") != log.npos, "kind dimension mismatch logged");
        TexturePackSet set({pack});
        auto found = set.find(original);
        require(found && found->scale == 2 && found->albedo && found->albedo->width == 4 && found->albedo->height == 2 && found->albedo->rgba == albedo.rgba, "2x albedo");
        require(found->normal && found->normal->rgba[3] == 255 && found->normal->rgba[0] == normal.rgba[0], "normal alpha forced");
        require(found->roughness && !found->metallic && !found->ao && found->orm && found->orm->width == 4 && found->orm->height == 2, "orm flags");
        for (std::size_t i = 0; i < rough.size(); ++i)
            require(found->orm->rgba[i * 4] == 255 && found->orm->rgba[i * 4 + 1] == rough[i] && found->orm->rgba[i * 4 + 2] == 0 && found->orm->rgba[i * 4 + 3] == 255, "orm defaults");
        require(!set.find(other) && !set.find(other) && !set.find(image(3, 3, 1)), "3x scale and unknown rejected");
        const auto rejected = capture.text.str().substr(log.size());
        require(rejected.find(otherKey) != rejected.npos && rejected.find(otherKey) == rejected.rfind(otherKey), "bad scale logged once");
    }
    const std::string header = "kabus22-texture-pack 1\nname\tX\nlicense\tY\n";
    const std::pair<const char*, std::string> malformed[] = {
        {"c-header", "kabus22-texture-pack 2\nname\tX\nlicense\tY\n" + key + "\talbedo\tr.png\n"},
        {"c-noname", "kabus22-texture-pack 1\nlicense\tY\n" + key + "\talbedo\tr.png\n"},
        {"c-nolicense", "kabus22-texture-pack 1\nname\tX\n"},
        {"c-shortkey", header + key.substr(1) + "\talbedo\tr.png\n"},
        {"c-upperkey", header + "ABCDEF0123456789\talbedo\tr.png\n"},
        {"c-kind", header + key + "\tspecular\tr.png\n"},
        {"c-duplicate", header + key + "\talbedo\tr.png\n" + key + "\talbedo\tr.png\n"},
        {"c-late", header + key + "\talbedo\tr.png\nname\tZ\n"},
        {"c-empty", header},
    };
    for (const auto& [name, manifest] : malformed) {
        write(root / name / "pack.txt", manifest);
        fs::copy_file(good / "r.png", root / name / "r.png");
        auto pack = load_texture_pack(root / name);
        require(!pack.valid && !pack.detail.empty(), name);
    }
    require(load_texture_pack(root / "c-empty").detail == "no usable entries", "empty pack detail");
    fs::create_directories(root / "a-nomanifest");
    require(!load_texture_pack(root / "a-nomanifest").valid && !load_texture_pack(root / "missing").valid, "missing manifest/directory");
    fs::create_directory_symlink(good, root / "z-link");
    Capture quiet;
    auto packs = discover_texture_packs();
    require(packs.size() == 2 + std::size(malformed) && packs.front().name == "a-nomanifest" && packs[1].name == "b-good" && packs[1].valid && packs.back().name == "c-upperkey", "discover sorted, real directories only");

    // texture_upscale: exact 2x, alpha-test coverage identical at every threshold, colour key never bleeds into visible
    // texels, box-downscale stays within upscale.py's guard (6/255 mean error, 1.5/255 shift), Scale2x adds no colours.
    auto smooth = [](std::uint32_t w, std::uint32_t h, bool cutout) {
        Image result{w, h, std::vector<std::uint8_t>(std::size_t(w) * h * 4)};
        for (std::uint32_t y = 0; y < h; ++y) for (std::uint32_t x = 0; x < w; ++x) {
            auto* p = &result.rgba[(std::size_t(y) * w + x) * 4];
            const double dx = x - w / 2.0, dy = y - h / 2.0, r = std::sqrt(dx * dx + dy * dy);
            const std::uint8_t a = !cutout ? 255 : r < h / 4.0 ? 255 : r < h / 3.0 ? 100 : 0;
            if (!a) { p[0] = 255; p[1] = 0; p[2] = 255; p[3] = 0; continue; } // colour key
            p[0] = std::uint8_t(120 + 60 * std::sin(x * 0.7) * std::cos(y * 0.5)); p[1] = std::uint8_t(110 + 50 * std::cos(x * 0.3 + y * 0.4)); p[2] = std::uint8_t(90 + 40 * std::sin(y * 0.9)); p[3] = a;
        }
        return result;
    };
    for (bool wrap : {false, true}) {
        auto in = smooth(24, 16, true); auto out = upscale_texture_2x(in, wrap);
        require(out.width == 48 && out.height == 32 && out.rgba.size() == 48 * 32 * 4, "upscale is exactly 2x");
        for (int t : {1, 3, 128, 255}) {
            std::size_t before = 0, after = 0;
            for (std::size_t i = 3; i < in.rgba.size(); i += 4) before += in.rgba[i] >= t;
            for (std::size_t i = 3; i < out.rgba.size(); i += 4) after += out.rgba[i] >= t;
            require(after == 4 * before, "upscale keeps alpha-test coverage");
        }
        for (std::size_t i = 0; i < out.rgba.size(); i += 4) require(!out.rgba[i + 3] || out.rgba[i + 1] > 20, "colour key never bleeds into visible upscaled texels");
    }
    {
        auto in = smooth(32, 32, false); auto out = upscale_texture_2x(in, true);
        double error = 0, shift[3]{};
        for (std::uint32_t y = 0; y < 32; ++y) for (std::uint32_t x = 0; x < 32; ++x) for (int c = 0; c < 3; ++c) {
            double box = 0;
            for (int j = 0; j < 4; ++j) box += out.rgba[((std::size_t(y) * 2 + j / 2) * 64 + x * 2 + j % 2) * 4 + c] / 4.0;
            const double d = box - in.rgba[(std::size_t(y) * 32 + x) * 4 + c]; error += std::abs(d); shift[c] += d;
        }
        require(error / (32 * 32 * 3) <= 6 && std::abs(shift[0]) / 1024 <= 1.5 && std::abs(shift[1]) / 1024 <= 1.5 && std::abs(shift[2]) / 1024 <= 1.5, "upscale passes the box-downscale consistency guard");
    }
    {
        Image in{8, 8, std::vector<std::uint8_t>(8 * 8 * 4)};
        for (std::size_t i = 0; i < 64; ++i) { const std::uint8_t v = (i % 8 + i / 8) % 3 == 0 ? 200 : i % 8 > i / 8 ? 40 : 120; in.rgba[i * 4] = v; in.rgba[i * 4 + 1] = std::uint8_t(255 - v); in.rgba[i * 4 + 2] = 7; in.rgba[i * 4 + 3] = 255; }
        auto out = upscale_texture_2x(in, false);
        for (std::size_t i = 0; i < out.rgba.size(); i += 4) { bool known = false; for (std::size_t j = 0; j < in.rgba.size() && !known; j += 4) known = std::memcmp(&in.rgba[j], &out.rgba[i], 4) == 0; require(known, "Scale2x emits only source colours"); }
    }
}
}
