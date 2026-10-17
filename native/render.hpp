#pragma once

#include "assets.hpp"
#include <SDL.h>
#include <array>
#include <bitset>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace k22 {

enum class WindowMode { Windowed, Fullscreen, Borderless };
enum class RendererBackend { OpenGL, Vulkan };
struct WindowMetrics { int width{}, height{}, drawableWidth{}, drawableHeight{}; float scaleX{1}, scaleY{1}; };
class TexturePackSet;
enum class ShadowMode : std::uint8_t { Original, ShadowMap, RayTraced };
// Wave-2 optional effects; all default off, so a default-constructed value is the original path.
struct GraphicsFeatures {
    ShadowMode shadows{ShadowMode::Original};
    bool gi{}, roughReflections{}, denoise{}, bloom{}, ssao{}, fxaa{}, taa{}, fog{};
    // Vulkan-only visual upgrades: smoothed vertex normals, PN-triangle tessellation, load-time texture upscaling, sharpening.
    bool smoothNormals{}, tessellation{}, upscale{}, sharpen{};
    // AMD FSR 3.1.4 upscaling (Vulkan): 0 off, 1 native AA, 2 quality, 3 balanced, 4 performance, 5 ultra performance.
    // Replaces TAA; scene/RT/denoise run at the render resolution, UI stays at the display resolution.
    std::uint8_t fsr{};
    bool operator==(const GraphicsFeatures&) const = default;
};
struct RenderOptions { int width{1280}, height{720}, samples{4}; float anisotropy{8}; bool vsync{true}; WindowMode mode{WindowMode::Windowed}; std::string title{"Kabus 22"}; RendererBackend backend{RendererBackend::OpenGL}; bool rayTracing{true};
    bool pbr{}, forceAnisotropy{}, hidden{}; // PBR/HDR path; anisotropy regardless of script D3D gate; hidden probe window.
    std::shared_ptr<const TexturePackSet> packs; // Null keeps original textures exactly.
    GraphicsFeatures features; // Admitted for the selected backend by render_policy::admit_features.
};
struct Input {
    std::bitset<SDL_NUM_SCANCODES> keys, pressed, released;
    std::bitset<8> mouseButtons, mousePressed, mouseReleased;
    // Game controllers in stable slots: the first connected pad is 0; a removed pad frees its slot, the next one fills the lowest free slot.
    static constexpr int Pads = 2;
    std::array<std::bitset<SDL_CONTROLLER_BUTTON_MAX>,Pads> controllerButtons, controllerPressed, controllerReleased;
    std::array<std::array<float,SDL_CONTROLLER_AXIS_MAX>,Pads> controllerAxes{};
    std::array<bool,Pads> controllerConnected{};
    bool pad_pressed(int button) const { for(auto& p : controllerPressed) if(p[button]) return true; return false; } // any pad, this frame
    bool pad_held(int button) const { for(int p = 0; p < Pads; ++p) if(controllerButtons[p][button] || controllerPressed[p][button]) return true; return false; } // any pad
    bool pad_any() const { for(auto& p : controllerButtons) if(p.any()) return true; return false; } // any button held on any pad
    float mouseX{}, mouseY{}, mouseDX{}, mouseDY{}, wheelX{}, wheelY{};
    std::array<std::bitset<32>,2> joystickButtons, joystickPressed, joystickReleased;
    std::array<std::array<float,6>,2> joystickAxes{};
    std::array<bool,2> joystickConnected{};
    std::array<int,2> joystickPad{-1, -1}; // game controller slot of the same device as raw slot j; -1: not a game controller
    double dt{};
    bool quit{};
    std::string text;
};
struct Rect { float x{}, y{}, width{}, height{}; };
enum class EffectKind { None, NormalMix, NormalSunMix };
struct ScriptMaterialState {
    std::uint32_t sourceId{}, flags{};
    Color ambient{}, diffuse{}, specular{}, emissive{};
    float power{}, alpha{100};
    std::int32_t albedoRaw{102400};
    std::array<std::uint32_t,8> skillBits{};
    EffectKind effect{EffectKind::None};
    std::uint64_t effectGeneration{};
    std::array<const Image*,2> skins{};
};
struct Camera {
    Vec3 position{}, angles{};
    Rect viewport{}; // Logical SDL pixels; zero width/height select the whole window.
    float horizontalFov{75}, referenceAspect{4.0f/3.0f}, nearClip{1}, farClip{10000};
    float pixelAspect{1}; // Legacy signed pixel-size ratio, negative flips vertically.
    bool widescreen{true}, clearDepth{true};
    bool perDrawOpacity{}, clearColor{true}, clearStencil{true};
    std::uint8_t opacityByte{255};
    std::uint32_t layers{~0u};
    float layer{};
    bool drawWorld{true};
    float alpha{1}, fogStart{}, fogEnd{};
    Color ambient{0.25f,0.25f,0.25f,1}, fogColor{}, background{};
    float nativeAmbient{}; // Public VIEW.AMBIENT; native scalar units, not RGB.
    bool environmentOverride{};
    bool foreground{};
    std::uint32_t sourceId{}, portalId{}, excludeEntityId{};
    Vec3 portalPosition{}, portalNormal{};
    bool portalTarget{}, portalClip{}, noCull{};
    bool noShadow{}, noEntities{};
    bool doorPortal{}; // Apply RenderFrame::door to this view (the game's main camera only).
};
struct NativeShadowState {
    bool caster{};
    float planeZ{};
    Vec3 negativeNormal{};
};
struct NativeEntityLightState {
    std::int16_t scalar{};
    std::array<std::int16_t,3> delta{}; // Native B/G/R cache deviations.
    std::array<std::int32_t,3> bias{}; // Native B/G/R per-draw additions.
    NativeShadowState shadow{};
};
struct DrawInstance {
    const Model* model{};
    std::uint32_t objectId{};
    std::uint32_t materialIndex{~0u};
    Vec3 position{}, angles{}, scale{1,1,1};
    std::uint32_t frame{}, nextFrame{}, layers{1};
    std::uint32_t spriteFrames{1}, skinIndex{};
    float interpolation{}, alpha{1}, ambient{}; // Public ENTITY.AMBIENT units, not a normalized multiplier.
    std::int16_t nativeLightScalar{};
    std::array<std::int16_t,3> nativeLightDelta{}; // Sampled B/G/R cache deviations.
    std::array<std::int32_t,3> nativeLightBias{}; // Per-draw signed B/G/R additions, not persisted sample cache.
    bool bright{}; // ENTITY.BRIGHT 0x200000; not additive particle BRIGHT 0x8000.
    float reflectivity{1};
    Color tint{1,1,1,1};
    std::span<const Vec3> positions, normals;
    std::uint32_t meshId{};
    std::uint64_t meshRevision{};
    std::span<const Vec3> nextPositions, nextNormals;
    std::uint32_t nextMeshId{};
    std::uint64_t nextMeshRevision{};
    bool viewSpace{};
    bool billboard{};
    bool visible{true}, unlit{}, doubleSided{}, additive{};
    bool translucent{}, overlay{}, flare{};
    bool invertFlare{};
    bool noFog{};
    bool nativeFloorLight{true}; // False only for source-proven A6 ENTITY/SKY declaration classes.
    std::array<std::int32_t,3> nativeSpecialColor{}; // Source A6 special PUBLIC B/G/R, already signed SAR10.
    NativeShadowState nativeShadow{};
    std::uint32_t shadowViewId{};
    bool shadowRequested{};
};
struct PortalSurface { Vec3 position{}, normal{}; };
// Native seamless door preview (docs/SEAMLESS_DOORS.md), not an A6 VIEW portal. world==nullptr: none this frame.
// For a doorPortal camera at C with sC=dot(C-center,normal)>0 and an open rect (halfWidth,halfHeight>0), let sP=dot(P-center,normal)
// for a fragment's world position P and X=C+(P-C)*sC/(sC-sP) the view line's crossing of the wall plane;
// inRect(X): |dot(X-center,right)|<=halfWidth && |dot(X-center,up)|<=halfHeight.
// Current-level fragments are discarded when sP<1 && sC-sP>0 && inRect(X) (cuts the doorway, including the door face itself).
// The door world (static mesh only, lightmapped, no entities/dynamic lights/ray queries; not in any acceleration structure)
// is drawn with transform(position,angles,{1,1,1}) and kept only where sP<-2 && inRect(X). sC<=0 or a closed rect: draw
// nothing extra and clip nothing, but keep the door world's GPU resources prepared.
struct DoorPortal {
    const World* world{};
    Vec3 position{}, angles{}; // door world -> current level, DrawInstance convention
    Vec3 center{}, normal{}, right{}, up{}; // doorway centre on the wall plane; unit normal points into the current room
    float halfWidth{}, halfHeight{};
};
struct RenderLight {
    Vec3 position{}; Color color{1,1,1,1}; float range{100}, intensity{1}; bool baked{};
    std::uint32_t type{1}; bool synthetic{};
    Vec3 direction{1,0,0}, attenuation{1.5f,0,0};
    float theta{30}, phi{45}, falloff{2};
};
struct Particle {
    Vec3 position{}; const Image* image{}; Color color{1,1,1,1};
    std::uint32_t materialIndex{~0u};
    std::int32_t alphaRaw{102400};
    float size{1}, angle{}; bool additive{}, translucent{};
    Vec3 velocity{};
    float scaleZ{1};
    bool beam{}, streak{}, flare{}, overlay{};
    bool invertFlare{};
};
struct Glyph { Rect pixels{}; Vec2 bearing{}; float advance{}; };
struct FontAtlas { Image image; std::array<Glyph,256> glyphs{}; float lineHeight{}, ascent{}; };
struct UICommand {
    enum class Kind { Image, Text, Solid } kind{Kind::Image};
    enum class TextureMode { Default, CursorKey16, CursorKey24 };
    Rect rect{}, uv{0,0,1,1}, clip{};
    const Image* image{};
    const FontAtlas* font{};
    std::string_view text;
    Color color{1,1,1,1};
    float layer{};
    float angle{}, scaleX{1}, scaleY{1}, advanceDelta{}, alphaRef{0.5f};
    bool additive{}, overlay{}, filter{};
    bool dynamicImage{};
    std::uint64_t imageSerial{};
    TextureMode textureMode{TextureMode::Default};
    bool cursorOverlay{}; // Final software cursor ordering, also for explicit-alpha unkeyed images.
};
struct ColorAdjustment {
    std::array<std::array<std::uint16_t,256>,3> gammaRamp = [] {
        std::array<std::array<std::uint16_t,256>,3> ramp{};
        for(auto& channel:ramp) for(unsigned i=0;i<256;++i) channel[i]=i*257;
        return ramp;
    }();
    bool enabled{};
};
struct SkinBinding { const Image* source{}; const Image* image{}; bool dynamic{}; std::uint64_t serial{}; bool nativeTextureAlpha{}; };
struct RenderFrame {
    const World* world{};
    std::span<const DrawInstance> instances;
    std::span<const DrawInstance> shadows;
    std::span<const Camera> cameras;
    std::span<const UICommand> ui;
    std::span<const Particle> particles;
    std::span<const RenderLight> lights;
    std::span<const SkinBinding> skinBindings;
    std::span<const ScriptMaterialState> materials;
    void* lightContext{};
    NativeEntityLightState (*prepareEntityLight)(void*,const DrawInstance&,const Camera&){};
    NativeShadowState (*cachedEntityShadow)(void*,const DrawInstance&){};
    Color screenColor{0,0,0,1};
    bool clearScreen{true};
    Color clearColor{0,0,0,1}, ambient{0.25f,0.25f,0.25f,1}, sunColor{0,0,0,1}, fogColor{0,0,0,1};
    Vec3 sunDirection{0,0,-1};
    float fogStart{}, fogEnd{};
    double animationTimeSeconds{};
    float turbulenceSpeed{1}, turbulenceRange{1};
    float skyCurve{1}, skyClip{};
    Vec2 skySpeed{1,1};
    bool skyBlend{true}; // Native SKY_BLEND default15 is tested as nonzero.
    double mipmapping{3}, anisotropy{}; // Native modes and nonzero enable gate; enabled anisotropy uses RenderOptions quality.
    bool bonusProfile{}, hardwareLighting{true}, renderEntities{true}, planarCapable{true};
    bool nativeFixedLightBank{true}; // A7 fixed128 vs linked/direct127 SUN albedo contract.
    bool shadowSortVertical{true};
    std::int32_t lightResRaw{}, tripleBufferRaw{}, shadowStencilRaw{};
    Vec2 shadowSun{};
    float shadowOffset{2}, shadowAlpha{50}, clipSize{0.5f};
    ColorAdjustment adjustment{};
    const Image* video{};
    std::uint64_t videoSerial{};
    Rect videoRect{};
    DoorPortal door{};
};
struct PresentationState {
    unsigned limit{1}, pending{};
    std::uint64_t submissions{}, waits{};
    int swapInterval{}, samples{};
};
class Renderer {
public:
    explicit Renderer(const RenderOptions& options = {});
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void poll(Input& input);
    void draw(const RenderFrame& frame);
    WindowMetrics metrics() const;
    void set_mode(int width, int height, WindowMode mode);
    void set_title(std::string_view title);
    void set_position(int x, int y);
    void set_mouse_mode(bool visible, bool relative = false);
    void rumble(int pad, float strength, std::uint32_t milliseconds = 250); // Game controller slot `pad`; empty slots and pads without rumble ignore it.
    void request_quit();
    void release_scene(const Model* keep = nullptr); // keep: this model's GPU mesh and textures survive (seamless door world)
    void purge_model(const Model* model);
    void purge_image(const Image* image);
    int shader_version() const;
    PresentationState presentation_state() const;
    static EffectKind identify_effect(std::string_view name, std::span<const std::byte> source);
    static Vec3 project(Vec3 position, const Camera& camera, WindowMetrics metrics);
    static std::optional<PortalSurface> portal_surface(const RenderFrame& frame, const Camera& camera, WindowMetrics metrics);
    static void boundary_check();
    // Actual hidden-window backend initialization; returns {available, detail}. Never throws for missing display/device.
    static std::pair<bool,std::string> probe(RendererBackend backend, bool rayTracing);
    std::string description() const; // Selected device/driver for logs and probing.
    const FontAtlas& font_atlas(std::string_view familyOrPath, int pixels, bool bold = false, bool italic = false);
    static Vec2 text_extent(const FontAtlas& font, std::string_view text);
    void capture(std::string_view path) const; // Final postprocessed frame as binary PPM; requires a completed draw at the current drawable size.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
