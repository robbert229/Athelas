#include "gpu/vk_loader.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_vulkan.h>
#endif

namespace gpu::vk {

#define GPU_VK_DEFINE(name) PFN_##name name = nullptr;
GPU_VK_GLOBAL(GPU_VK_DEFINE)
GPU_VK_INSTANCE(GPU_VK_DEFINE)
GPU_VK_DEVICE(GPU_VK_DEFINE)
#undef GPU_VK_DEFINE
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;

namespace {
void* g_library = nullptr;
VkInstance g_instance = VK_NULL_HANDLE;

#ifndef _WIN32
// SDL's loader, once its video is up: the window's surface is made with the functions SDL looked up itself, so the
// instance must come from the same library (SDL_VULKAN_LIBRARY may name another one than libvulkan.so.1)
PFN_vkGetInstanceProcAddr SdlLoader() {
    static PFN_vkGetInstanceProcAddr sdl = nullptr;
    if (!sdl && SDL_WasInit(SDL_INIT_VIDEO) && SDL_Vulkan_LoadLibrary(nullptr))
        sdl = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    return sdl;
}
#endif
}  // namespace

bool OpenLibrary(const char** reason) {
#ifdef _WIN32
    if (vkGetInstanceProcAddr) return true;
    HMODULE m = ::LoadLibraryW(L"vulkan-1.dll");
    if (!m) {
        if (reason) *reason = "vulkan-1.dll isn't installed (the graphics driver has no Vulkan)";
        return false;
    }
    g_library = m;
    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        reinterpret_cast<void*>(::GetProcAddress(m, "vkGetInstanceProcAddr")));
#else
    if (PFN_vkGetInstanceProcAddr sdl = SdlLoader()) {
        if (vkGetInstanceProcAddr == sdl) return true;
        vkGetInstanceProcAddr = sdl;  // (one opened before SDL's video started, by the probe, stays open)
    } else {
        if (vkGetInstanceProcAddr) return true;
        g_library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!g_library) {
            if (reason) *reason = "libvulkan.so.1 isn't installed";
            return false;
        }
        vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(g_library, "vkGetInstanceProcAddr"));
    }
#endif
    if (!vkGetInstanceProcAddr) {
        if (reason) *reason = "the Vulkan loader has no vkGetInstanceProcAddr";
        return false;
    }
#define GPU_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(VK_NULL_HANDLE, #name));
    GPU_VK_GLOBAL(GPU_VK_LOAD)
#undef GPU_VK_LOAD
    if (!vkCreateInstance) {
        if (reason) *reason = "the Vulkan loader has no vkCreateInstance";
        return false;
    }
    return true;
}

void LoadInstance(VkInstance instance) {
    g_instance = instance;
#define GPU_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name));
    GPU_VK_INSTANCE(GPU_VK_LOAD)
#undef GPU_VK_LOAD
}

void LoadDevice(VkDevice device) {
#define GPU_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));
    GPU_VK_DEVICE(GPU_VK_LOAD)
#undef GPU_VK_LOAD
}

PFN_vkVoidFunction Lookup(const char* name, void* instance) {
    VkInstance i = instance ? static_cast<VkInstance>(instance) : g_instance;
    return vkGetInstanceProcAddr ? vkGetInstanceProcAddr(i, name) : nullptr;
}

}  // namespace gpu::vk
