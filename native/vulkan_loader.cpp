#include "vulkan_loader.hpp"
#include <dlfcn.h>
#include <stdexcept>
#include <string>
namespace k22::vk22 {
namespace api {
#define K22_DEFINE_VULKAN(name) PFN_##name name{};
K22_VULKAN_FUNCTIONS(K22_DEFINE_VULKAN)
#undef K22_DEFINE_VULKAN
}
void load_vulkan(){
    // The loader remains resident while SDL and any Vulkan device can use it.
    static void* library=[] {
        void* handle=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);
        if(!handle)throw std::runtime_error(std::string("Explicit Vulkan renderer requires host libvulkan.so.1: ")+dlerror());
        struct Dispatch {
#define K22_LOCAL_VULKAN(name) PFN_##name name{};
            K22_VULKAN_FUNCTIONS(K22_LOCAL_VULKAN)
#undef K22_LOCAL_VULKAN
        } loaded;
        try {
#define K22_LOAD_VULKAN(name) loaded.name=reinterpret_cast<PFN_##name>(dlsym(handle,#name)); if(!loaded.name)throw std::runtime_error("Host Vulkan loader lacks required entrypoint " #name);
            K22_VULKAN_FUNCTIONS(K22_LOAD_VULKAN)
#undef K22_LOAD_VULKAN
        }catch(...){dlclose(handle);throw;}
        // Publish only a complete immutable dispatch; failed loads leave no dangling pointers.
#define K22_PUBLISH_VULKAN(name) api::name=loaded.name;
        K22_VULKAN_FUNCTIONS(K22_PUBLISH_VULKAN)
#undef K22_PUBLISH_VULKAN
        return handle;
    }();
    (void)library;
}
}
