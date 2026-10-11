#pragma once
#include "render.hpp"
#include "vulkan_loader.hpp"
#include "fsr_vk.hpp"
#include <array>
#include <memory>
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
    void release_scene();
    void purge_model(const Model*);
    void purge_image(const Image*);
    int shader_version() const;
    std::string description() const;
    PresentationState presentation_state() const;
private:
    std::unique_ptr<State> state_;
};
namespace vk22 {
void require(VkResult,const char*);
struct Buffer { VkBuffer handle{}; VkDeviceMemory memory{}; VkDeviceSize size{}; void* mapped{}; VkDeviceAddress address{}; };
struct Image { VkImage handle{}; VkDeviceMemory memory{}; VkImageView view{}; VkFormat format{}; std::uint32_t width{},height{},levels{1},layers{1}; VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED}; VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT}; };
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
// explicitMaterial=2048, unlit=4096, hasLightmap=8192 (GI hit lighting; lightState.z = lightmap texture).
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
    bool bufferAddressEnabled{},swapDirty{};
    VkSwapchainKHR swapchain{}; VkFormat swapFormat{}; VkPresentModeKHR presentMode{VK_PRESENT_MODE_FIFO_KHR};
    // extent: display (swapchain, UI); render: background views, their effects and RT signals (FSR input size, else extent).
    VkExtent2D extent{},render{}; std::vector<VkImage> swapImages; std::vector<VkImageLayout> swapLayouts;
    std::vector<VkSemaphore> swapRendered;
    VkFormat depthFormat{VK_FORMAT_D24_UNORM_S8_UINT}; VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
    vk22::Target scene,alpha,portal,hdr; vk22::Image finalImage; // hdr: PBR background views (RGBA16F, scene samples, render size).
    std::unique_ptr<vk22::Fsr3> fsr; // fsr feature: upscales the composited render-size view to extent.
    bool retainedColorValid{},portalColorValid{},finalValid{};
    struct Slot { VkCommandPool pool{}; VkCommandBuffer command{}; VkFence fence{}; VkSemaphore acquired{},rendered{}; bool pending{}; std::vector<vk22::Buffer> garbageBuffers; std::vector<vk22::Image> garbageImages; VkQueryPool queries{}; std::vector<const char*> marks; };
    std::array<Slot,2> slots{}; unsigned slotIndex{},front{},pending{},limit{1}; std::uint64_t submissions{},waits{};
    VkCommandBuffer command{}; std::uint32_t acquiredIndex{}; int requestedInterval{-1};
    std::uint64_t finalSubmission{}; unsigned finalSlot{};
    // K22_GPU_TIMING=1: GPU timestamps between mark() calls plus CPU frame times, averaged to stderr every 300 frames.
    struct Timing { bool enabled{}; double period{}; std::vector<std::pair<const char*,double>> gpu; double gpuTotal{},cpuWait{},cpuRecord{},interval{},fence{},acquire{},present{}; std::uint64_t frames{}; std::int64_t last{}; } timing;
    void mark(const char* label);
    void account(Slot&);
    std::unique_ptr<vk22::Raster,vk22::RasterDelete> raster;
    std::unique_ptr<vk22::RayTracing,vk22::RayDelete> rays;
    State(SDL_Window*,const RenderOptions&); ~State();
    vk22::Buffer buffer(VkDeviceSize,VkBufferUsageFlags,bool mapped=false,const void* initial=nullptr);
    void destroy(vk22::Buffer&); void destroy(vk22::Image&); void retire(vk22::Buffer&); void retire(vk22::Image&);
    vk22::Image image(std::uint32_t,std::uint32_t,VkFormat,VkImageUsageFlags,std::uint32_t levels=1,std::uint32_t layers=1,VkSampleCountFlagBits=VK_SAMPLE_COUNT_1_BIT,bool cube=false);
    void transition(vk22::Image&,VkImageLayout,VkPipelineStageFlags=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VkAccessFlags=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
    void upload(vk22::Image&,std::span<const std::byte>,std::span<const VkBufferImageCopy>,bool mipmaps=false);
    VkSampler sampler(bool linear,bool clamp,double mipmapping=0,float anisotropy=1);
    VkShaderModule shader(std::span<const std::uint32_t>);
    void begin(WindowMetrics,std::int32_t,bool); void finish(); void drain(); void invalidate();
    void ensure_targets(WindowMetrics); void ensure_auxiliary(vk22::Target&,VkExtent2D);
    void immediate(const std::function<void(VkCommandBuffer)>&);
    void readback(std::string_view,WindowMetrics);
};
namespace vk22 {
std::unique_ptr<Raster,RasterDelete> raster_create(VulkanBackend::State&);
void raster_destroy(Raster*);
void raster_draw(Raster&,const RenderFrame&,WindowMetrics);
void raster_release(Raster&); void raster_purge_model(Raster&,const Model*); void raster_purge_image(Raster&,const k22::Image*);
int raster_shader_version(const Raster&);
std::unique_ptr<RayTracing,RayDelete> rt_create(VulkanBackend::State&);
void rt_destroy(RayTracing*);
Pose& rt_pose(RayTracing&,const DrawInstance&,std::span<const Vertex> expandedVertices={},std::span<const Vec3> expandedTangents={},bool needsTangents=false);
Pose* rt_previous_pose(RayTracing&,Pose& current);
// instanceObjects (optional): receives the DrawInstance::objectId of every TLAS instance in custom-index order.
void rt_scene(RayTracing&,std::span<const RayGeometry>,std::vector<std::uint32_t>* instanceObjects=nullptr);
VkDescriptorSetLayout rt_layout(const RayTracing&); VkDescriptorSet rt_set(const RayTracing&);
void rt_replace_texture(RayTracing&,VkImageView previous,VkImageView replacement);
void rt_release(RayTracing&); void rt_purge_model(RayTracing&,const Model*); void rt_purge_image(RayTracing&,const k22::Image*);
}

}