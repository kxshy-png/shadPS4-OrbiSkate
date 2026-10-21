# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

import struct
import unittest

from shader_jump_table import inspect


def fixture(pair=6, index=20, loaded=22):
    def alu(opcode, dst, src0, src1):
        return 0x8000_0000 | opcode << 23 | dst << 16 | src1 << 8 | src0

    words = [
        alu(7, index, 30, 130), alu(30, index, index, 130),
        0xbe80_1f00 | pair << 16, alu(0, pair, 255, pair), 0x74,
        alu(4, pair + 1, 128, pair + 1),
        0xc000_0000 | loaded << 15 | (pair // 2) << 9 | index,
        0xbf8c_007f, alu(1, pair, pair, loaded),
        alu(5, pair + 1, pair + 1, 128), 0xbe80_2000 | pair,
    ]
    blob = bytearray(0x8c)
    struct.pack_into('<11I', blob, 0, *words)
    struct.pack_into('<3I', blob, 0x80, 0x80, 0x78, 0x58)
    return blob


class JumpTableTests(unittest.TestCase):
    def test_multiple_targets_and_default_entry(self):
        result = inspect(fixture(), 40, 44)
        self.assertEqual(result['table_offset'], 0x80)
        self.assertEqual(result['targets'], [0, 8, 40])
        self.assertTrue(result['targets_aligned_and_in_code'])

    def test_reports_missing_table_without_reading_past_code(self):
        result = inspect(fixture()[:44], 40, 44)
        self.assertFalse(result['complete'])
        self.assertEqual(result['required_bytes'], 0x8c)
        self.assertNotIn('targets', result)

    def test_truncated_final_entry_is_incomplete(self):
        self.assertFalse(inspect(fixture()[:-1], 40, 44)['complete'])

    def test_preserves_carry_in_pc_relative_table_offset(self):
        blob = fixture()
        struct.pack_into('<I', blob, 16, 0xffff_fff4)
        result = inspect(blob, 40, 44)
        self.assertEqual(result['table_offset'], 0x1_0000_0000)
        self.assertFalse(result['complete'])

    def test_rejects_missing_carry_and_borrow(self):
        for offset in (20, 36):
            with self.subTest(offset=offset):
                blob = fixture()
                struct.pack_into('<I', blob, offset, 0xbf80_0000)
                with self.assertRaises(ValueError):
                    inspect(blob, 40, 44)

    def test_rejects_wrong_base_register(self):
        blob = fixture()
        struct.pack_into('<I', blob, 8, 0xbe88_1f00)
        with self.assertRaises(ValueError):
            inspect(blob, 40, 44)

    def test_rejects_nonzero_lgkmcnt(self):
        blob = fixture()
        struct.pack_into('<I', blob, 28, 0xbf8c_017f)
        with self.assertRaises(ValueError):
            inspect(blob, 40, 44)

    def test_rejects_signed_index_bound(self):
        blob = fixture()
        struct.pack_into('<I', blob, 0, struct.unpack_from('<I', blob)[0] ^ (1 << 23))
        with self.assertRaises(ValueError):
            inspect(blob, 40, 44)

    def test_rejects_index_clobbering_pc_pair(self):
        with self.assertRaises(ValueError):
            inspect(fixture(index=6), 40, 44)

    def test_flags_unaligned_and_out_of_range_targets(self):
        for displacement in (0x7f, 0x50, 0xffff_ffff):
            with self.subTest(displacement=displacement):
                blob = fixture()
                struct.pack_into('<I', blob, 0x80, displacement)
                self.assertFalse(inspect(blob, 40, 44)['targets_aligned_and_in_code'])

    def test_rejects_invalid_input_bounds(self):
        for pc, size in ((40, 43), (44, 44), (0, 44), (40, 0x100)):
            with self.subTest(pc=pc, size=size):
                with self.assertRaises(ValueError):
                    inspect(fixture(), pc, size)


if __name__ == '__main__':
    unittest.main()
