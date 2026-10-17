#pragma once
#include "vulkan_loader.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace k22::vk22 {
// AMD FidelityFX Super Resolution 3.1.4 upscaler (FidelityFX SDK v1.1.4 Vulkan backend, third_party/FidelityFX-SDK;
// wrapper ported from IKV src/fsr_vk.cpp). Inputs: render-resolution tone-mapped colour, depth, unjittered UV-space
// motion (previousUV = UV + motion), non-inverted depth. Records compute work into the caller's command buffer only.
class Fsr3 {
public:
    // nullptr when the device can run FSR's shaders, else the missing capability.
    static const char* unsupported(VkPhysicalDevice);
    // Frame generation (optical flow + frame interpolation) on top of unsupported(): subgroup arithmetic, formatless
    // storage reads and storage/sampled/attachment/transfer use of the display-format interpolation source/output.
    static const char* generation_unsupported(VkPhysicalDevice,VkFormat display);
    static void enable_features(VkPhysicalDeviceFeatures&,bool generation=false);
    // quality: GraphicsFeatures::fsr (1 native AA, 2 quality, 3 balanced, 4 performance, 5 ultra performance).
    static VkExtent2D render_extent(VkExtent2D display,int quality);
    // generation != VK_FORMAT_UNDEFINED also creates the FSR 3.1.4 optical-flow and frame-interpolation contexts
    // (display size, that colour format), which reuse this upscaler's dilated depth/motion and previous depth.
    Fsr3(VkPhysicalDevice,VkDevice,VkExtent2D render,VkExtent2D display,VkFormat generation=VK_FORMAT_UNDEFINED);
    ~Fsr3();
    Fsr3(const Fsr3&)=delete;
    Fsr3& operator=(const Fsr3&)=delete;
    // FSR's Halton(2,3) sub-pixel offset in render pixels ([-.5,.5]) for a frame index.
    std::array<float,2> jitter(std::uint64_t frame) const;
    struct Image { VkImage image; VkFormat format; VkExtent2D extent; VkImageUsageFlags usage; };
    // Inputs and the (storage) output are in SHADER_READ_ONLY_OPTIMAL and are returned in that layout.
    struct Dispatch {
        VkCommandBuffer command;
        Image color,depth,motion,reactive,output; // reactive.image may be VK_NULL_HANDLE (none)
        std::array<float,2> jitter;
        float frameMs,sharpness,nearPlane,farPlane,fovY;
        bool reset;
    };
    void dispatch(const Dispatch&);
    // Reactive mask (R8, render size) from the opaque-only and the final render-size colour (FSR's autogen pass, sample
    // settings): translucent, particle and view-space pixels that have no motion vectors lean on the current frame.
    void reactive(VkCommandBuffer,const Image& opaque,const Image& color,const Image& output);
    // After dispatch() in the same command buffer: writes the frame halfway between the previous and this source
    // (display size, the generation format, HUD-less, SHADER_READ_ONLY_OPTIMAL) into output (GENERAL, storage), recording
    // the copy of source the next call interpolates from. reset: no valid previous source/history; frame: +1 per call.
    struct Interpolation {
        VkCommandBuffer command;
        Image source,output;
        float frameMs,nearPlane,farPlane,fovY;
        bool reset;
        std::uint64_t frame;
    };
    void interpolate(const Interpolation&);
    bool generates() const;
private:
    struct Context;
    std::unique_ptr<Context> context_;
    std::vector<unsigned char> scratch_,generationScratch_;
    VkExtent2D render_{},display_{};
};
}
