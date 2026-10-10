#include "texture_pack.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <unordered_set>
#include <iconv.h>

namespace k22 {
namespace {
namespace fs = std::filesystem;
constexpr std::uint32_t maxSide = 8192;
constexpr std::uintmax_t maxPng = 64ULL * 1024 * 1024, maxManifest = 1024 * 1024;
constexpr std::array<std::uint8_t, 8> pngSignature{137, 'P', 'N', 'G', 13, 10, 26, 10};
constexpr std::array<const char*, 5> kindNames{"albedo", "normal", "roughness", "metallic", "ao"};
struct CodecFree { void operator()(AVCodecContext* p) const { avcodec_free_context(&p); } };
struct FrameFree { void operator()(AVFrame* p) const { av_frame_free(&p); } };
struct PacketFree { void operator()(AVPacket* p) const { av_packet_free(&p); } };
struct SwsFree { void operator()(SwsContext* p) const { sws_freeContext(p); } };
// Original resource/material names are Windows-1254 bytes; index.tsv is UTF-8 with TSV control bytes replaced.
std::string tsv_utf8(std::string_view name) {
    std::string out;iconv_t converter = iconv_open("UTF-8", "WINDOWS-1254");
    if (converter == (iconv_t)-1) throw std::runtime_error("Windows-1254 conversion unavailable");
    for (unsigned char byte : name) {
        if (byte < 0x20) { out += '_'; continue; }
        char in = char(byte), buffer[4]{}; char* input = &in; char* output = buffer; std::size_t inLeft = 1, outLeft = sizeof(buffer);
        if (iconv(converter, &input, &inLeft, &output, &outLeft) == std::size_t(-1)) out += '?'; else out.append(buffer, sizeof(buffer) - outLeft);
    }
    iconv_close(converter);return out;
}
using Codec = std::unique_ptr<AVCodecContext, CodecFree>;
using Frame = std::unique_ptr<AVFrame, FrameFree>;
using Packet = std::unique_ptr<AVPacket, PacketFree>;
void check(int result, const char* operation) {
    if (result >= 0) return;
    std::array<char, AV_ERROR_MAX_STRING_SIZE> error{};
    av_strerror(result, error.data(), error.size());
    throw std::runtime_error(std::string(operation) + ": " + error.data());
}
std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out) c = c == '\\' ? '/' : c >= 'A' && c <= 'Z' ? char(c + 32) : c;
    return out;
}
bool inside(const fs::path& child, const fs::path& parent) {
    auto relative = child.lexically_relative(parent);
    return !relative.empty() && *relative.begin() != "..";
}
std::vector<std::uint8_t> read_file(const fs::path& path, std::uintmax_t limit) {
    auto size = fs::file_size(path);
    if (size > limit) throw std::runtime_error("file exceeds size limit");
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> bytes(size);
    if (!in || (size && !in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(size))))
        throw std::runtime_error("cannot read " + path.string());
    return bytes;
}
void write_file(const fs::path& path, std::span<const std::uint8_t> bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out || !out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())) || !out.flush())
        throw std::runtime_error("cannot write " + path.string());
}
bool valid_image(const Image& image) {
    return image.width && image.height && image.rgba.size() == std::size_t(image.width) * image.height * 4;
}
// PNG width/height from the 33-byte signature+IHDR prefix, without decoding.
std::pair<std::uint32_t, std::uint32_t> png_header(const fs::path& path) {
    std::array<std::uint8_t, 33> header{};
    std::ifstream in(path, std::ios::binary);
    if (!in.read(reinterpret_cast<char*>(header.data()), header.size())) throw std::runtime_error("truncated PNG header");
    auto big = [&](std::size_t o) { return std::uint32_t(header[o]) << 24 | std::uint32_t(header[o + 1]) << 16 | std::uint32_t(header[o + 2]) << 8 | header[o + 3]; };
    if (!std::equal(pngSignature.begin(), pngSignature.end(), header.begin())) throw std::runtime_error("not a PNG file");
    if (big(8) != 13 || std::memcmp(header.data() + 12, "IHDR", 4) != 0) throw std::runtime_error("missing PNG IHDR");
    return {big(16), big(20)};
}
}

std::uint64_t texture_key(const Image& image) {
    if (!valid_image(image)) throw std::runtime_error("texture key requires a valid RGBA8 image");
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    auto mix = [&](std::uint8_t byte) { hash = (hash ^ byte) * 0x100000001b3ULL; };
    for (std::uint32_t value : {image.width, image.height})
        for (int i = 0; i < 4; ++i) mix(std::uint8_t(value >> (8 * i)));
    for (auto byte : image.rgba) mix(byte);
    return hash;
}
std::string texture_key_hex(std::uint64_t key) {
    std::string text(16, '0');
    for (int i = 15; i >= 0; --i, key >>= 4) text[i] = "0123456789abcdef"[key & 15];
    return text;
}

