#!/usr/bin/env python3
"""
Dump dialogue text strings from Exult's USECODE data files.

USECODE file contains compiled usecode functions. Each function has:
  - Function ID (2B LE, or extended with 0xfffe/0xffff marker)
  - Length (2B or 4B LE)
  - Code buffer:
      - Data segment (text data with null-terminated strings)
      - num_args, num_vars, num_externs (2B each)
      - Externs table (2 * num_externs B)
      - Actual bytecode

This tool iterates all functions and extracts the null-terminated strings
from the data segment of each function.
"""

import struct
import sys
import os
from datetime import datetime


def read_u16(data, offset):
    """Read a 16-bit unsigned little-endian integer."""
    return struct.unpack_from('<H', data, offset)[0], offset + 2


def read_u32(data, offset):
    """Read a 32-bit unsigned little-endian integer."""
    return struct.unpack_from('<I', data, offset)[0], offset + 4


def extract_strings_from_data_segment(data_seg):
    """Extract null-terminated printable strings from a data segment."""
    strings = []
    current = []
    for byte in data_seg:
        if byte == 0:
            if current:
                s = bytes(current).decode('latin-1', errors='replace')
                # Filter: at least 3 chars and printable
                if len(s) >= 3 and all(32 <= ord(c) < 127 or c in 'àáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ' for c in s):
                    strings.append(s)
                current = []
        else:
            current.append(byte)
    # Handle trailing string without null terminator
    if current:
        s = bytes(current).decode('latin-1', errors='replace')
        if len(s) >= 3 and all(32 <= ord(c) < 127 or c in 'àáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ' for c in s):
            strings.append(s)
    return strings


def parse_usecode(filepath):
    """Parse a USECODE file, returning list of (func_id, strings) tuples."""
    with open(filepath, 'rb') as f:
        data = f.read()

    functions = []
    offset = 0
    filesize = len(data)

    while offset < filesize:
        func_start = offset

        # Read function ID (2 bytes)
        if offset + 2 > filesize:
            break

        func_id, offset = read_u16(data, offset)
        extended = False
        id_is_32bit = False

        if func_id == 0xfffe:
            # Extended format: 32-bit ID and length
            id_is_32bit = True
            extended = True
            if offset + 4 > filesize:
                break
            func_id, offset = read_u32(data, offset)
            if offset + 4 > filesize:
                break
            func_len, offset = read_u32(data, offset)
        elif func_id == 0xffff:
            # Older extended: 16-bit ID (read again), 32-bit length
            extended = True
            if offset + 2 > filesize:
                break
            func_id, offset = read_u16(data, offset)
            if offset + 4 > filesize:
                break
            func_len, offset = read_u32(data, offset)
        else:
            # Standard format: 16-bit length
            if offset + 2 > filesize:
                break
            func_len, offset = read_u16(data, offset)

        # Read the code buffer
        if offset + func_len > filesize:
            # Truncated, but try to read what's left
            func_len = filesize - offset

        code_buf = data[offset:offset + func_len]
        offset += func_len

        if func_len < 6:  # Too small to have a data segment header
            continue

        # Parse data segment from code buffer
        code_offset = 0
        if not extended:
            data_len, code_offset = read_u16(code_buf, code_offset)
        else:
            data_len, code_offset = read_u32(code_buf, code_offset)

        if data_len == 0 or data_len > func_len:
            continue

        # Extract the data segment
        data_seg = code_buf[code_offset:code_offset + data_len]

        # Extract strings from data segment
        strings = extract_strings_from_data_segment(data_seg)

        if strings:
            functions.append((func_id, strings, data_seg))

    return functions


def is_dialogue_likely(s):
    """Heuristic: check if a string looks like dialogue text."""
    # Skip if all uppercase/short
    if s.isupper() and len(s) < 20:
        return False
    # Skip if it's just numbers
    if s.replace(' ', '').isdigit():
        return False
    # Strings with spaces, punctuation, mixed case are likely dialogue
    has_lower = any(c.islower() for c in s)
    has_space = ' ' in s
    return has_lower or has_space or len(s) > 30


