// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <span>
#include <vector>
#include "common/types.h"

namespace Shader::Gcn {

inline std::vector<u32> PatchSkateJumpTable(std::span<const u32> code, u64 shader_hash) {
    // Captured from the jump tables in skate. CUSA37423 v1.29.
    static constexpr std::array<u32, 36> targets_e83af81b{
        0x2648, 0x2570, 0x2478, 0x2380, 0x22a0, 0x21c0, 0x20e0, 0x2000, 0x1f20,
        0x1e40, 0x1d60, 0x1c80, 0x1ba0, 0x1ac0, 0x19e0, 0x1900, 0x1820, 0x1740,
        0x1660, 0x1580, 0x14a0, 0x13c0, 0x12e0, 0x1200, 0x1120, 0x1040, 0x0f60,
        0x0e80, 0x0da0, 0x0cc0, 0x0be0, 0x0b00, 0x0a20, 0x0940, 0x086c, 0x2720,
    };
    static constexpr std::array<u32, 33> targets_90882249{
        0x12994, 0x122bc, 0x11be4, 0x1150c, 0x10e34, 0x1075c, 0x10084, 0xf9ac, 0xeb78,
        0xe4a0,  0xdd7c,  0xce14,  0xc73c,  0xc018,  0xb0b0,  0xa4ac,  0x98a8, 0x8ca4,
        0x8524,  0x7eb0,  0x71c8,  0x659c,  0x5a50,  0x5174,  0x4570,  0x3ec4, 0x3800,
        0x2bfc,  0x20ac,  0x155c,  0x958,   0x41c,   0x13068,
    };
    std::span<const u32> targets;
    u32 code_bytes;
    u32 branch_pc;
    u32 table_offset;
    u64 expected_fingerprint;
    switch (shader_hash) {
    case 0xe83af81b:
        targets = targets_e83af81b;
        code_bytes = 0x2a88;
        branch_pc = 0x868;
        table_offset = 0x2b24;
        expected_fingerprint = 0x5eb2de8fe58b4fd5;
        break;
    case 0x90882249:
        targets = targets_90882249;
        code_bytes = 0x1505c;
        branch_pc = 0x418;
        table_offset = 0x15144;
        expected_fingerprint = 0x0a0b49c4f6321ef8;
        break;
    default:
        return {};
    }
    if (code.size_bytes() != code_bytes) {
        return {};
    }
    u64 fingerprint = 0xcbf29ce484222325;
    for (const u32 word : code) {
        for (u32 shift = 0; shift < 32; shift += 8) {
            fingerprint = (fingerprint ^ ((word >> shift) & 0xff)) * 0x100000001b3;
        }
    }
    if (fingerprint != expected_fingerprint) {
        return {};
    }

    std::vector<u32> patched{code.begin(), code.end()};
    const u32 load_pc = branch_pc - 16;
    const u32 index_reg = code[load_pc / 4] & 0xff;
    const u32 value_reg = (code[load_pc / 4] >> 15) & 0x7f;
    const u32 dispatch = static_cast<u32>(patched.size());
    const u32 leaves = dispatch + (targets.size() - 1) * 3 + 1;
    const auto branch = [](u32 opcode, u32 from, u32 to) {
        return opcode | static_cast<u16>(static_cast<s32>(to) - static_cast<s32>(from) - 1);
    };

    patched[load_pc / 4] = branch(0xbf820000, load_pc / 4, dispatch);
    for (u32 pc = load_pc + 4; pc <= branch_pc; pc += 4) {
        patched[pc / 4] = 0xbf800000;
    }

    for (u32 index = 0; index + 1 < targets.size(); ++index) {
        patched.push_back(0xbf06ff00 | index_reg); // s_cmp_eq_u32 index_reg, index * 4
        patched.push_back(index * 4);
        const u32 pc = static_cast<u32>(patched.size());
        patched.push_back(branch(0xbf850000, pc, leaves + index * 5));
    }
    const u32 default_pc = static_cast<u32>(patched.size());
    patched.push_back(branch(0xbf820000, default_pc, leaves + (targets.size() - 1) * 5));

    for (const u32 target : targets) {
        patched.push_back(0xbe8003ff | (value_reg << 16)); // s_mov_b32 value_reg, displacement
        patched.push_back(table_offset - target);
        patched.push_back(code[(branch_pc - 8) / 4]);
        patched.push_back(code[(branch_pc - 4) / 4]);
        const u32 pc = static_cast<u32>(patched.size());
        patched.push_back(branch(0xbf820000, pc, target / 4));
    }
    return patched;
}

} // namespace Shader::Gcn
