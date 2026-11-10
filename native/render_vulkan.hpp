#pragma once
#include "render.hpp"
#include "vulkan_loader.hpp"
#include "fsr_vk.hpp"
#include <array>
#include <chrono>
#include <memory>
#include <mutex>
#include <functional>
#include <vector>
namespace k22 {
class VulkanBackend {
public:
    struct State;
    VulkanBackend(SDL_Window*,const RenderOptions&);
    ~VulkanBackend();
    void draw(const RenderFrame&,WindowMetrics);
    void capture(std::string_view,WindowMetrics) const;
    void drain();
    void invalidate();
    void release_scene(std::span<const Model* const> keep = {});
    void purge_model(const Model*);
    void purge_image(const Image*);
    int shader_version() const;
    std::string description() const;
    PresentationState presentation_state() const;
    const RenderOptions& options() const;
    bool reconfigure(const RenderOptions&); // Renderer::reconfigure contract, `next` already admitted for this backend.
private:
    std::unique_ptr<State> state_;
};
namespace vk22 {
void require(VkResult,const char*);
// id: process-unique identity assigned by State::buffer/image (0: not created there). Unlike the Vulkan handle it is never reused.
struct Buffer { VkBuffer handle{}; VkDeviceMemory memory{}; VkDeviceSize size{}; void* mapped{}; VkDeviceAddress address{}; std::uint64_t id{}; };
struct Image { VkImage handle{}; VkDeviceMemory memory{}; VkImageView view{}; VkFormat format{}; std::uint32_t width{},height{},levels{1},layers{1}; VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED}; VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT}; std::uint64_t id{}; };
struct Target { Image color,depth,resolve; };
struct RayMaterial {
    VkImageView texture{}; VkSampler sampler{};
    std::array<float,4> uvTransform{1,1,0,0};
    Color tint{1,1,1,1}; float alpha{1};
    std::uint32_t first{},count{},flags{},spriteFrames{1},frame0{},frame1{};
    float interpolation{};
    Color effectAmbient{};
    std::array<float,4> skillMain{},skillExtra{};
    Vec3 effectSunDirection{},effectCameraPosition{};
    std::array<float,4> preparedAmbient{},preparedDiffuse{},preparedSpecular{},preparedEmissive{},ambientLight{};
    Vec3 infiniteViewer{};
    std::uint32_t lightCount{};
    std::int32_t selectedLight{-1};
    VkImageView normalTexture{}; VkSampler normalSampler{};
    std::array<float,4> normalUvTransform{1,1,0,0};
    // Ray-traced GI hit lighting only (zero/absent otherwise): baked lightmap and native ambient terms.
    VkImageView lightmap{}; VkSampler lightmapSampler{}; float surfaceAmbient{},materialAmbient{};
};
// Ray flags: overlay=1, flare=2, invertFlare=4, textureAlpha=8,
// doubleSided=16, clockwise=32, effect=64, effectSun=128, alphaTest=256, additive=512, blend=1024,
// explicitMaterial=2048, unlit=4096, hasLightmap=8192 (GI hit lighting; lightState.z = lightmap texture),
// opaqueTexels=16384 (the bound texture's alpha is 1 everywhere; rt_scene's opaque geometry).
struct Pose { Buffer vertices; std::uint32_t count{}; };
struct RayGeometry { const DrawInstance* draw{}; Pose* pose{}; Buffer* indices{}; std::span<const RayMaterial> materials; std::array<float,16> transform{}; std::array<float,4> clipPlane{}; };
struct Raster;
struct RayTracing;
struct RasterDelete { void operator()(Raster*) const; };
struct RayDelete { void operator()(RayTracing*) const; };
}
struct VulkanBackend::State {
    SDL_Window* window{}; RenderOptions options;
    VkInstance instance{}; VkSurfaceKHR surface{}; VkPhysicalDevice physical{}; VkDevice device{};
    VkPhysicalDeviceProperties properties{}; VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkQueue queue{}; std::uint32_t queueFamily{}; bool rayEnabled{};
    std::mutex queueMutex; // every queue submit/present/wait: the frame-generation present thread shares the queue.
    bool bufferAddressEnabled{},swapDirty{};
    // Device-level capabilities fixed at creation (reconfigure clamps or refuses against them): MSAA counts usable by
    // every target, tessellationShader supported / enabled on the device, frame generation found unsupported.
    VkSampleCountFlags sampleSupport{VK_SAMPLE_COUNT_1_BIT}; bool tessellationSupported{},tessellationEnabled{},generationUnsupported{};
    VkSwapchainKHR swapchain{}; VkFormat swapFormat{}; VkPresentModeKHR presentMode{VK_PRESENT_MODE_FIFO_KHR};
    // extent: display (swapchain, UI); render: background views, their effects and RT signals (FSR input size, else extent).
    VkExtent2D extent{},render{}; std::vector<VkImage> swapImages; std::vector<VkImageLayout> swapLayouts;
    std::vector<VkSemaphore> swapRendered;
    VkFormat depthFormat{VK_FORMAT_D24_UNORM_S8_UINT}; VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
    vk22::Target scene,alpha,portal,hdr; vk22::Image finalImage; // hdr: PBR background views (RGBA16F, scene samples, render size).
    std::unique_ptr<vk22::Fsr3> fsr; // fsr feature: upscales the composited render-size view to extent.
    // FSR 3.1.4 frame generation (fsr->generates(); docs/GRAPHICS_ROADMAP.md): per frame slot, generated is the frame
    // interpolated between the previous and this rendered frame with this frame's UI drawn over it (valid when
    // generatedValid), real a copy of this frame's finalImage (captures). The frame's own command buffer blits both into
    // two acquired swapchain images; the present thread (Presenter) only presents them: generated, then real half a
    // rendered-frame interval later. generationInterval: EMA of the render thread's period between submissions without its
    // wait on the present thread (generationWait), last measured at generationLast. frameTimeline: timeline semaphore
    // (frame generation devices) signalled with frameValue by each frame-generation submission; the present thread waits it.
    bool generating{},generatedValid{}; std::array<vk22::Image,2> generated,real;
    struct Presenter; std::unique_ptr<Presenter> presenter;
    double generationInterval{},generationWait{}; std::chrono::steady_clock::time_point generationLast{};
    VkSemaphore frameTimeline{}; std::uint64_t frameValue{};
    void drain_presents(std::uint64_t outstanding=0);
    bool retainedColorValid{},portalColorValid{},finalValid{};
    struct Slot { VkCommandPool pool{}; VkCommandBuffer command{}; VkFence fence{}; VkSemaphore acquired{},acquiredGenerated{},rendered{}; bool pending{}; std::vector<vk22::Buffer> garbageBuffers; std::vector<vk22::Image> garbageImages; VkQueryPool queries{}; std::vector<const char*> marks; };
    std::array<Slot,2> slots{}; unsigned slotIndex{},front{},pending{},limit{1}; std::uint64_t submissions{},waits{};
    VkCommandBuffer command{}; std::uint32_t acquiredIndex{}; int requestedInterval{-1};
    std::uint64_t finalSubmission{}; unsigned finalSlot{};
    // K22_GPU_TIMING=1: GPU timestamps between mark() calls plus CPU frame times, averaged to stderr every 300 frames.
    struct Timing { bool enabled{}; double period{}; std::vector<std::pair<const char*,double>> gpu; double gpuTotal{},cpuWait{},cpuRecord{},interval{},fence{},acquire{},present{}; std::uint64_t frames{}; std::int64_t last{}; } timing;
    void mark(const char* label);
    void account(Slot&);
    std::unique_ptr<vk22::Raster,vk22::RasterDelete> raster;
    std::unique_ptr<vk22::RayTracing,vk22::RayDelete> rays;
    std::uint64_t resourceIds{};
    // On-disk pipeline cache: the driver's VkPipelineCache data plus pipelineKeys, the raster's record of the pipelines it
    // created (opaque here), under $XDG_CACHE_HOME/kabus22, so a later run builds them at startup instead of at first
    // draw. K22_PIPELINE_CACHE=0: no file (the in-memory cache stays).
    VkPipelineCache pipelineCache{}; std::vector<std::uint8_t> pipelineKeys; bool pipelineCacheDirty{}; std::size_t pipelineCacheBytes{}; std::string pipelineCachePath;
    void load_pipeline_cache(); void save_pipeline_cache();
    // K22_RENDER_EVENTS=1: a stderr line for every frame that created resources (event(): pipelines, uploads, pool or
    // buffer growth) or whose recording stalled, with the CPU time between mark() calls, and every 600 frames the
    // averages per frame of scene binds (draws), culled world batches and descriptor sets written.
    struct Events { bool enabled{}; std::chrono::steady_clock::time_point last{}; std::vector<std::pair<const char*,double>> phases; std::string log; double typical{},recorded{}; std::uint64_t draws{},binds{},culled{},sets{}; } events;
    void event(const char* what,std::chrono::steady_clock::time_point since,std::string_view detail={});
    State(SDL_Window*,const RenderOptions&); ~State();
    vk22::Buffer buffer(VkDeviceSize,VkBufferUsageFlags,bool mapped=false,const void* initial=nullptr);
    void destroy(vk22::Buffer&); void destroy(vk22::Image&); void retire(vk22::Buffer&); void retire(vk22::Image&);
    vk22::Image image(std::uint32_t,std::uint32_t,VkFormat,VkImageUsageFlags,std::uint32_t levels=1,std::uint32_t layers=1,VkSampleCountFlagBits=VK_SAMPLE_COUNT_1_BIT,bool cube=false);
    void transition(vk22::Image&,VkImageLayout,VkPipelineStageFlags=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VkAccessFlags=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
    void upload(vk22::Image&,std::span<const std::byte>,std::span<const VkBufferImageCopy>,bool mipmaps=false);
    VkSampler sampler(bool linear,bool clamp,double mipmapping=0,float anisotropy=1);
    VkShaderModule shader(std::span<const std::uint32_t>);
    void begin(WindowMetrics,std::int32_t,bool); void finish(); void drain(); void invalidate(); bool reconfigure(RenderOptions);
    void ensure_targets(WindowMetrics); void ensure_auxiliary(vk22::Target&,VkExtent2D);
    void immediate(const std::function<void(VkCommandBuffer)>&);
    void readback(std::string_view,WindowMetrics);
};
namespace vk22 {
std::unique_ptr<Raster,RasterDelete> raster_create(VulkanBackend::State&);
void raster_destroy(Raster*);
void raster_draw(Raster&,const RenderFrame&,WindowMetrics);
void raster_release(Raster&,std::span<const Model* const> keep={}); void raster_purge_model(Raster&,const Model*); void raster_purge_image(Raster&,const k22::Image*);
void raster_reconfigure(Raster&); // device idle, State::options already replaced: drop pipelines, effect images and temporal history
int raster_shader_version(const Raster&);
std::unique_ptr<RayTracing,RayDelete> rt_create(VulkanBackend::State&);
void rt_destroy(RayTracing*);
Pose& rt_pose(RayTracing&,const DrawInstance&,std::span<const Vertex> expandedVertices={},std::span<const Vec3> expandedTangents={},bool needsTangents=false);
Pose* rt_previous_pose(RayTracing&,Pose& current);
void rt_poses_ready(RayTracing&);
void rt_scene(RayTracing&,std::span<const RayGeometry>);
VkDescriptorSetLayout rt_layout(const RayTracing&); VkDescriptorSet rt_set(const RayTracing&);
void rt_replace_texture(RayTracing&,VkImageView previous,VkImageView replacement);
void rt_release(RayTracing&,std::span<const Model* const> keep={}); void rt_purge_model(RayTracing&,const Model*); void rt_purge_image(RayTracing&,const k22::Image*);
}

}