// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <span>
#include <vector>
#include "common/types.h"

namespace Shader::Gcn {

inline std::vector<u32> PatchSkateJumpTable(std::span<const u32> code, u64 shader_hash) {
    if (shader_hash != 0xe83af81b || code.size_bytes() != 0x2a88) {
        return {};
    }

    u64 fingerprint = 0xcbf29ce484222325;
    for (const u32 word : code) {
        for (u32 shift = 0; shift < 32; shift += 8) {
            fingerprint = (fingerprint ^ ((word >> shift) & 0xff)) * 0x100000001b3;
        }
    }
    if (fingerprint != 0x5eb2de8fe58b4fd5) {
        return {};
    }

    // Captured from the jump table in skate. CUSA37423 v1.29.
    static constexpr std::array<u32, 36> targets{
        0x2648, 0x2570, 0x2478, 0x2380, 0x22a0, 0x21c0, 0x20e0, 0x2000, 0x1f20,
        0x1e40, 0x1d60, 0x1c80, 0x1ba0, 0x1ac0, 0x19e0, 0x1900, 0x1820, 0x1740,
        0x1660, 0x1580, 0x14a0, 0x13c0, 0x12e0, 0x1200, 0x1120, 0x1040, 0x0f60,
        0x0e80, 0x0da0, 0x0cc0, 0x0be0, 0x0b00, 0x0a20, 0x0940, 0x086c, 0x2720,
    };

    std::vector<u32> patched{code.begin(), code.end()};
    const u32 dispatch = static_cast<u32>(patched.size());
    const u32 leaves = dispatch + (targets.size() - 1) * 3 + 1;
    const auto branch = [](u32 opcode, u32 from, u32 to) {
        return opcode | static_cast<u16>(static_cast<s32>(to) - static_cast<s32>(from) - 1);
    };

    patched[0x858 / 4] = branch(0xbf820000, 0x858 / 4, dispatch);
    for (u32 pc = 0x85c; pc <= 0x868; pc += 4) {
        patched[pc / 4] = 0xbf800000;
    }

    for (u32 index = 0; index + 1 < targets.size(); ++index) {
        patched.push_back(0xbf06ff1a); // s_cmp_eq_u32 s26, index * 4
        patched.push_back(index * 4);
        const u32 pc = static_cast<u32>(patched.size());
        patched.push_back(branch(0xbf850000, pc, leaves + index * 5));
    }
    const u32 default_pc = static_cast<u32>(patched.size());
    patched.push_back(branch(0xbf820000, default_pc, leaves + (targets.size() - 1) * 5));

    for (const u32 target : targets) {
        patched.push_back(0xbe9a03ff); // s_mov_b32 s26, displacement
        patched.push_back(0x2b24 - target);
        patched.push_back(code[0x860 / 4]);
        patched.push_back(code[0x864 / 4]);
        const u32 pc = static_cast<u32>(patched.size());
        patched.push_back(branch(0xbf820000, pc, target / 4));
    }
    return patched;
}

} // namespace Shader::Gcn
