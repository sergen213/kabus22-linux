#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <future>
#include <vector>

namespace k22 {
struct Vec2 { float x{}, y{}; };
struct Vec3 { float x{}, y{}, z{}; };
struct Color { float r{}, g{}, b{}, a{1}; };
// sourceDepth is original pixel bit depth; 0 denotes compressed/non-packed sources.
// nativeStorageBytes records native CPU storage: 0 is encoded/compressed, 255 is unknown.
// nativeTextureAlpha captures A7 BMAP100 under native decoded-texture policy, not pixel opacity.
// a6AlphaFormat is A6 BMAP0x100: the file is a 32-bit TGA (0040e2b0); such bitmaps are never OVERLAY-keyed.
struct Image { uint32_t width{}, height{}; std::vector<uint8_t> rgba; uint32_t sourceDepth{32}; uint8_t nativeStorageBytes{255}; bool nativeTextureAlpha{}; bool a6AlphaFormat{}; };
struct Vertex { Vec3 position, normal; Vec2 uv, lightmap_uv; };
// sourceFace is the original WMB face index; non-world triangles retain ~0u.
struct Triangle { std::array<uint32_t,3> indices{}; uint32_t materialIndex{}; uint32_t flags{}; uint32_t sourceFace{~0u}; };
struct Material {
    std::string name, textureName, lightmapName;
    std::shared_ptr<const Image> texture, lightmap;
    uint32_t flags{};
    Color ambient{1,1,1,1}, albedo{1,1,1,1}, specular{}, emissive{};
    float power{}, alpha{1}; Vec2 uvScale{1,1}, uvOffset;
    std::vector<uint8_t> raw;
    // A7 runtime BMAP primary-face use mark, not a serialized skin-type bit.
    bool nativePrimarySubset{};
};
struct AnimationFrame { std::string name; std::vector<Vec3> positions, normals, sourcePositions, sourceNormals; std::vector<std::array<float,12>> boneTransforms; };
struct Bone { std::string name; int32_t parent{-1}; std::array<float,12> transform{}; };
struct VertexWeight { uint32_t vertex{}, bone{}; float weight{}; };
struct World;
struct Model {
    std::vector<Vertex> vertices; std::vector<Triangle> triangles; std::vector<Material> materials;
    std::vector<Vertex> sourceVertices; std::vector<std::array<uint32_t,3>> sourceTriangles;
    std::vector<uint32_t> vertexSources;
    std::vector<uint32_t> sourceRenderOffsets,sourceRenderIndices;
    std::vector<AnimationFrame> frames; std::vector<std::string> frameNames;
    std::vector<Bone> bones; std::vector<VertexWeight> weights;
    Vec3 min, max; uint32_t flags{}; std::vector<uint8_t> metadata;
    // A6 MDL resource box: first animation frame only (K22.exe 00457d10/00457d70 bound each frame block; 00458500 copies block 1 to +0x38).
    Vec3 resourceMin, resourceMax;
    uint32_t formatVersion{},terrainWidth{},terrainHeight{}; Vec2 terrainSpacing;
    // Original resource slots, initialized at birth; null selected slots reject geometry.
    std::array<const Model*,3> shadowLods{};
    const World* nativeWorld{}; // Borrowed original WMB face metadata; valid for this Assets-owned World lifetime.
    bool clockwise{};
};
// Native MDL/HMP skins are one model-wide bitmap bank, not per-face alternatives.
bool model_has_entity_skins(const Model&) noexcept;
uint32_t model_skin_index(const Model&,int32_t oneBasedSelector);
const Material* model_material(const Model&,uint32_t faceMaterial,uint32_t skinIndex);
struct WorldEntity {
    uint32_t type{}, flags{}; std::string name, file, action;
    Vec3 position, angle, scale{1,1,1}; std::array<float,100> skills{};
    Color ambient{1,1,1,1}; float albedo{1}; std::vector<uint8_t> metadata;
    uint32_t objectIndex{},pathIndex{},attachedEntityIndex{};
    std::string path,material,string1,string2; float soundVolume{},soundRange{};
    Vec3 regionMin,regionMax;
};
struct PathNode { Vec3 position; std::array<float,6> skills{}; std::vector<uint32_t> edges; std::vector<uint8_t> metadata; };
struct PathEdgeInfo { uint32_t node1{},node2{}; float length{},bezier{},weight{},skill{}; };
struct Path { std::string name; std::vector<PathNode> nodes; std::vector<std::array<uint32_t,2>> edges; std::vector<PathEdgeInfo> edgeInfo; std::vector<uint8_t> metadata; };
struct Light { Vec3 position; Color color; float range{}; uint32_t flags{}; Vec3 direction; float cone{}; };
struct CollisionMesh { std::vector<Vec3> vertices; std::vector<std::array<uint32_t,3>> triangles; std::vector<uint32_t> flags; };
struct WorldPlane { Vec3 normal; float distance{}; uint32_t type{}; };
// Native texel origins/extents and flags are separate from normalized renderer UVs.
struct WorldFace { uint32_t plane{},legacyPlane{},side{},firstEdge{},edgeCount{},materialIndex{}; std::array<uint8_t,4> lightStyles{}; int32_t lightmapOffset{}; uint32_t lightmapScale{},lightmapIndex{}; Vec2 lightmapOrigin; int32_t texelOriginS{},texelOriginT{},texelExtentS{},texelExtentT{}; uint32_t nativeFlags{}; uint8_t nativeSampleShift{}; };
struct WorldBspNode { uint32_t plane{}; std::array<int32_t,2> children{}; Vec3 min,max; uint32_t firstFace{},faceCount{}; };
struct WorldBspLeaf { int32_t content{},pvsOffset{}; Vec3 min,max; uint32_t firstFace{},faceCount{},firstBlock{},blockCount{}; std::array<uint8_t,4> ambient{}; };
// Point query nodes retain their source BSP node; compiled hull nodes have none.
struct WorldHull { uint32_t plane{}; std::array<int32_t,2> children{}; uint32_t bspNode{~0u}; };
struct WorldLegacyHull { int32_t root{},last{-1}; Vec3 lower{},upper{}; bool generic{}; };
struct World {
    Model mesh; std::vector<WorldEntity> entities; std::vector<Path> paths; std::vector<Light> lights;
    CollisionMesh collision; Color ambient, sunColor, fogColor; Vec3 sunDirection;
    float fogStart{}, fogEnd{}; std::array<std::vector<uint8_t>,17> sections;
    std::vector<WorldPlane> planes; std::vector<WorldFace> faces; std::vector<WorldBspNode> bspNodes;
    std::vector<WorldBspLeaf> bspLeaves; std::vector<WorldHull> hulls; std::vector<uint32_t> bspBlocks;
    std::array<Color,4> fogColors{}; float gamma{},compilerVersion{},sunAzimuth{},sunElevation{};
    uint32_t infoFlags{},lightmapSize{}; std::vector<std::shared_ptr<const Image>> lightmaps;
    std::vector<uint8_t> metadata;
    // Captured native CPU layout: 1 mono, 2 RGB565, 3 RGB24, 255 unknown.
    uint8_t nativeLightmapStorageBytes{255};
    // A6 descriptors: point, narrow, fat. Bounds are converted public HULL units.
    std::array<WorldLegacyHull,3> legacyHulls{};
    std::vector<WorldHull> legacyPointNodes;
    bool legacyHullsValid{},legacyHullBoundsPublished{};
};
class Assets {
public:
    // Loose files precede archives; the first-mounted archive member wins (A6 and A7). Bonus selects A7 WMB/MDL records.
    explicit Assets(std::filesystem::path root,bool bonusProfile=false);
    // Captured by new image loads; existing cached bitmap storage is unchanged.
    void set_native_alpha_depth(double depth) noexcept { nativeAlphaDepth_=depth; }
    // Captured by new worlds; existing cached world/lightmap storage is unchanged.
    void set_native_shadow_depth(double depth,bool stageCombinationCapable) noexcept {
        nativeColoredLightmapBytes_=stageCombinationCapable&&depth>=24?3:2;
    }
    // Original 565 tables capture runtime startup state once, not per world load.
    void initialize_native_lightmap_tables(int32_t gammaRaw,bool monochrome,bool stageCombinationCapable);
    uint16_t native_lightmap_color(uint16_t word) const;
    uint8_t native_lightmap_mono(uint16_t word) const;
    void mount(std::string_view resource);
    // Mod overlay (docs/MODDING.md): call in priority order, first call wins; mod files precede loose files and archives. Returns file count.
    // A mod's <dir>/<stem>.gltf or .glb also provides <dir>/<stem>.mdl (model() imports it, model_gltf.hpp).
    size_t add_mod(const std::filesystem::path& dataDir);
    // {mod data root, file} providing name, or null when it is the game's own.
    const std::pair<std::filesystem::path,std::filesystem::path>* mod_file(std::string_view name) const;
    bool contains(std::string_view name) const;
    std::vector<std::string> names() const; // Sorted normalized names of loose files and mounted archive entries.
    std::vector<uint8_t> read(std::string_view name) const;
    std::string text(std::string_view name) const;
    std::string archive_text(std::string_view name) const;
    const Model& model(std::string_view name);
    const World& world(std::string_view name);
    const Image& image(std::string_view name);
    std::shared_ptr<const Image> image_ptr(std::string_view name);
    void purge_image(std::string_view name);
    // Caller retires all borrowed model/GPU references before purging.
    bool purge_model(std::string_view name);
    const Model* find_model(std::string_view name) const; // cached decode or null; never loads
    // Invalidates decoded references; call only after scene/render teardown. keepWorld: one decoded world survives (seamless door destination).
    // keepModels: models still used by entities that outlive the level (and their shadow LOD companions) stay decoded.
    void release_level(std::string_view keepWorld={},std::span<const Model* const> keepModels={});
    // Seamless doors: decodes a WMB on a worker thread from a snapshot of this Assets (archives, decode settings, decoded
    // images); world() adopts the result (waiting if needed). True once the world is cached; decode errors surface there.
    bool prefetch_world(std::string_view name);
    // Forgets a cached or finished prefetched world; the caller retires its GPU/model references first.
    void drop_world(std::string_view name);
    static std::vector<uint8_t> decompress(std::span<const uint8_t> bytes, size_t expected);
    const std::filesystem::path& root() const { return root_; }
private:
    struct Entry { std::filesystem::path archive; uint64_t offset{}; uint32_t packed{}, size{}; };
    std::vector<uint8_t> read_entry(const Entry&) const;
    std::vector<uint8_t> read_original(std::string_view name) const; // loose file or archive member, ignoring mods
    std::filesystem::path resolve(std::string_view name) const;
    std::shared_ptr<const Image> embedded_image(std::span<const uint8_t>,size_t&,uint32_t,uint32_t,uint32_t);
    const Model* load_model(std::string_view name,bool initializeShadowResources);
    Model decode_model(std::span<const uint8_t>, std::string_view);
    World decode_world(std::span<const uint8_t>, std::string_view);
    std::filesystem::path root_;
    bool bonusProfile_{};
    double nativeAlphaDepth_{32};
    uint8_t nativeColoredLightmapBytes_{255};
    std::array<uint8_t,256> nativeLightmapRamp_{},nativeMonoRamp_{};
    bool nativeLightmapTablesInitialized_{},nativeLightmapMonochrome_{};
    std::unordered_map<std::string,std::filesystem::path> loose_;
    std::unordered_map<std::string,std::pair<std::filesystem::path,std::filesystem::path>> mods_; // name -> {mod data root, file}
    std::unordered_map<std::string,Entry> entries_;
    std::unordered_set<std::string> mounted_;
    std::unordered_map<std::string,std::shared_ptr<const Image>> images_;
    std::unordered_map<std::string,Model> models_;
    std::unordered_map<std::string,World> worlds_;
    // Seamless door prefetch: the world plus its entities' models/images, decoded by a worker Assets snapshot.
    struct Prefetched{World world;std::unordered_map<std::string,Model> models;std::unordered_map<std::string,std::shared_ptr<const Image>> images;double alphaDepth{};};
    std::unordered_map<std::string,std::shared_future<std::shared_ptr<Prefetched>>> prefetches_;
    std::unordered_map<std::string,std::unordered_map<std::string,Model>> prefetchedModels_; // by world; adopted when it becomes the level
    size_t cachedBytes_{};
};
}