def main():
    import argparse

    parser = argparse.ArgumentParser(description='Dump dialogue text from USECODE files')
    parser.add_argument('usecode_file', nargs='?',
                        default=r'D:\Project\uc\ULTIMA7\STATIC\USECODE',
                        help='Path to USECODE file')
    parser.add_argument('-o', '--output', default=None,
                        help='Output file path (default: auto-named based on input)')
    parser.add_argument('--all-strings', action='store_true',
                        help='Dump ALL strings, not just dialogue-likely ones')
    parser.add_argument('--no-dedup', action='store_true',
                        help='Do NOT deduplicate strings across functions')
    parser.add_argument('--min-length', type=int, default=3,
                        help='Minimum string length to include (default: 3)')
    parser.add_argument('--by-function', action='store_true',
                        help='Group strings by function ID (includes short/non-dialogue strings)')

    args = parser.parse_args()

    if not os.path.exists(args.usecode_file):
        print(f"Error: File not found: {args.usecode_file}")
        sys.exit(1)

    basename = os.path.splitext(os.path.basename(args.usecode_file))[0]
    output_path = args.output or f'{basename}_dialogue_dump.txt'

    print(f"Parsing: {args.usecode_file}")
    print(f"File size: {os.path.getsize(args.usecode_file):,} bytes")
    print()

    functions = parse_usecode(args.usecode_file)
    print(f"Found {len(functions)} functions with string data")

    total_strings = sum(len(f[1]) for f in functions)
    print(f"Total strings (raw): {total_strings}")

    if args.by_function:
        # Output grouped by function ID
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(f"# USECODE Dialogue Text Dump\n")
            f.write(f"# File: {args.usecode_file}\n")
            f.write(f"# Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
            f.write(f"# Total functions with text: {len(functions)}\n")
            f.write(f"# Format: [FuncID:0xNNNN] string\n")
            f.write(f"# T = trimmed (too short or utility), D = dialogue, S = short/other\n")
            f.write(f"{'='*80}\n\n")

            dialogue_count = 0
            trimmed_count = 0
            for func_id, strings, _ in sorted(functions, key=lambda x: x[0]):
                f.write(f"--- Function 0x{func_id:04X} ({func_id}) ---\n")
                for s in strings:
                    if len(s) < args.min_length:
                        f.write(f"  [T:trimmed] {s}\n")
                        trimmed_count += 1
                    elif is_dialogue_likely(s):
                        f.write(f"  [D:dialog] {s}\n")
                        dialogue_count += 1
                    else:
                        f.write(f"  [S:short ] {s}\n")
                        trimmed_count += 1
                f.write("\n")

            f.write(f"\n{'='*80}\n")
            f.write(f"# Summary: {dialogue_count} dialogue strings, {trimmed_count} trimmed/utility strings\n")
    else:
        # Deduplicated flat list
        all_strings = []
        for func_id, strings, _ in functions:
            for s in strings:
                if len(s) >= args.min_length:
                    all_strings.append(s)

        if not args.no_dedup:
            # Deduplicate while preserving order
            seen = set()
            unique_strings = []
            for s in all_strings:
                if s not in seen:
                    seen.add(s)
                    unique_strings.append(s)
            all_strings = unique_strings

        if not args.all_strings:
            all_strings = [s for s in all_strings if is_dialogue_likely(s)]

        # Sort alphabetically
        all_strings.sort()

        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(f"# USECODE Dialogue Text Dump\n")
            f.write(f"# File: {args.usecode_file}\n")
            f.write(f"# Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
            f.write(f"# Total strings: {len(all_strings)}\n")
            f.write(f"# Filter: {'dialogue-likely only' if not args.all_strings else 'all strings'}, "
                    f"{'deduplicated' if not args.no_dedup else 'not deduplicated'}\n")
            f.write(f"{'='*80}\n\n")

            for i, s in enumerate(all_strings, 1):
                f.write(f"{i:5d}. {s}\n")

        print(f"Output ({'dialogue-likely' if not args.all_strings else 'all'}): {len(all_strings)} strings")
        print(f"Written to: {output_path}")


if __name__ == '__main__':
    main()
