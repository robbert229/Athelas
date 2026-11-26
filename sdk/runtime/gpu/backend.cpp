// The backend the renderer draws with
#include "gpu/backend.h"

#include "app/app.h"
#include "core/log.h"

namespace gpu {

// [graphics] renderer: 0 Direct3D 12, 1 Vulkan (xdk/d3d8.cpp falls back to Direct3D 12 when Vulkan's Init fails)
std::unique_ptr<Backend> CreateBackend() {
#ifdef _WIN32
    if (app::Get().renderer == 1) return CreateVulkanBackend();
    return CreateD3D12Backend();
#else
    // off Windows only Vulkan: the setting's Direct3D 12 is read as Vulkan (the file keeps what it says), and the
    // upscalers and frame generation, Direct3D 12's, stay off (vk.cpp's Init says so for the upscaler)
    if (app::Get().renderer != 1) LOGR("renderer: Direct3D 12 is Windows only, Vulkan draws the game");
    if (app::Get().frame_generation) LOGR("renderer: frame generation is Direct3D 12 only, off");
    return CreateVulkanBackend();
#endif
}

}  // namespace gpu
