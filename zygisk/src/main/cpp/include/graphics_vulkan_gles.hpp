#pragma once

#include "graphics/gles_drm_hook.hpp"
#include "graphics/vulkan_hal_hook.hpp"

namespace mirage {

inline void init_graphics_symbols() {
    init_gles_drm_symbols();
    init_vulkan_hal_symbols();
}

} // namespace mirage