Image decode_png(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < pngSignature.size() || !std::equal(pngSignature.begin(), pngSignature.end(), bytes.begin()))
        throw std::runtime_error("not a PNG file");
    if (bytes.size() > maxPng) throw std::runtime_error("PNG exceeds 64 MiB");
    const AVCodec* decoder = avcodec_find_decoder(AV_CODEC_ID_PNG);
    if (!decoder) throw std::runtime_error("PNG decoder unavailable");
    Codec codec(avcodec_alloc_context3(decoder));
    Frame frame(av_frame_alloc());
    Packet packet(av_packet_alloc());
    if (!codec || !frame || !packet) throw std::bad_alloc();
    codec->err_recognition = AV_EF_CRCCHECK | AV_EF_CAREFUL | AV_EF_EXPLODE;
    check(avcodec_open2(codec.get(), decoder, nullptr), "Opening PNG decoder");
    check(av_new_packet(packet.get(), int(bytes.size())), "PNG packet allocation");
    std::memcpy(packet->data, bytes.data(), bytes.size());
    check(avcodec_send_packet(codec.get(), packet.get()), "Submitting PNG");
    check(avcodec_receive_frame(codec.get(), frame.get()), "Decoding PNG");
    if (frame->width <= 0 || frame->height <= 0 || frame->width > int(maxSide) || frame->height > int(maxSide))
        throw std::runtime_error("PNG dimensions outside 1..8192");
    Image image{std::uint32_t(frame->width), std::uint32_t(frame->height), std::vector<std::uint8_t>(std::size_t(frame->width) * frame->height * 4)};
    const auto format = AVPixelFormat(frame->format);
    if (format == AV_PIX_FMT_RGBA) {
        for (int y = 0; y < frame->height; ++y)
            std::memcpy(image.rgba.data() + std::size_t(y) * frame->width * 4, frame->data[0] + std::ptrdiff_t(y) * frame->linesize[0], std::size_t(frame->width) * 4);
        return image;
    }
    std::unique_ptr<SwsContext, SwsFree> scale(sws_getContext(frame->width, frame->height, format, frame->width, frame->height,
        AV_PIX_FMT_RGBA, SWS_POINT | SWS_ACCURATE_RND | SWS_BITEXACT, nullptr, nullptr, nullptr));
    if (!scale) throw std::runtime_error("Unable to convert PNG pixels");
    // Gray PNG samples are full-range luma; keep 8-bit values exact as R=G=B.
    const int* table = sws_getCoefficients(SWS_CS_DEFAULT);
    sws_setColorspaceDetails(scale.get(), table, 1, table, 1, 0, 1 << 16, 1 << 16);
    std::uint8_t* destinations[4] = {image.rgba.data(), nullptr, nullptr, nullptr};
    int strides[4] = {frame->width * 4, 0, 0, 0};
    check(sws_scale(scale.get(), frame->data, frame->linesize, 0, frame->height, destinations, strides), "Converting PNG pixels");
    return image;
}
std::vector<std::uint8_t> encode_png(const Image& image) {
    if (!valid_image(image) || image.width > maxSide || image.height > maxSide) throw std::runtime_error("PNG encode requires a valid RGBA8 image up to 8192 px");
    const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_PNG);
    if (!encoder) throw std::runtime_error("PNG encoder unavailable");
    Codec codec(avcodec_alloc_context3(encoder));
    Frame frame(av_frame_alloc());
    Packet packet(av_packet_alloc());
    if (!codec || !frame || !packet) throw std::bad_alloc();
    codec->width = int(image.width); codec->height = int(image.height);
    codec->pix_fmt = AV_PIX_FMT_RGBA; codec->time_base = {1, 1};
    check(avcodec_open2(codec.get(), encoder, nullptr), "Opening PNG encoder");
    frame->width = codec->width; frame->height = codec->height; frame->format = AV_PIX_FMT_RGBA;
    check(av_frame_get_buffer(frame.get(), 0), "PNG frame allocation");
    for (std::uint32_t y = 0; y < image.height; ++y)
        std::memcpy(frame->data[0] + std::ptrdiff_t(y) * frame->linesize[0], image.rgba.data() + std::size_t(y) * image.width * 4, std::size_t(image.width) * 4);
    check(avcodec_send_frame(codec.get(), frame.get()), "Submitting PNG frame");
    check(avcodec_send_frame(codec.get(), nullptr), "Flushing PNG encoder");
    check(avcodec_receive_packet(codec.get(), packet.get()), "Encoding PNG");
    return {packet->data, packet->data + packet->size};
}

