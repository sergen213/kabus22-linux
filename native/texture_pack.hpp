#pragma once
#include "assets.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace k22 {
// Contract E key: FNV-1a-64 over LE width, LE height, then engine-decoded RGBA8 rows.
std::uint64_t texture_key(const Image&);
std::string texture_key_hex(std::uint64_t);
// Lossless PNG through the existing FFmpeg dependency; any PNG pixel format decodes to RGBA8 top row first.
Image decode_png(std::span<const std::uint8_t>);
std::vector<std::uint8_t> encode_png(const Image&);
// texture_upscale (Vulkan): 2x one original texture, docs/TEXTURE_RESTORATION.md --method auto without AI. <=16 colours:
// Scale2x; else Lanczos-3 + mild unsharp. RGB is resampled after bleeding visible colour into alpha-0 texels; alpha is
// nearest-scaled, so alpha-test coverage is identical at every threshold. wrap: tiling (world) vs edge (model atlas).
Image upscale_texture_2x(const Image&, bool wrap);
enum class PackKind { Albedo, Normal, Roughness, Metallic, AO };
struct PackFile { std::filesystem::path path; std::uint32_t width{}, height{}; };
struct TexturePack {
    std::string name, displayName, license, detail;
    std::filesystem::path path;
    bool valid{};
    std::unordered_map<std::uint64_t, std::array<std::optional<PackFile>,5>> entries;
};
// Replacement set for one original image. Missing kinds keep originals/defaults:
// albedo original, flat normal, legacy roughness, metallic 0, AO 1.
struct PackTexture {
    std::shared_ptr<const Image> albedo, normal, orm; // orm RGBA: R=AO, G=roughness, B=metallic, A=255.
    bool roughness{}, metallic{}, ao{};
    std::uint32_t scale{1};
};
std::filesystem::path texture_pack_root(); // ${XDG_DATA_HOME:-~/.local/share}/kabus22/packs
TexturePack load_texture_pack(const std::filesystem::path& directory); // Never throws for pack content; valid=false with detail.
std::vector<TexturePack> discover_texture_packs(); // Every child directory of the root, sorted by directory name.
class TexturePackSet {
public:
    explicit TexturePackSet(std::vector<TexturePack> packs); // Priority order; invalid packs rejected by caller.
    // First pack containing the key provides every kind; decoded lazily, invalid entries logged once and ignored.
    std::optional<PackTexture> find(const Image& original) const;
    const std::vector<TexturePack>& packs() const { return packs_; }
private:
    std::vector<TexturePack> packs_;
    mutable std::unordered_map<std::uint64_t,bool> rejected_;
};
// Contract D --dump-textures: never writes inside data. Returns written texture count.
std::size_t dump_textures(const std::filesystem::path& data, bool bonus, const std::filesystem::path& output);
}
