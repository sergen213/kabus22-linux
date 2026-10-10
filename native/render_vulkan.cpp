#include "vulkan_loader.hpp"
#include "render_vulkan.hpp"
#include "render_policy.hpp"
#include <SDL_vulkan.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace k22 {
namespace vk22 {
void require(VkResult result,const char* operation) {
    if(result!=VK_SUCCESS) throw std::runtime_error(std::string(operation)+" failed (Vulkan "+std::to_string(result)+")");
}
}
namespace {
using vk22::require;
std::uint32_t memory_type(const VulkanBackend::State& s,std::uint32_t bits,VkMemoryPropertyFlags flags) {
    for(std::uint32_t i=0;i<s.memoryProperties.memoryTypeCount;++i)
        if((bits&(1u<<i))&&(s.memoryProperties.memoryTypes[i].propertyFlags&flags)==flags) return i;
    throw std::runtime_error("Vulkan device has no compatible memory type");
}
void garbage(VulkanBackend::State& s,VulkanBackend::State::Slot& slot) {
    for(auto& b:slot.garbageBuffers) s.destroy(b);
    for(auto& i:slot.garbageImages) s.destroy(i);
    slot.garbageBuffers.clear(); slot.garbageImages.clear();
}
void retire_slot(VulkanBackend::State& s,unsigned index,bool account=true) {
    auto& slot=s.slots[index];
    if(!slot.pending) return;
    require(vkWaitForFences(s.device,1,&slot.fence,VK_TRUE,UINT64_MAX),"Waiting for Vulkan frame");
    slot.pending=false; --s.pending;
    if(account) ++s.waits;
    garbage(s,slot);
}
void destroy_target(VulkanBackend::State& s,vk22::Target& t) {
    s.destroy(t.color); s.destroy(t.depth); s.destroy(t.resolve);
}
void destroy_swapchain(VulkanBackend::State& s) {
    for(auto semaphore:s.swapRendered) vkDestroySemaphore(s.device,semaphore,nullptr);
    s.swapRendered.clear(); s.swapImages.clear(); s.swapLayouts.clear();
    if(s.swapchain) vkDestroySwapchainKHR(s.device,s.swapchain,nullptr);
    s.swapchain=VK_NULL_HANDLE;
}
void cleanup(VulkanBackend::State& s) {
    if(s.device) {
        vkDeviceWaitIdle(s.device);
        s.raster.reset(); s.rays.reset();
        destroy_target(s,s.scene); destroy_target(s,s.alpha); destroy_target(s,s.portal); destroy_target(s,s.hdr); s.destroy(s.finalImage);
        for(auto& slot:s.slots) {
            garbage(s,slot);
            if(slot.fence) vkDestroyFence(s.device,slot.fence,nullptr);
            if(slot.acquired) vkDestroySemaphore(s.device,slot.acquired,nullptr);
            if(slot.rendered) vkDestroySemaphore(s.device,slot.rendered,nullptr);
            if(slot.pool) vkDestroyCommandPool(s.device,slot.pool,nullptr);
        }
        destroy_swapchain(s); vkDestroyDevice(s.device,nullptr); s.device=VK_NULL_HANDLE;
    }
    if(s.surface) vkDestroySurfaceKHR(s.instance,s.surface,nullptr);
    if(s.instance) vkDestroyInstance(s.instance,nullptr);
    s.surface=VK_NULL_HANDLE; s.instance=VK_NULL_HANDLE;
}
bool extension(std::span<const VkExtensionProperties> extensions,const char* name) {
    return std::any_of(extensions.begin(),extensions.end(),[&](const auto& e){return std::strcmp(e.extensionName,name)==0;});
}
VkImageAspectFlags aspect(VkFormat format) {
    switch(format) {
        case VK_FORMAT_D24_UNORM_S8_UINT: case VK_FORMAT_D32_SFLOAT_S8_UINT:return VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;
        case VK_FORMAT_D32_SFLOAT:return VK_IMAGE_ASPECT_DEPTH_BIT;
        default:return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}
void barrier(VkCommandBuffer command,VkImage image,VkImageAspectFlags mask,std::uint32_t levels,std::uint32_t layers,VkImageLayout oldLayout,VkImageLayout newLayout,VkPipelineStageFlags2 stage,VkAccessFlags2 access) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask=oldLayout==VK_IMAGE_LAYOUT_UNDEFINED?VK_PIPELINE_STAGE_2_NONE:VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask=oldLayout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_2_MEMORY_READ_BIT|VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.dstStageMask=stage; b.dstAccessMask=access; b.oldLayout=oldLayout; b.newLayout=newLayout;
    b.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.image=image; b.subresourceRange={mask,0,levels,0,layers};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO}; dependency.imageMemoryBarrierCount=1; dependency.pImageMemoryBarriers=&b;
    vkCmdPipelineBarrier2(command,&dependency);
}
void create_swapchain(VulkanBackend::State& s,WindowMetrics metrics) {
    s.drain();
    // Present operations are not covered by submission fences; only swapchain replacement waits the queue.
    if(s.swapchain) require(vkQueueWaitIdle(s.queue),"Retiring Vulkan presentation");
    VkSurfaceCapabilitiesKHR caps{}; require(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(s.physical,s.surface,&caps),"Querying Vulkan surface");
    std::uint32_t count{}; require(vkGetPhysicalDeviceSurfaceFormatsKHR(s.physical,s.surface,&count,nullptr),"Querying Vulkan surface formats");
    std::vector<VkSurfaceFormatKHR> formats(count); require(vkGetPhysicalDeviceSurfaceFormatsKHR(s.physical,s.surface,&count,formats.data()),"Reading Vulkan surface formats");
    auto selected=std::find_if(formats.begin(),formats.end(),[](const auto& f){return (f.format==VK_FORMAT_B8G8R8A8_UNORM||f.format==VK_FORMAT_R8G8B8A8_UNORM)&&f.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;});
    VkSurfaceFormatKHR format{};
    if(formats.size()==1&&formats[0].format==VK_FORMAT_UNDEFINED) format={VK_FORMAT_B8G8R8A8_UNORM,formats[0].colorSpace};
    else if(selected!=formats.end()) format=*selected;
    else throw std::runtime_error(std::string(s.properties.deviceName)+": Vulkan surface lacks numeric RGBA8/BGRA8 UNORM presentation");
    VkFormatProperties props{}; vkGetPhysicalDeviceFormatProperties(s.physical,format.format,&props);
    if(!(caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT)||!(props.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT))
        throw std::runtime_error("Vulkan surface does not support final-image transfer presentation");
    require(vkGetPhysicalDeviceSurfacePresentModesKHR(s.physical,s.surface,&count,nullptr),"Querying Vulkan present modes");
    std::vector<VkPresentModeKHR> modes(count); require(vkGetPhysicalDeviceSurfacePresentModesKHR(s.physical,s.surface,&count,modes.data()),"Reading Vulkan present modes");
    auto mode=VK_PRESENT_MODE_FIFO_KHR;
    if(!s.requestedInterval) {
        if(std::find(modes.begin(),modes.end(),VK_PRESENT_MODE_IMMEDIATE_KHR)!=modes.end()) mode=VK_PRESENT_MODE_IMMEDIATE_KHR;
        else if(std::find(modes.begin(),modes.end(),VK_PRESENT_MODE_MAILBOX_KHR)!=modes.end()) mode=VK_PRESENT_MODE_MAILBOX_KHR;
    }
    VkExtent2D size=caps.currentExtent;
    if(size.width==UINT32_MAX) size={std::clamp(static_cast<std::uint32_t>(metrics.drawableWidth),caps.minImageExtent.width,caps.maxImageExtent.width),std::clamp(static_cast<std::uint32_t>(metrics.drawableHeight),caps.minImageExtent.height,caps.maxImageExtent.height)};
    if(!size.width||!size.height) {s.swapDirty=true; return;}
    auto images=std::max(caps.minImageCount,3u); if(caps.maxImageCount) images=std::min(images,caps.maxImageCount);
    VkCompositeAlphaFlagBitsKHR composite=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if(!(caps.supportedCompositeAlpha&composite)) for(auto bit:{VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) if(caps.supportedCompositeAlpha&bit){composite=bit;break;}
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR}; info.surface=s.surface; info.minImageCount=images; info.imageFormat=format.format; info.imageColorSpace=format.colorSpace; info.imageExtent=size; info.imageArrayLayers=1; info.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT; info.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE; info.preTransform=caps.currentTransform; info.compositeAlpha=composite; info.presentMode=mode; info.clipped=VK_TRUE; info.oldSwapchain=s.swapchain;
    VkSwapchainKHR replacement{}; require(vkCreateSwapchainKHR(s.device,&info,nullptr,&replacement),"Creating Vulkan swapchain");
    destroy_swapchain(s); s.swapchain=replacement; s.swapFormat=format.format; s.presentMode=mode;
    require(vkGetSwapchainImagesKHR(s.device,s.swapchain,&count,nullptr),"Querying Vulkan swapchain images"); s.swapImages.resize(count);
    require(vkGetSwapchainImagesKHR(s.device,s.swapchain,&count,s.swapImages.data()),"Reading Vulkan swapchain images"); s.swapLayouts.assign(count,VK_IMAGE_LAYOUT_UNDEFINED); s.swapRendered.resize(count);
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; for(auto& handle:s.swapRendered) require(vkCreateSemaphore(s.device,&semaphore,nullptr,&handle),"Creating Vulkan present semaphore");
    if(s.extent.width!=size.width||s.extent.height!=size.height) {
        destroy_target(s,s.scene); destroy_target(s,s.alpha); destroy_target(s,s.portal); destroy_target(s,s.hdr); s.destroy(s.finalImage);
        s.retainedColorValid=s.portalColorValid=s.finalValid=false;
    }
    s.extent=size; s.swapDirty=false;
}
}