fs::path texture_pack_root() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg && fs::path(xdg).is_absolute()) return fs::path(xdg) / "kabus22/packs";
    const char* home = std::getenv("HOME");
    if (!home || !*home) throw std::runtime_error("HOME or XDG_DATA_HOME required for texture packs");
    return fs::path(home) / ".local/share/kabus22/packs";
}

TexturePack load_texture_pack(const fs::path& directory) {
    TexturePack pack;
    pack.name = directory.filename().string();
    pack.path = directory;
    try {
        std::error_code ec;
        if (!fs::is_directory(directory, ec)) { pack.detail = "missing directory"; return pack; }
        const auto manifest = directory / "pack.txt";
        if (!fs::is_regular_file(fs::symlink_status(manifest, ec))) { pack.detail = "missing pack.txt (regular file, not symlink)"; return pack; }
        auto bytes = read_file(manifest, maxManifest);
        std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        struct Raw { std::uint64_t key; std::size_t kind; std::string path; };
        std::vector<Raw> raw;
        std::unordered_set<std::string> seen;
        bool hasName = false, hasLicense = false;
        std::size_t number = 0;
        auto malformed = [&](std::string reason) { pack.detail = "pack.txt line " + std::to_string(number) + ": " + reason; return pack; };
        for (std::size_t start = 0; start <= text.size();) {
            auto end = std::min(text.find('\n', start), text.size());
            auto line = text.substr(start, end - start);
            start = end + 1; ++number;
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (number == 1) { if (line != "kabus22-texture-pack 1") return malformed("expected header 'kabus22-texture-pack 1'"); continue; }
            if (line.empty()) continue;
            std::vector<std::string_view> fields;
            for (std::size_t p = 0;;) { auto tab = line.find('\t', p); fields.push_back(line.substr(p, tab - p)); if (tab == line.npos) break; p = tab + 1; }
            if (fields.size() == 2 && (fields[0] == "name" || fields[0] == "license")) {
                bool& flag = fields[0] == "name" ? hasName : hasLicense;
                if (flag) return malformed("duplicate " + std::string(fields[0]));
                if (!raw.empty()) return malformed(std::string(fields[0]) + " after entries");
                if (fields[1].empty()) return malformed("empty " + std::string(fields[0]));
                flag = true;
                (fields[0] == "name" ? pack.displayName : pack.license) = fields[1];
                continue;
            }
            if (fields.size() != 3) return malformed("expected key<TAB>kind<TAB>path");
            if (!hasName || !hasLicense) return malformed("name and license must precede entries");
            if (fields[0].size() != 16 || fields[0].find_first_not_of("0123456789abcdef") != fields[0].npos) return malformed("key must be 16 lowercase hex digits");
            auto kind = std::size_t(std::find(kindNames.begin(), kindNames.end(), fields[1]) - kindNames.begin());
            if (kind == kindNames.size()) return malformed("unknown kind '" + std::string(fields[1]) + "'");
            if (!seen.insert(std::string(fields[0]) + '\t' + std::string(fields[1])).second) return malformed("duplicate key/kind");
            raw.push_back({std::stoull(std::string(fields[0]), nullptr, 16), kind, std::string(fields[2])});
        }
        if (!hasName || !hasLicense) { pack.detail = "pack.txt: missing name or license"; return pack; }
        const auto root = fs::canonical(directory);
        for (const auto& entry : raw) {
            auto reject = [&](const std::string& reason) {
                std::cerr << "kabus22: texture pack " << pack.name << ": " << texture_key_hex(entry.key) << ' ' << kindNames[entry.kind] << ": " << reason << '\n';
            };
            fs::path relative(entry.path);
            if (relative.empty() || relative.is_absolute() || relative.has_root_path()) { reject("path must be relative"); continue; }
            if (std::any_of(relative.begin(), relative.end(), [](const fs::path& part) { return part == ".."; })) { reject("path must not contain '..'"); continue; }
            if (lower(relative.extension().string()) != ".png") { reject("only .png files are accepted"); continue; }
            auto file = fs::canonical(root / relative, ec);
            if (ec) { reject("missing file"); continue; }
            if (!inside(file, root) || file == root) { reject("path escapes pack directory"); continue; }
            if (!fs::is_regular_file(file, ec)) { reject("not a regular file"); continue; }
            if (fs::file_size(file, ec) > maxPng || ec) { reject("file exceeds 64 MiB"); continue; }
            std::pair<std::uint32_t, std::uint32_t> size;
            try { size = png_header(file); } catch (const std::exception& error) { reject(error.what()); continue; }
            if (!size.first || !size.second || size.first > maxSide || size.second > maxSide) { reject("dimensions outside 1..8192"); continue; }
            pack.entries[entry.key][entry.kind] = PackFile{file, size.first, size.second};
        }
        for (auto it = pack.entries.begin(); it != pack.entries.end();) {
            const PackFile* first = nullptr; bool mismatch = false;
            for (const auto& file : it->second) if (file) { if (!first) first = &*file; else mismatch |= file->width != first->width || file->height != first->height; }
            if (!first || mismatch) {
                if (mismatch) std::cerr << "kabus22: texture pack " << pack.name << ": " << texture_key_hex(it->first) << ": kinds have different dimensions\n";
                it = pack.entries.erase(it);
            } else ++it;
        }
        pack.valid = !pack.entries.empty();
        pack.detail = pack.valid ? std::to_string(pack.entries.size()) + " textures" : "no usable entries";
    } catch (const std::exception& error) {
        pack.valid = false; pack.entries.clear();
        pack.detail = error.what();
    }
    return pack;
}

