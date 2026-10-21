# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import json
import struct
from pathlib import Path


def word(data, offset):
    if offset < 0 or offset % 4 or offset + 4 > len(data):
        raise ValueError(f'Missing or unaligned dword at {offset:#x}')
    return struct.unpack_from('<I', data, offset)[0]


def sop2(token):
    if token >> 30 != 2:
        raise ValueError('Expected SOP2 instruction')
    return (token >> 23) & 0x7f, (token >> 16) & 0x7f, token & 0xff, (token >> 8) & 0xff


def inline_integer(operand):
    if 128 <= operand <= 192:
        return operand - 128
    if 193 <= operand <= 208:
        return -(operand - 192)
    raise ValueError('Expected inline integer constant')


def inspect(data, branch_pc, code_size):
    if code_size < 4 or code_size % 4 or code_size > len(data):
        raise ValueError('Invalid instruction byte length')
    if branch_pc < 40 or branch_pc + 4 > code_size:
        raise ValueError('Branch is outside the instruction span')
    setpc = word(data, branch_pc)
    if setpc & 0xffff_ff00 != 0xbe80_2000:
        raise ValueError('Expected S_SETPC_B64')
    pair = setpc & 0xff
    if pair > 102 or pair % 2:
        raise ValueError('Expected an aligned scalar register pair')
    getpc_pc = branch_pc - 32
    if word(data, getpc_pc) != 0xbe80_1f00 | (pair << 16):
        raise ValueError('Expected S_GETPC_B64 into the branch register pair')
    if sop2(word(data, branch_pc - 28)) != (0, pair, 255, pair):
        raise ValueError('Expected literal low-word addition')
    addend = word(data, branch_pc - 24)
    if sop2(word(data, branch_pc - 20)) != (4, pair + 1, 128, pair + 1):
        raise ValueError('Expected carry propagation into the high word')
    load = word(data, branch_pc - 16)
    if load >> 22 != 0x300 or load & 0x100 or (load >> 9) & 0x3f != pair // 2:
        raise ValueError('Expected register-offset S_LOAD_DWORD from the PC-relative base')
    index = load & 0xff
    loaded = (load >> 15) & 0x7f
    if index > 103 or loaded > 103:
        raise ValueError('Expected scalar GPRs for the table index and loaded value')
    if word(data, branch_pc - 12) & 0xffff_0000 != 0xbf8c_0000:
        raise ValueError('Expected S_WAITCNT')
    if word(data, branch_pc - 12) & 0xf00:
        raise ValueError('Expected lgkmcnt(0) before consuming the scalar load')
    if sop2(word(data, branch_pc - 8)) != (1, pair, pair, loaded):
        raise ValueError('Expected low-word subtraction of the loaded displacement')
    if sop2(word(data, branch_pc - 4)) != (5, pair + 1, pair + 1, 128):
        raise ValueError('Expected borrow propagation into the high word')
    if sop2(word(data, branch_pc - 36)) != (30, index, index, 130):
        raise ValueError('Expected a dword-scaled table index')
    minimum = sop2(word(data, branch_pc - 40))
    if minimum[:2] != (7, index):
        raise ValueError('Expected an unsigned bounded table index')
    maximum = inline_integer(minimum[3])
    if maximum < 0 or maximum > 64:
        raise ValueError('Invalid table bound')
    if index in (pair, pair + 1) or loaded in (pair, pair + 1):
        raise ValueError('Table index or loaded displacement clobbers the PC base')
    table = getpc_pc + 4 + addend
    count = maximum + 1
    required = table + count * 4
    result = {
        'branch_pc': branch_pc, 'getpc_pc': getpc_pc, 'table_offset': table,
        'entry_count': count, 'code_bytes': code_size, 'captured_bytes': len(data),
        'required_bytes': required, 'complete': required <= len(data),
    }
    if not result['complete']:
        return result
    offsets = [word(data, table + i * 4) for i in range(count)]
    targets = [table - offset for offset in offsets]
    result['displacements'] = offsets
    result['targets'] = targets
    result['targets_aligned_and_in_code'] = all(0 <= t < code_size and t % 4 == 0 for t in targets)
    return result


def main():
    parser = argparse.ArgumentParser(description='Inspect a bounded GCN PC-relative jump table')
    parser.add_argument('binary', type=Path)
    parser.add_argument('--branch-pc', type=lambda s: int(s, 0), required=True)
    parser.add_argument('--code-size', type=lambda s: int(s, 0), required=True)
    args = parser.parse_args()
    try:
        result = inspect(args.binary.read_bytes(), args.branch_pc, args.code_size)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))
    if not result['complete'] or not result['targets_aligned_and_in_code']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
