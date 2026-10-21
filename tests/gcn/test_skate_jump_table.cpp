// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>
#include "shader_recompiler/skate_shader_patch.h"

namespace {

constexpr std::array<u32, 12> Code{
    0x83808100, 0x8f008200, 0xbe8a1f00, 0x800a0aff, 52,         0x820b0b80,
    0xc0010a00, 0xbf8c007f, 0x808a020a, 0x828b800b, 0xbe80200a, 0xbf810000,
};

auto Patch(std::span<const u32> code, std::span<const u32> displacements) {
    return Shader::Gcn::PatchSkateRuntimeJumpTable(
        code, [&](u32 offset, std::span<u32> destination) {
            if (offset != 64 || destination.size() != displacements.size()) {
                return false;
            }
            std::ranges::copy(displacements, destination.begin());
            return true;
        });
}

} // namespace

TEST(SkateJumpTable, RetainsAllBoundedCases) {
    const auto patched = Patch(Code, std::array{20U, 20U});
    ASSERT_EQ(patched.size(), Code.size() + 14);
    EXPECT_EQ(patched[6] >> 16, 0xbf82U);
    EXPECT_EQ(patched[10], 0xbf800000U);
    EXPECT_EQ(patched[patched.size() - 3], Code[8]);
    EXPECT_EQ(patched[patched.size() - 2], Code[9]);
}

TEST(SkateJumpTable, RejectsMissingOrInvalidTargets) {
    EXPECT_TRUE(Patch(Code, std::array{20U}).empty());
    EXPECT_TRUE(Patch(Code, std::array{19U, 20U}).empty());
    EXPECT_TRUE(Patch(Code, std::array{48U, 20U}).empty());
    EXPECT_TRUE(Patch(Code, std::array{68U, 20U}).empty());
    EXPECT_TRUE(Patch(Code, std::array{24U, 20U}).empty());
}

TEST(SkateJumpTable, RejectsChangedTableAndUnsupportedPattern) {
    u32 calls = 0;
    EXPECT_TRUE(Shader::Gcn::PatchSkateRuntimeJumpTable(Code, [&](u32, std::span<u32> destination) {
                    std::ranges::fill(destination, ++calls == 1 ? 20U : 24U);
                    return true;
                }).empty());
    auto code = Code;
    code[7] |= 0x100;
    EXPECT_TRUE(Patch(code, std::array{20U, 20U}).empty());
    code = Code;
    code[4] = 0xfffffffc;
    EXPECT_TRUE(Patch(code, std::array{20U, 20U}).empty());
}

TEST(SkateJumpTable, RejectsOutOfRangeRelativeBranch) {
    std::vector<u32> code(40000, 0xbf800000);
    EXPECT_TRUE(Shader::Gcn::LowerSkateJumpTable(code, 40, 64, std::array{44U}).empty());
}
