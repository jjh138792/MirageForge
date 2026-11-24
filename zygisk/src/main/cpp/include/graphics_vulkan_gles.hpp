// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

#pragma once

#include "graphics/gles_drm_hook.hpp"
#include "graphics/vulkan_hal_hook.hpp"

namespace mirage {

inline void init_graphics_symbols() {
    init_gles_drm_symbols();
    init_vulkan_hal_symbols();
}

} // namespace mirage
