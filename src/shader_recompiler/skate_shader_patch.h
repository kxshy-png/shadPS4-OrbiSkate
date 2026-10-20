// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <span>
#include <vector>
#include "common/types.h"
#include "shader_recompiler/frontend/decode.h"

namespace Shader::Gcn {

inline std::vector<u32> LowerSkateJumpTable(std::span<const u32> code, u32 branch_pc,
                                            u32 table_offset, std::span<const u32> targets) {
    if (targets.empty() || targets.size() > 65 || code.size() > 0x100000) {
        return {};
    }
    const auto fits_branch = [](s64 from, s64 to) {
        const s64 displacement = to - from - 1;
        return displacement >= std::numeric_limits<s16>::min() &&
               displacement <= std::numeric_limits<s16>::max();
    };
    const u32 leaf_start = static_cast<u32>(code.size() + (targets.size() - 1) * 3 + 1);
    if (!fits_branch((branch_pc - 16) / 4, code.size())) {
        return {};
    }
    for (u32 i = 0; i < targets.size(); ++i) {
        if (!fits_branch(leaf_start + i * 5 + 4, targets[i] / 4)) {
            return {};
        }
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

    return LowerSkateJumpTable(code, branch_pc, table_offset, targets);
}

template <typename ReadTable>
std::vector<u32> PatchSkateRuntimeJumpTable(std::span<const u32> code, ReadTable&& read_table) {
    if (code.empty() || code.size() > 0x100000 ||
        std::ranges::none_of(code, [](u32 word) { return (word & 0xffffff00) == 0xbe802000; })) {
        return {};
    }
    std::vector<bool> boundaries(code.size());
    std::vector<u32> direct_targets;
    GcnCodeSlice slice(code.data(), code.data() + code.size());
    GcnDecodeContext decoder;
    u32 pc = 0;
    u32 branch_pc = 0;
    Opcode last_opcode{};
    while (!slice.atEnd()) {
        const auto inst = decoder.decodeInstruction(slice);
        boundaries[pc / 4] = true;
        if (inst.opcode == Opcode::S_SETPC_B64) {
            if (branch_pc != 0 || pc < 40) {
                return {};
            }
            branch_pc = pc;
        } else if (inst.IsUnconditionalBranch() || inst.IsConditionalBranch()) {
            direct_targets.push_back(inst.BranchTarget(pc));
        } else if (inst.IsFork() || inst.opcode == Opcode::S_SWAPPC_B64) {
            return {};
        }
        pc += inst.length;
        last_opcode = inst.opcode;
    }
    if (branch_pc == 0 || (last_opcode != Opcode::S_ENDPGM && last_opcode != Opcode::S_BRANCH)) {
        return {};
    }
    for (u32 offset = 0; offset <= 40; offset += 4) {
        if (offset != 24 && !boundaries[(branch_pc - offset) / 4]) {
            return {};
        }
    }
    const auto word = [&](u32 offset) { return code[(branch_pc - offset) / 4]; };
    const u32 pair = word(0) & 0xff;
    const u32 load = word(16);
    const u32 index = load & 0xff;
    const u32 loaded = (load >> 15) & 0x7f;
    const auto sop2 = [](u32 op, u32 dst, u32 a, u32 b) {
        return 0x80000000 | (op << 23) | (dst << 16) | (b << 8) | a;
    };
    if (pair > 102 || pair % 2 || index > 103 || loaded > 103 || index == pair ||
        index == pair + 1 || loaded == pair || loaded == pair + 1 ||
        word(32) != (0xbe801f00 | (pair << 16)) || word(28) != sop2(0, pair, 255, pair) ||
        word(20) != sop2(4, pair + 1, 128, pair + 1) || (load >> 22) != 0x300 || (load & 0x100) ||
        ((load >> 9) & 0x3f) != pair / 2 || (word(12) & 0xffff0f00) != 0xbf8c0000 ||
        word(8) != sop2(1, pair, pair, loaded) || word(4) != sop2(5, pair + 1, pair + 1, 128) ||
        word(36) != sop2(30, index, index, 130)) {
        return {};
    }
    const u32 bound_operand = (word(40) >> 8) & 0xff;
    if (bound_operand < 128 || bound_operand > 192 ||
        word(40) != sop2(7, index, word(40) & 0xff, bound_operand) || (word(40) & 0xff) == 255) {
        return {};
    }
    const u32 count = bound_operand - 128 + 1;
    const u64 table = u64{branch_pc} - 28 + word(24);
    if (table < code.size_bytes() || table % 4 || table + count * 4 > code.size_bytes() + 4096) {
        return {};
    }
    const auto valid_target = [&](u32 target) {
        return target < code.size_bytes() && target % 4 == 0 && boundaries[target / 4] &&
               !(target > branch_pc - 40 && target <= branch_pc);
    };
    if (!std::ranges::all_of(direct_targets, valid_target)) {
        return {};
    }
    std::vector<u32> displacements(count), repeated(count), targets(count);
    if (!read_table(static_cast<u32>(table), std::span{displacements}) ||
        !read_table(static_cast<u32>(table), std::span{repeated}) || displacements != repeated) {
        return {};
    }
    for (u32 i = 0; i < count; ++i) {
        if (displacements[i] > table) {
            return {};
        }
        targets[i] = static_cast<u32>(table) - displacements[i];
        if (!valid_target(targets[i])) {
            return {};
        }
    }
    // This title stores immutable dispatch tables immediately after its shader metadata.
    return LowerSkateJumpTable(code, branch_pc, static_cast<u32>(table), targets);
}

} // namespace Shader::Gcn
