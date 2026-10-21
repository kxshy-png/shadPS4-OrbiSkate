// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <map>
#include <ranges>
#include <span>
#include <vector>
#include <spirv/unified1/spirv.hpp11>
#include "common/types.h"

namespace Shader {

inline bool BoundSkateFragmentLoops(std::vector<u32>& code) {
    if (code.size() < 5 || code[0] != spv::MagicNumber || code[3] > 0xffff0000) {
        return false;
    }
    std::vector<std::span<const u32>> instructions;
    std::map<u32, std::vector<size_t>> blocks;
    u32 current_block{}, uint_type{}, bool_type{};
    size_t function = 0;
    for (size_t offset = 5; offset < code.size();) {
        const u32 count = code[offset] >> 16;
        if (count == 0 || count > code.size() - offset) {
            return false;
        }
        const std::span<const u32> inst{code.data() + offset, count};
        const auto op = static_cast<spv::Op>(inst[0] & 0xffff);
        if (op == spv::Op::OpTypeInt && count == 4 && inst[2] == 32 && inst[3] == 0) {
            uint_type = inst[1];
        } else if (op == spv::Op::OpTypeBool && count == 2) {
            bool_type = inst[1];
        } else if (op == spv::Op::OpFunction) {
            if (function != 0) {
                return false;
            }
            function = instructions.size();
        } else if (op == spv::Op::OpLabel && count == 2) {
            current_block = inst[1];
        }
        if (current_block != 0) {
            blocks[current_block].push_back(instructions.size());
        }
        instructions.push_back(inst);
        offset += count;
    }
    if (uint_type == 0 || bool_type == 0 || function == 0) {
        return false;
    }
    std::map<size_t, std::vector<u32>> before;
    std::map<size_t, std::vector<u32>> replacements;
    const auto append = [](std::vector<u32>& out, spv::Op op, std::initializer_list<u32> args) {
        out.push_back((static_cast<u32>(args.size() + 1) << 16) | static_cast<u32>(op));
        out.insert(out.end(), args);
    };
    u32 next_id = code[3];
    const u32 zero = next_id++, one = next_id++, limit = next_id++;
    append(before[function], spv::Op::OpConstant, {uint_type, zero, 0});
    append(before[function], spv::Op::OpConstant, {uint_type, one, 1});
    append(before[function], spv::Op::OpConstant, {uint_type, limit, 64});
    u32 loops = 0;
    for (const auto& [header, body] : blocks) {
        for (const auto index : body) {
            const auto merge_inst = instructions[index];
            if ((merge_inst[0] & 0xffff) != static_cast<u32>(spv::Op::OpLoopMerge)) {
                continue;
            }
            if (merge_inst.size() != 4) {
                return false;
            }
            const u32 merge = merge_inst[1], continuation = merge_inst[2];
            const auto continuation_it = blocks.find(continuation);
            if (continuation_it == blocks.end()) {
                return false;
            }
            std::vector<u32> predecessors;
            for (const auto& [label, candidate] : blocks) {
                const auto end = instructions[candidate.back()];
                const bool branch = end.size() == 2 &&
                                    end[0] == (2u << 16 | u32(spv::Op::OpBranch)) &&
                                    end[1] == header;
                const bool conditional = end.size() >= 4 &&
                                         (end[0] & 0xffff) == u32(spv::Op::OpBranchConditional) &&
                                         (end[2] == header || end[3] == header);
                if (branch || conditional) {
                    predecessors.push_back(label);
                }
            }
            if (predecessors.size() != 2 || !std::ranges::contains(predecessors, continuation)) {
                return false;
            }
            const u32 preheader = predecessors[predecessors[0] == continuation ? 1 : 0];
            const size_t end_index = continuation_it->second.back();
            const auto end = instructions[end_index];
            if (end.size() != 4 || end[0] != (4u << 16 | u32(spv::Op::OpBranchConditional)) ||
                end[2] != header || end[3] != merge) {
                return false;
            }
            const auto first_non_phi =
                std::ranges::find_if(body | std::views::drop(1), [&](size_t i) {
                    return (instructions[i][0] & 0xffff) != u32(spv::Op::OpPhi);
                });
            if (first_non_phi == body.end()) {
                return false;
            }
            const u32 counter = next_id++, incremented = next_id++, under_limit = next_id++,
                      keep_looping = next_id++;
            append(before[*first_non_phi], spv::Op::OpPhi,
                   {uint_type, counter, zero, preheader, incremented, continuation});
            append(before[end_index], spv::Op::OpIAdd, {uint_type, incremented, counter, one});
            append(before[end_index], spv::Op::OpULessThan,
                   {bool_type, under_limit, incremented, limit});
            append(before[end_index], spv::Op::OpLogicalAnd,
                   {bool_type, keep_looping, end[1], under_limit});
            append(replacements[end_index], spv::Op::OpBranchConditional,
                   {keep_looping, header, merge});
            ++loops;
        }
    }
    if (loops != 5) {
        return false;
    }
    std::vector<u32> patched{code.begin(), code.begin() + 5};
    patched[3] = next_id;
    for (size_t i = 0; i < instructions.size(); ++i) {
        if (const auto it = before.find(i); it != before.end()) {
            patched.insert(patched.end(), it->second.begin(), it->second.end());
        }
        if (const auto it = replacements.find(i); it != replacements.end()) {
            patched.insert(patched.end(), it->second.begin(), it->second.end());
        } else {
            patched.insert(patched.end(), instructions[i].begin(), instructions[i].end());
        }
    }
    code = std::move(patched);
    return true;
}

} // namespace Shader