std::vector<TexturePack> discover_texture_packs() {
    std::vector<TexturePack> packs;
    std::error_code ec;
    const auto root = texture_pack_root();
    if (!fs::is_directory(root, ec)) return packs;
    std::vector<fs::path> directories;
    for (const auto& entry : fs::directory_iterator(root, ec))
        if (!entry.is_symlink(ec) && entry.is_directory(ec)) directories.push_back(entry.path());
    std::sort(directories.begin(), directories.end(), [](const fs::path& a, const fs::path& b) { return a.filename() < b.filename(); });
    for (const auto& directory : directories) packs.push_back(load_texture_pack(directory));
    return packs;
}

TexturePackSet::TexturePackSet(std::vector<TexturePack> packs) : packs_(std::move(packs)) {}

std::optional<PackTexture> TexturePackSet::find(const Image& original) const {
    if (!valid_image(original)) return std::nullopt;
    const auto key = texture_key(original);
    if (rejected_.contains(key)) return std::nullopt;
    for (const auto& pack : packs_) {
        auto found = pack.entries.find(key);
        if (found == pack.entries.end()) continue;
        auto reject = [&](const std::string& reason) {
            std::cerr << "kabus22: texture pack " << pack.name << ": " << texture_key_hex(key) << ": " << reason << '\n';
            rejected_[key] = true;
            return std::nullopt;
        };
        const auto& files = found->second;
        const auto& first = *std::find_if(files.begin(), files.end(), [](const auto& file) { return file.has_value(); });
        const std::uint32_t scale = first->width / original.width;
        if (first->width % original.width || (scale != 1 && scale != 2 && scale != 4 && scale != 8) || first->height != std::uint64_t(original.height) * scale)
            return reject(std::to_string(first->width) + "x" + std::to_string(first->height) + " is not 1/2/4/8 x " +
                          std::to_string(original.width) + "x" + std::to_string(original.height));
        std::array<std::optional<Image>, 5> images;
        for (std::size_t kind = 0; kind < files.size(); ++kind) {
            if (!files[kind]) continue;
            try {
                images[kind] = decode_png(read_file(files[kind]->path, maxPng));
                if (images[kind]->width != files[kind]->width || images[kind]->height != files[kind]->height) throw std::runtime_error("decoded size differs from PNG header");
            } catch (const std::exception& error) { return reject(std::string(kindNames[kind]) + ": " + error.what()); }
        }
        PackTexture result;
        result.scale = scale;
        if (auto& albedo = images[std::size_t(PackKind::Albedo)]) {
            albedo->sourceDepth = original.sourceDepth; albedo->nativeStorageBytes = original.nativeStorageBytes; albedo->nativeTextureAlpha = original.nativeTextureAlpha;
            result.albedo = std::make_shared<const Image>(std::move(*albedo));
        }
        if (auto& normal = images[std::size_t(PackKind::Normal)]) {
            for (std::size_t i = 3; i < normal->rgba.size(); i += 4) normal->rgba[i] = 255;
            result.normal = std::make_shared<const Image>(std::move(*normal));
        }
        const auto& ao = images[std::size_t(PackKind::AO)], &roughness = images[std::size_t(PackKind::Roughness)], &metallic = images[std::size_t(PackKind::Metallic)];
        result.ao = ao.has_value(); result.roughness = roughness.has_value(); result.metallic = metallic.has_value();
        if (result.ao || result.roughness || result.metallic) {
            Image orm{first->width, first->height, std::vector<std::uint8_t>(std::size_t(first->width) * first->height * 4)};
            for (std::size_t i = 0; i < orm.rgba.size(); i += 4) {
                orm.rgba[i] = ao ? ao->rgba[i] : 255;
                orm.rgba[i + 1] = roughness ? roughness->rgba[i] : 255;
                orm.rgba[i + 2] = metallic ? metallic->rgba[i] : 0;
                orm.rgba[i + 3] = 255;
            }
            result.orm = std::make_shared<const Image>(std::move(orm));
        }
        return result;
    }
    return std::nullopt;
}