VulkanBackend::State::State(SDL_Window* w,const RenderOptions& o):window(w),options(o) {
    try {
        vk22::load_vulkan();
        unsigned count{};
        if(!SDL_Vulkan_GetInstanceExtensions(window,&count,nullptr)) throw std::runtime_error(SDL_GetError());
        std::vector<const char*> extensions(count);
        if(!SDL_Vulkan_GetInstanceExtensions(window,&count,extensions.data())) throw std::runtime_error(SDL_GetError());
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO}; application.pApplicationName="Kabus 22"; application.apiVersion=VK_API_VERSION_1_3;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo=&application; info.enabledExtensionCount=count; info.ppEnabledExtensionNames=extensions.data();
        require(vkCreateInstance(&info,nullptr,&instance),"Creating Vulkan 1.3 instance");
        if(!SDL_Vulkan_CreateSurface(window,instance,&surface)) throw std::runtime_error(SDL_GetError());
        require(vkEnumeratePhysicalDevices(instance,&count,nullptr),"Enumerating Vulkan devices"); std::vector<VkPhysicalDevice> devices(count);
        require(vkEnumeratePhysicalDevices(instance,&count,devices.data()),"Reading Vulkan devices");
        std::string failures; int best=-1; bool tessellation{};
        for(auto candidate:devices) {
            VkPhysicalDeviceProperties p{}; vkGetPhysicalDeviceProperties(candidate,&p);
            std::uint32_t n{}; require(vkEnumerateDeviceExtensionProperties(candidate,nullptr,&n,nullptr),"Querying Vulkan device extensions"); std::vector<VkExtensionProperties> ext(n); require(vkEnumerateDeviceExtensionProperties(candidate,nullptr,&n,ext.data()),"Reading Vulkan device extensions");
            VkPhysicalDeviceRayQueryFeaturesKHR ray{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
            VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR}; acceleration.pNext=&ray;
            VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES}; f13.pNext=&acceleration;
            VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES}; f12.pNext=&f13;
            VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; features.pNext=&f12; vkGetPhysicalDeviceFeatures2(candidate,&features);
            std::string missing;
            if(p.apiVersion<VK_API_VERSION_1_3) missing="Vulkan 1.3";
            else if(!extension(ext,VK_KHR_SWAPCHAIN_EXTENSION_NAME)) missing="KHR_swapchain";
            else if(!features.features.geometryShader||!features.features.shaderClipDistance||!features.features.samplerAnisotropy) missing="geometryShader/clipDistance/samplerAnisotropy";
            else if(!f13.dynamicRendering||!f13.synchronization2) missing="dynamicRendering/synchronization2";
            else if(o.rayTracing&&(!extension(ext,VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)||!extension(ext,VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)||!extension(ext,VK_KHR_RAY_QUERY_EXTENSION_NAME)||!acceleration.accelerationStructure||!ray.rayQuery||!f12.bufferDeviceAddress||!f12.runtimeDescriptorArray||!f12.shaderSampledImageArrayNonUniformIndexing||!features.features.shaderSampledImageArrayDynamicIndexing||!features.features.shaderInt64)) missing="KHR ray-query acceleration structures, buffer addresses, descriptor indexing or shaderInt64";
            vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,nullptr); std::vector<VkQueueFamilyProperties> queues(n); vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,queues.data());
            unsigned family=UINT32_MAX;
            for(unsigned i=0;i<n;++i) { VkBool32 present{}; require(vkGetPhysicalDeviceSurfaceSupportKHR(candidate,i,surface,&present),"Querying Vulkan queue presentation"); if(present&&(queues[i].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)){family=i;break;} }
            if(family==UINT32_MAX) missing="combined graphics/compute/present queue";
            if(!missing.empty()) { failures+=std::string(p.deviceName)+": unsupported "+missing+"; "; continue; }
            int score=p.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU?3:p.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU?2:1;
            if(score>best) {physical=candidate; properties=p; queueFamily=family; bufferAddressEnabled=f12.bufferDeviceAddress; tessellation=features.features.tessellationShader; best=score;}
        }
        if(!physical) throw std::runtime_error("No suitable Vulkan device: "+failures+(o.rayTracing?"select --ray-tracing off for raster-only rendering":""));
        vkGetPhysicalDeviceMemoryProperties(physical,&memoryProperties); rayEnabled=o.rayTracing;
        VkPhysicalDeviceRayQueryFeaturesKHR ray{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR}; ray.rayQuery=rayEnabled;
        VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR}; acceleration.accelerationStructure=rayEnabled; acceleration.pNext=rayEnabled?&ray:nullptr;
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES}; f13.dynamicRendering=VK_TRUE; f13.synchronization2=VK_TRUE; f13.pNext=rayEnabled?&acceleration:nullptr;
        VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES}; f12.bufferDeviceAddress=bufferAddressEnabled; f12.runtimeDescriptorArray=rayEnabled; f12.shaderSampledImageArrayNonUniformIndexing=rayEnabled; f12.pNext=&f13;
        // tessellation is optional: an unsupported request is dropped with a notice instead of failing device selection.
        if(options.features.tessellation&&!tessellation){std::fprintf(stderr,"kabus22: graphics: tessellation unsupported by %s; disabling it\n",properties.deviceName);options.features.tessellation=false;}
        VkPhysicalDeviceFeatures features{}; features.geometryShader=VK_TRUE; features.shaderClipDistance=VK_TRUE; features.samplerAnisotropy=VK_TRUE; features.shaderInt64=rayEnabled; features.shaderSampledImageArrayDynamicIndexing=rayEnabled; features.tessellationShader=options.features.tessellation;
        const float priority=1; VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; queueInfo.queueFamilyIndex=queueFamily; queueInfo.queueCount=1; queueInfo.pQueuePriorities=&priority;
        std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        if(rayEnabled) deviceExtensions.insert(deviceExtensions.end(),{VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,VK_KHR_RAY_QUERY_EXTENSION_NAME});
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; deviceInfo.pNext=&f12; deviceInfo.pEnabledFeatures=&features; deviceInfo.queueCreateInfoCount=1; deviceInfo.pQueueCreateInfos=&queueInfo; deviceInfo.enabledExtensionCount=static_cast<std::uint32_t>(deviceExtensions.size()); deviceInfo.ppEnabledExtensionNames=deviceExtensions.data();
        require(vkCreateDevice(physical,&deviceInfo,nullptr,&device),"Creating Vulkan device"); vkGetDeviceQueue(device,queueFamily,0,&queue);
        for(auto format:{VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D32_SFLOAT_S8_UINT}) {VkFormatProperties p{}; vkGetPhysicalDeviceFormatProperties(physical,format,&p); if(p.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT){depthFormat=format;break;} }
        VkImageFormatProperties colorProperties{},depthProperties{};
        VkFormatProperties numericColor{};
        vkGetPhysicalDeviceFormatProperties(physical,VK_FORMAT_R8G8B8A8_UNORM,&numericColor);
        if(!(numericColor.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT))
            throw std::runtime_error(std::string(properties.deviceName)+": RGBA8 UNORM final-image blits unsupported");
        require(vkGetPhysicalDeviceImageFormatProperties(physical,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,0,&colorProperties),"Querying Vulkan scene format");
        require(vkGetPhysicalDeviceImageFormatProperties(physical,depthFormat,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,0,&depthProperties),"Querying Vulkan depth/stencil format");
        auto supported=properties.limits.framebufferColorSampleCounts&properties.limits.framebufferDepthSampleCounts&properties.limits.framebufferStencilSampleCounts&colorProperties.sampleCounts&depthProperties.sampleCounts;
        for(auto sample:{VK_SAMPLE_COUNT_64_BIT,VK_SAMPLE_COUNT_32_BIT,VK_SAMPLE_COUNT_16_BIT,VK_SAMPLE_COUNT_8_BIT,VK_SAMPLE_COUNT_4_BIT,VK_SAMPLE_COUNT_2_BIT,VK_SAMPLE_COUNT_1_BIT}) if(static_cast<int>(sample)<=std::max(o.samples,1)&&(supported&sample)){samples=sample;break;}
        if(static_cast<int>(samples)<o.samples) std::fprintf(stderr,"kabus22: graphics: MSAA %d unsupported, using %d\n",o.samples,static_cast<int>(samples));
        if(o.anisotropy>properties.limits.maxSamplerAnisotropy) std::fprintf(stderr,"kabus22: graphics: anisotropy %g unsupported, using %g\n",double(o.anisotropy),double(properties.limits.maxSamplerAnisotropy));
        for(auto& slot:slots) {
            VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pool.queueFamilyIndex=queueFamily;
            require(vkCreateCommandPool(device,&pool,nullptr,&slot.pool),"Creating Vulkan frame pool");
            VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; allocation.commandPool=slot.pool; allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocation.commandBufferCount=1;
            require(vkAllocateCommandBuffers(device,&allocation,&slot.command),"Allocating Vulkan frame command");
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence.flags=VK_FENCE_CREATE_SIGNALED_BIT; require(vkCreateFence(device,&fence,nullptr,&slot.fence),"Creating Vulkan frame fence");
            VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; require(vkCreateSemaphore(device,&semaphore,nullptr,&slot.acquired),"Creating Vulkan acquisition semaphore");
        }
        requestedInterval=options.vsync?1:0;
        WindowMetrics metrics{};
        SDL_GetWindowSize(window,&metrics.width,&metrics.height);
        SDL_Vulkan_GetDrawableSize(window,&metrics.drawableWidth,&metrics.drawableHeight);
        ensure_targets(metrics);
        rays=vk22::rt_create(*this); raster=vk22::raster_create(*this);
    } catch(...) {cleanup(*this);throw;}
}
VulkanBackend::State::~State(){cleanup(*this);}

