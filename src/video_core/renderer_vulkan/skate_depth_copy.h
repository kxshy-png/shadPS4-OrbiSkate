// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <bit>
#include "video_core/amdgpu/regs.h"

namespace Vulkan {

inline bool IsSkateDepthCopyDraw(const AmdGpu::Regs& regs, u64 shader_hash) {
    const auto& color = regs.color_buffers[0];
    return shader_hash == 0xfecfa65674c27431 &&
           std::bit_cast<u32>(regs.depth_render_control) == 0x84 &&
           regs.color_control.mode == AmdGpu::ColorControl::OperationMode::Normal &&
           regs.color_target_mask.raw == 0xf &&
           regs.primitive_type == AmdGpu::PrimitiveType::RectList && regs.num_indices == 3 &&
           regs.depth_buffer.DepthValid() && regs.depth_buffer.NumSamples() == 2 &&
           regs.depth_buffer.z_info.format == AmdGpu::DepthBuffer::ZFormat::Z32Float &&
           regs.depth_view.slice_start == 0 && regs.depth_view.slice_max == 0 && color &&
           color.GetDataFmt() == AmdGpu::DataFormat::Format32 &&
           color.GetNumberFmt() == AmdGpu::NumberFormat::Float && color.NumSamples() == 1 &&
           color.BaseSlice() == 0 && color.NumSlices() == 1;
}

} // namespace Vulkan