std::size_t dump_textures(const fs::path& data, bool bonus, const fs::path& output) {
    const auto dataRoot = fs::canonical(data);
    if (inside(fs::weakly_canonical(fs::absolute(output)), dataRoot)) throw std::runtime_error("dump output must not be inside the game data directory");
    fs::create_directories(output);
    const auto outputRoot = fs::canonical(output);
    if (inside(outputRoot, dataRoot)) throw std::runtime_error("dump output must not be inside the game data directory");
    Assets assets(data, bonus);
    if (bonus) assets.mount("K22_mode.wrs");
    else for (auto name : {"K22Widescreen.wrs", "K22.wrs", "Kaynak.k22"}) if (assets.contains(name)) assets.mount(name); // Same first-mounted-wins order as native/main.cpp.
    assets.initialize_native_lightmap_tables(102400, false, true);
    assets.set_native_alpha_depth(32);
    struct Row { std::uint32_t width, height; std::string category, source; };
    std::map<std::uint64_t, Row> rows;
    auto add = [&](const Image* image, std::string_view category, const std::string& source) {
        if (!image) return;
        try {
            auto key = texture_key(*image);
            if (rows.contains(key)) return;
            write_file(outputRoot / (texture_key_hex(key) + ".png"), encode_png(*image));
            rows.emplace(key, Row{image->width, image->height, std::string(category), source});
        } catch (const std::exception& error) { std::cerr << "kabus22: dump: skipping " << source << ": " << error.what() << '\n'; }
    };
    auto extension = [](const std::string& name) { auto dot = name.rfind('.'); return dot == name.npos ? std::string{} : name.substr(dot); };
    const auto names = assets.names();
    std::unordered_set<std::string> spriteFiles;
    for (const auto& name : names) {
        const auto ext = extension(name);
        if (ext != ".wmb" && ext != ".mdl" && ext != ".hmp") continue;
        try {
            if (ext == ".wmb") {
                const auto& world = assets.world(name);
                for (const auto& entity : world.entities) if (!entity.file.empty()) spriteFiles.insert(lower(entity.file));
                for (const auto& material : world.mesh.materials)
                    add(material.texture.get(), (material.flags & 15) == 5 ? "sky" : "world", name + ":" + (material.textureName.empty() ? material.name : material.textureName));
            } else {
                const auto& model = assets.model(name);
                for (std::size_t i = 0; i < model.materials.size(); ++i) add(model.materials[i].texture.get(), "model", name + ":skin" + std::to_string(i));
            }
        } catch (const std::exception& error) { std::cerr << "kabus22: dump: skipping " << name << ": " << error.what() << '\n'; }
        assets.release_level();
    }
    for (const auto& name : names) {
        const auto ext = extension(name);
        if (ext != ".pcx" && ext != ".tga" && ext != ".bmp" && ext != ".dds") continue;
        try {
            auto image = assets.image_ptr(name);
            add(image.get(), name.find("font") != name.npos ? "font" : spriteFiles.contains(name) ? "sprite" : "ui", name);
            assets.purge_image(name);
        } catch (const std::exception& error) { std::cerr << "kabus22: dump: skipping " << name << ": " << error.what() << '\n'; }
    }
    std::string index;
    for (const auto& [key, row] : rows)
        index += texture_key_hex(key) + '\t' + std::to_string(row.width) + '\t' + std::to_string(row.height) + '\t' + row.category + '\t' + tsv_utf8(row.source) + '\n';
    write_file(outputRoot / "index.tsv", std::span(reinterpret_cast<const std::uint8_t*>(index.data()), index.size()));
    return rows.size();
}
}