vk22::Buffer VulkanBackend::State::buffer(VkDeviceSize size,VkBufferUsageFlags usage,bool mapped,const void* initial) {
    if(!size) throw std::runtime_error("Vulkan buffer cannot have zero size");
    if((usage&VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)&&!bufferAddressEnabled) throw std::runtime_error(std::string(properties.deviceName)+": buffer device addresses unavailable");
    vk22::Buffer b{}; b.size=size;
    try {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size=size; info.usage=usage|(!mapped&&initial?VK_BUFFER_USAGE_TRANSFER_DST_BIT:0); info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        require(vkCreateBuffer(device,&info,nullptr,&b.handle),"Creating Vulkan buffer");
        VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(device,b.handle,&requirements);
        VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO}; flags.flags=(usage&VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)?VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT:0;
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.pNext=flags.flags?&flags:nullptr; allocation.allocationSize=requirements.size; allocation.memoryTypeIndex=memory_type(*this,requirements.memoryTypeBits,mapped?VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        require(vkAllocateMemory(device,&allocation,nullptr,&b.memory),"Allocating Vulkan buffer memory"); require(vkBindBufferMemory(device,b.handle,b.memory,0),"Binding Vulkan buffer memory");
        if(mapped) {require(vkMapMemory(device,b.memory,0,size,0,&b.mapped),"Mapping Vulkan buffer"); if(initial) std::memcpy(b.mapped,initial,static_cast<std::size_t>(size));}
        else if(initial) {
            auto staging=buffer(size,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,true,initial);
            try {
                auto copy=[&](VkCommandBuffer cmd) {
                    VkBufferCopy region{0,0,size};
                    vkCmdCopyBuffer(cmd,staging.handle,b.handle,1,&region);
                    VkMemoryBarrier2 barrierInfo{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
                    barrierInfo.srcStageMask=VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                    barrierInfo.srcAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
                    barrierInfo.dstStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                    barrierInfo.dstAccessMask=VK_ACCESS_2_MEMORY_READ_BIT;
                    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                    dependency.memoryBarrierCount=1; dependency.pMemoryBarriers=&barrierInfo;
                    vkCmdPipelineBarrier2(cmd,&dependency);
                };
                if(command) { copy(command); retire(staging); }
                else { immediate(copy); destroy(staging); }
            } catch(...) { destroy(staging); throw; }
        }
        if(usage&VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT){VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};address.buffer=b.handle;b.address=vkGetBufferDeviceAddress(device,&address);}
        return b;
    }catch(...){destroy(b);throw;}
}
void VulkanBackend::State::destroy(vk22::Buffer& b){if(b.mapped)vkUnmapMemory(device,b.memory);if(b.handle)vkDestroyBuffer(device,b.handle,nullptr);if(b.memory)vkFreeMemory(device,b.memory,nullptr);b={};}
void VulkanBackend::State::destroy(vk22::Image& i){if(i.view)vkDestroyImageView(device,i.view,nullptr);if(i.handle)vkDestroyImage(device,i.handle,nullptr);if(i.memory)vkFreeMemory(device,i.memory,nullptr);i={};}
void VulkanBackend::State::retire(vk22::Buffer& b){if(!b.handle)return;if(command){slots[slotIndex].garbageBuffers.push_back(b);b={};}else{drain();destroy(b);}}
void VulkanBackend::State::retire(vk22::Image& i){if(!i.handle)return;if(command){slots[slotIndex].garbageImages.push_back(i);i={};}else{drain();destroy(i);}}
vk22::Image VulkanBackend::State::image(std::uint32_t width,std::uint32_t height,VkFormat format,VkImageUsageFlags usage,std::uint32_t levels,std::uint32_t layers,VkSampleCountFlagBits sampleCount,bool cube) {
    if(!width||!height||!levels||!layers)throw std::runtime_error("Invalid Vulkan image dimensions");
    vk22::Image i{};i.width=width;i.height=height;i.format=format;i.levels=levels;i.layers=layers;i.samples=sampleCount;
    try {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; info.flags=cube?VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT:0;info.imageType=VK_IMAGE_TYPE_2D;info.format=format;info.extent={width,height,1};info.mipLevels=levels;info.arrayLayers=layers;info.samples=sampleCount;info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=usage;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        require(vkCreateImage(device,&info,nullptr,&i.handle),"Creating Vulkan image"); VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(device,i.handle,&requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=memory_type(*this,requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);require(vkAllocateMemory(device,&allocation,nullptr,&i.memory),"Allocating Vulkan image memory");require(vkBindImageMemory(device,i.handle,i.memory,0),"Binding Vulkan image memory");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=i.handle;view.viewType=cube?VK_IMAGE_VIEW_TYPE_CUBE:layers>1?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D;view.format=format;view.subresourceRange={aspect(format),0,levels,0,layers};require(vkCreateImageView(device,&view,nullptr,&i.view),"Creating Vulkan image view");return i;
    }catch(...){destroy(i);throw;}
}
void VulkanBackend::State::transition(vk22::Image& i,VkImageLayout layout,VkPipelineStageFlags stage,VkAccessFlags access){if(!command)throw std::runtime_error("Vulkan image transition outside command recording");barrier(command,i.handle,aspect(i.format),i.levels,i.layers,i.layout,layout,stage,access);i.layout=layout;}
void VulkanBackend::State::upload(vk22::Image& i,std::span<const std::byte> data,std::span<const VkBufferImageCopy> regions,bool mipmaps) {
    if(data.empty()||regions.empty())throw std::runtime_error("Empty Vulkan image upload");
    auto staging=buffer(data.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,true,data.data());
    try {
        auto record=[&](VkCommandBuffer cmd){
            barrier(cmd,i.handle,aspect(i.format),i.levels,i.layers,i.layout,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT);
            vkCmdCopyBufferToImage(cmd,staging.handle,i.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,static_cast<std::uint32_t>(regions.size()),regions.data());
            if(mipmaps&&i.levels>1){
                VkFormatProperties properties{};vkGetPhysicalDeviceFormatProperties(physical,i.format,&properties);
                if((properties.optimalTilingFeatures&(VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))!=(VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))throw std::runtime_error("Vulkan texture format cannot generate linear mipmaps");
                std::int32_t width=static_cast<std::int32_t>(i.width),height=static_cast<std::int32_t>(i.height);
                for(std::uint32_t level=1;level<i.levels;++level){
                    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};b.srcStageMask=VK_PIPELINE_STAGE_2_TRANSFER_BIT;b.srcAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;b.dstStageMask=VK_PIPELINE_STAGE_2_TRANSFER_BIT;b.dstAccessMask=VK_ACCESS_2_TRANSFER_READ_BIT;b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=i.handle;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,level-1,1,0,i.layers};VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dependency.imageMemoryBarrierCount=1;dependency.pImageMemoryBarriers=&b;vkCmdPipelineBarrier2(cmd,&dependency);
                    VkImageBlit blit{};blit.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level-1,0,i.layers};blit.srcOffsets[1]={width,height,1};blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,i.layers};blit.dstOffsets[1]={std::max(width/2,1),std::max(height/2,1),1};vkCmdBlitImage(cmd,i.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,i.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_LINEAR);
                    b.srcAccessMask=VK_ACCESS_2_TRANSFER_READ_BIT;b.dstStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;b.dstAccessMask=VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;vkCmdPipelineBarrier2(cmd,&dependency);width=std::max(width/2,1);height=std::max(height/2,1);
                }
                VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};b.srcStageMask=VK_PIPELINE_STAGE_2_TRANSFER_BIT;b.srcAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;b.dstStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;b.dstAccessMask=VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=i.handle;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,i.levels-1,1,0,i.layers};VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dependency.imageMemoryBarrierCount=1;dependency.pImageMemoryBarriers=&b;vkCmdPipelineBarrier2(cmd,&dependency);
            }else barrier(cmd,i.handle,aspect(i.format),i.levels,i.layers,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            i.layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        };
        if(command){record(command);retire(staging);}else{immediate(record);destroy(staging);}
    }catch(...){destroy(staging);throw;}
}
VkSampler VulkanBackend::State::sampler(bool linear,bool clamp,double mipmapping,float anisotropy) {
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter=info.minFilter=linear?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
    info.mipmapMode=mipmapping>2?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU=info.addressModeV=info.addressModeW=clamp?VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.maxLod=mipmapping>=1?VK_LOD_CLAMP_NONE:0;
    info.anisotropyEnable=anisotropy>1;
    info.maxAnisotropy=std::clamp(anisotropy,1.f,properties.limits.maxSamplerAnisotropy);
    VkSampler result{};
    require(vkCreateSampler(device,&info,nullptr,&result),"Creating Vulkan sampler");
    return result;
}
VkShaderModule VulkanBackend::State::shader(std::span<const std::uint32_t> words) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize=words.size_bytes(); info.pCode=words.data();
    VkShaderModule module{};
    require(vkCreateShaderModule(device,&info,nullptr,&module),"Creating Vulkan shader module");
    return module;
}
void VulkanBackend::State::immediate(const std::function<void(VkCommandBuffer)>& record) {
    if(command) throw std::runtime_error("Nested Vulkan immediate submission");
    VkCommandPool pool{}; VkFence fence{};
    try {
        VkCommandPoolCreateInfo create{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        create.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; create.queueFamilyIndex=queueFamily;
        require(vkCreateCommandPool(device,&create,nullptr,&pool),"Creating Vulkan transfer pool");
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool=pool; allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocation.commandBufferCount=1;
        VkCommandBuffer cmd{};
        require(vkAllocateCommandBuffers(device,&allocation,&cmd),"Allocating Vulkan transfer command");
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        require(vkBeginCommandBuffer(cmd,&beginInfo),"Beginning Vulkan transfer");
        record(cmd);
        require(vkEndCommandBuffer(cmd),"Ending Vulkan transfer");
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        require(vkCreateFence(device,&fenceInfo,nullptr,&fence),"Creating Vulkan transfer fence");
        VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandInfo.commandBuffer=cmd;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.commandBufferInfoCount=1; submit.pCommandBufferInfos=&commandInfo;
        require(vkQueueSubmit2(queue,1,&submit,fence),"Submitting Vulkan transfer");
        require(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),"Waiting for Vulkan transfer");
        vkDestroyFence(device,fence,nullptr);
        vkDestroyCommandPool(device,pool,nullptr);
    } catch(...) {
        if(fence) vkDestroyFence(device,fence,nullptr);
        if(pool) vkDestroyCommandPool(device,pool,nullptr);
        throw;
    }
}
void VulkanBackend::State::ensure_targets(WindowMetrics metrics){
    if(metrics.drawableWidth<=0||metrics.drawableHeight<=0)return;
    if(swapDirty||!swapchain||extent.width!=static_cast<unsigned>(metrics.drawableWidth)||extent.height!=static_cast<unsigned>(metrics.drawableHeight))create_swapchain(*this,metrics);
    if(!extent.width||!extent.height||swapDirty)return;
    if(!scene.color.handle){const bool composite=render_policy::composite_enabled(options.pbr,options.features),depthEffects=options.features.ssao||options.features.fog||options.features.taa;auto usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;scene.color=image(extent.width,extent.height,VK_FORMAT_R8G8B8A8_UNORM,usage,1,1,samples);scene.depth=image(extent.width,extent.height,depthFormat,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|(composite?VK_IMAGE_USAGE_TRANSFER_DST_BIT:0)|(depthEffects?VK_IMAGE_USAGE_TRANSFER_SRC_BIT:0),1,1,samples);if(samples!=VK_SAMPLE_COUNT_1_BIT)scene.resolve=image(extent.width,extent.height,VK_FORMAT_R8G8B8A8_UNORM,usage);finalImage=image(extent.width,extent.height,VK_FORMAT_R8G8B8A8_UNORM,usage);
        if(composite){hdr.color=image(extent.width,extent.height,VK_FORMAT_R16G16B16A16_SFLOAT,usage,1,1,samples);hdr.depth=image(extent.width,extent.height,depthFormat,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,1,1,samples);if(samples!=VK_SAMPLE_COUNT_1_BIT)hdr.resolve=image(extent.width,extent.height,VK_FORMAT_R16G16B16A16_SFLOAT,usage);}}
}
// Composited (PBR/post) translucent views blend into the HDR target, so their auxiliary color must keep HDR range too.
void VulkanBackend::State::ensure_auxiliary(vk22::Target& target){if(target.color.handle)return;target.color=image(extent.width,extent.height,render_policy::composite_enabled(options.pbr,options.features)&&&target==&alpha?VK_FORMAT_R16G16B16A16_SFLOAT:VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);target.depth=image(extent.width,extent.height,depthFormat,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);}
void VulkanBackend::State::begin(WindowMetrics metrics,std::int32_t tripleBufferRaw,bool bonus){
    command=VK_NULL_HANDLE;
    if(metrics.drawableWidth<=0||metrics.drawableHeight<=0){finalValid=false;return;}
    auto flags=SDL_GetWindowFlags(window);bool fullscreen=(flags&SDL_WINDOW_FULLSCREEN)!=0;bool exclusive=fullscreen&&(flags&SDL_WINDOW_FULLSCREEN_DESKTOP)!=SDL_WINDOW_FULLSCREEN_DESKTOP;
    unsigned desired=exclusive&&tripleBufferRaw!=0?2:1;
    int interval=options.vsync&&!(exclusive&&!bonus&&tripleBufferRaw!=0)?1:0;
    if(requestedInterval!=interval){requestedInterval=interval;swapDirty=true;}
    limit=desired;while(pending>=limit){retire_slot(*this,front);front=(front+1)%slots.size();}
    ensure_targets(metrics);if(swapDirty||!swapchain)return;
    auto& slot=slots[slotIndex];if(slot.pending)retire_slot(*this,slotIndex);
    garbage(*this,slot);
    VkResult acquired=vkAcquireNextImageKHR(device,swapchain,UINT64_MAX,slot.acquired,VK_NULL_HANDLE,&acquiredIndex);
    if(acquired==VK_ERROR_OUT_OF_DATE_KHR){swapDirty=true;ensure_targets(metrics);if(swapDirty)return;acquired=vkAcquireNextImageKHR(device,swapchain,UINT64_MAX,slot.acquired,VK_NULL_HANDLE,&acquiredIndex);}
    if(acquired==VK_ERROR_OUT_OF_DATE_KHR){swapDirty=true;return;}
    if(acquired==VK_SUBOPTIMAL_KHR)swapDirty=true;else require(acquired,"Acquiring Vulkan frame");
    require(vkResetCommandPool(device,slot.pool,0),"Resetting Vulkan frame commands");VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};beginInfo.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;require(vkBeginCommandBuffer(slot.command,&beginInfo),"Beginning Vulkan frame");command=slot.command;
}
void VulkanBackend::State::finish(){
    if(!command)return;auto& slot=slots[slotIndex];
    transition(finalImage,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    // The acquire wait is at TRANSFER, so the layout transition must start there too,
    // even for a fresh UNDEFINED image: discarding contents does not retire presentation.
    VkImageMemoryBarrier2 swapBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    swapBarrier.srcStageMask=VK_PIPELINE_STAGE_2_TRANSFER_BIT;swapBarrier.srcAccessMask=VK_ACCESS_2_NONE;
    swapBarrier.dstStageMask=VK_PIPELINE_STAGE_2_TRANSFER_BIT;swapBarrier.dstAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
    swapBarrier.oldLayout=swapLayouts[acquiredIndex];swapBarrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapBarrier.srcQueueFamilyIndex=swapBarrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    swapBarrier.image=swapImages[acquiredIndex];swapBarrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    VkDependencyInfo swapDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};swapDependency.imageMemoryBarrierCount=1;swapDependency.pImageMemoryBarriers=&swapBarrier;
    vkCmdPipelineBarrier2(command,&swapDependency);
    VkImageBlit blit{};blit.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};blit.dstSubresource=blit.srcSubresource;blit.srcOffsets[1]={static_cast<int>(extent.width),static_cast<int>(extent.height),1};blit.dstOffsets[1]=blit.srcOffsets[1];vkCmdBlitImage(command,finalImage.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,swapImages[acquiredIndex],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
    // Keep the final layout write in the ALL_COMMANDS signal's execution scope.
    swapBarrier.srcStageMask=VK_PIPELINE_STAGE_2_TRANSFER_BIT;swapBarrier.srcAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
    swapBarrier.dstStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;swapBarrier.dstAccessMask=VK_ACCESS_2_NONE;
    swapBarrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;swapBarrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier2(command,&swapDependency);swapLayouts[acquiredIndex]=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    require(vkEndCommandBuffer(command),"Ending Vulkan frame");require(vkResetFences(device,1,&slot.fence),"Resetting Vulkan frame fence");
    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};wait.semaphore=slot.acquired;wait.stageMask=VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};signal.semaphore=swapRendered[acquiredIndex];signal.stageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};commands.commandBuffer=command;VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};submit.waitSemaphoreInfoCount=1;submit.pWaitSemaphoreInfos=&wait;submit.commandBufferInfoCount=1;submit.pCommandBufferInfos=&commands;submit.signalSemaphoreInfoCount=1;submit.pSignalSemaphoreInfos=&signal;
    require(vkQueueSubmit2(queue,1,&submit,slot.fence),"Submitting Vulkan frame");if(!pending)front=slotIndex;slot.pending=true;++pending;++submissions;finalSlot=slotIndex;finalSubmission=submissions;finalValid=true;command=VK_NULL_HANDLE;
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.waitSemaphoreCount=1;present.pWaitSemaphores=&swapRendered[acquiredIndex];present.swapchainCount=1;present.pSwapchains=&swapchain;present.pImageIndices=&acquiredIndex;auto result=vkQueuePresentKHR(queue,&present);if(result==VK_ERROR_OUT_OF_DATE_KHR||result==VK_SUBOPTIMAL_KHR)swapDirty=true;else require(result,"Presenting Vulkan frame");slotIndex=(slotIndex+1)%slots.size();
}
void VulkanBackend::State::drain(){for(unsigned i=0;i<slots.size();++i)retire_slot(*this,i);front=slotIndex;}
void VulkanBackend::State::invalidate(){drain();finalValid=false;swapDirty=true;}
void VulkanBackend::State::readback(std::string_view path,WindowMetrics metrics) {
    if(!finalValid||metrics.drawableWidth<=0||metrics.drawableHeight<=0||
       extent.width!=static_cast<unsigned>(metrics.drawableWidth)||extent.height!=static_cast<unsigned>(metrics.drawableHeight))
        throw std::runtime_error("Vulkan capture requires a completed frame at the current drawable size");
    // Capture synchronizes without retiring/accounting a source presentation slot.
    auto& slot=slots[finalSlot];
    if(slot.pending) require(vkWaitForFences(device,1,&slot.fence,VK_TRUE,UINT64_MAX),"Waiting for Vulkan capture");
    VkDeviceSize size=static_cast<VkDeviceSize>(extent.width)*extent.height*4;
    auto pixels=buffer(size,VK_BUFFER_USAGE_TRANSFER_DST_BIT,true);
    try {
        immediate([&](VkCommandBuffer cmd) {
            barrier(cmd,finalImage.handle,VK_IMAGE_ASPECT_COLOR_BIT,1,1,finalImage.layout,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_READ_BIT);
            VkBufferImageCopy region{};
            region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; region.imageExtent={extent.width,extent.height,1};
            vkCmdCopyImageToBuffer(cmd,finalImage.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,pixels.handle,1,&region);
            VkBufferMemoryBarrier2 b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            b.srcStageMask=VK_PIPELINE_STAGE_2_TRANSFER_BIT; b.srcAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
            b.dstStageMask=VK_PIPELINE_STAGE_2_HOST_BIT; b.dstAccessMask=VK_ACCESS_2_HOST_READ_BIT;
            b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b.buffer=pixels.handle; b.size=VK_WHOLE_SIZE;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.bufferMemoryBarrierCount=1; dependency.pBufferMemoryBarriers=&b;
            vkCmdPipelineBarrier2(cmd,&dependency);
        });
        finalImage.layout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        std::ofstream output(std::string(path),std::ios::binary);
        if(!output) throw std::runtime_error("Cannot open Vulkan capture output");
        output<<"P6\n"<<extent.width<<' '<<extent.height<<"\n255\n";
        std::vector<char> row(static_cast<std::size_t>(extent.width)*3);
        auto* rgba=static_cast<const unsigned char*>(pixels.mapped);
        for(std::uint32_t y=0;y<extent.height;++y) {
            for(std::uint32_t x=0;x<extent.width;++x) {
                auto src=(static_cast<std::size_t>(y)*extent.width+x)*4;
                auto dst=static_cast<std::size_t>(x)*3;
                std::memcpy(row.data()+dst,rgba+src,3);
            }
            output.write(row.data(),static_cast<std::streamsize>(row.size()));
        }
        if(!output) throw std::runtime_error("Writing Vulkan capture failed");
        destroy(pixels);
    } catch(...) { destroy(pixels); throw; }
}
VulkanBackend::VulkanBackend(SDL_Window* window,const RenderOptions& options):state_(std::make_unique<State>(window,options)){}
VulkanBackend::~VulkanBackend()=default;
void VulkanBackend::draw(const RenderFrame& frame,WindowMetrics metrics) {
    // Reject source-unsupported passes before acquiring/signalling a swap image.
    if(metrics.drawableWidth>0&&metrics.drawableHeight>0) {
        if(frame.world&&!frame.hardwareLighting)
            throw std::runtime_error("Legacy software world-light topology is not implemented by the native Vulkan hardware path");
        const bool stencil=frame.bonusProfile?frame.shadowStencilRaw>0:frame.shadowStencilRaw!=0;
        if(stencil&&!frame.shadows.empty()&&frame.renderEntities&&(!frame.bonusProfile||frame.planarCapable)) {
            auto admitted=[&](const Camera& camera) {
                if(!camera.perDrawOpacity&&camera.alpha<=0) return false;
                auto viewport=camera.viewport;
                if(viewport.width<=0||viewport.height<=0) viewport={0,0,float(metrics.width),float(metrics.height)};
                return std::lround(viewport.width*metrics.scaleX)>0&&std::lround(viewport.height*metrics.scaleY)>0;
            };
            auto check=[&](const Camera& camera) {
                if(admitted(camera)&&camera.drawWorld&&!camera.noShadow&&!camera.noEntities)
                    throw std::runtime_error("Nondefault source stencil-volume shadows are not implemented; native Vulkan supports the recovered planar route");
            };
            for(const auto& camera:frame.cameras) {
                if(camera.portalTarget||!admitted(camera)) continue;
                // Portal targets render one level only, before their attached top-level view.
                if(camera.portalId) {
                    auto portal=std::find_if(frame.cameras.begin(),frame.cameras.end(),[&](const Camera& c){return c.sourceId==camera.portalId;});
                    if(portal==frame.cameras.end()) throw std::runtime_error("Attached portal view is absent from game frame");
                    if(Renderer::portal_surface(frame,camera,metrics)) check(*portal);
                }
                check(camera);
            }
        }
    }
    state_->begin(metrics,frame.tripleBufferRaw,frame.bonusProfile);
    if(!state_->command) return;
    try {
        vk22::raster_draw(*state_->raster,frame,metrics);
        state_->finish();
    } catch(...) {
        state_->command=VK_NULL_HANDLE;
        state_->finalValid=false;
        throw;
    }
}
void VulkanBackend::capture(std::string_view path,WindowMetrics metrics)const{state_->readback(path,metrics);}
void VulkanBackend::drain(){state_->drain();}
void VulkanBackend::invalidate(){state_->invalidate();}
void VulkanBackend::release_scene(){state_->drain();vk22::raster_release(*state_->raster);vk22::rt_release(*state_->rays);state_->retainedColorValid=false;state_->portalColorValid=false;}
void VulkanBackend::purge_model(const Model* model) {
    state_->drain();
    vk22::raster_purge_model(*state_->raster,model);
    vk22::rt_purge_model(*state_->rays,model);
    if(model) for(const auto& material:model->materials) {
        if(material.texture&&material.texture.use_count()==1) purge_image(material.texture.get());
        if(material.lightmap&&material.lightmap.use_count()==1) purge_image(material.lightmap.get());
    }
}
void VulkanBackend::purge_image(const Image* image){state_->drain();vk22::raster_purge_image(*state_->raster,image);vk22::rt_purge_image(*state_->rays,image);}
int VulkanBackend::shader_version()const{return vk22::raster_shader_version(*state_->raster);}
std::string VulkanBackend::description()const{const auto&p=state_->properties;return std::string(p.deviceName)+"; Vulkan "+std::to_string(VK_API_VERSION_MAJOR(p.apiVersion))+"."+std::to_string(VK_API_VERSION_MINOR(p.apiVersion))+"."+std::to_string(VK_API_VERSION_PATCH(p.apiVersion))+(state_->rayEnabled?"; KHR ray query":"");}
PresentationState VulkanBackend::presentation_state()const{return {state_->limit,state_->pending,state_->submissions,state_->waits,state_->presentMode==VK_PRESENT_MODE_FIFO_KHR?1:0,static_cast<int>(state_->samples)};}
}
