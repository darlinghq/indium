#!/usr/bin/env python3
#
# Copyright (C) 2026 Darling Developers
#
# Distributed under the terms in the LICENSE file at the root of this repository.
#
# Wraps one AIR bitcode module in the MTLB container that Iridium's AIR::Library reads.
#
#     make-metallib.py <module.air> <output.metallib> <type>:<function> [<type>:<function> ...]
#
# <type> is vertex, fragment or kernel. Every function entry points at the same module. The
# function hashes and the metadata blocks are left empty, since Iridium does not read them.

import struct
import sys

FUNCTION_TYPES = { "vertex": 0, "fragment": 1, "kernel": 2 }
HEADER_SIZE = 0x58


def tag(name, payload):
	return name.encode("ascii") + struct.pack("<H", len(payload)) + payload


def function_entry(name, function_type, bitcode_size):
	body = b"".join([
		tag("NAME", name.encode("utf-8") + b"\0"),
		tag("TYPE", struct.pack("<B", function_type)),
		tag("MDSZ", struct.pack("<Q", bitcode_size)),
		# public metadata, private metadata and bitcode offsets
		tag("OFFT", struct.pack("<QQQ", 0, 0, 0)),
		b"ENDT",
	])
	# the group size counts its own four bytes
	return struct.pack("<I", len(body) + 4) + body


def main(argv):
	if len(argv) < 4:
		sys.exit("usage: make-metallib.py <module.air> <output.metallib> <type>:<function>...")

	with open(argv[1], "rb") as f:
		bitcode = f.read()

	entries = []
	for spec in argv[3:]:
		kind, _, name = spec.partition(":")
		if kind not in FUNCTION_TYPES or not name:
			sys.exit("invalid function specification: " + spec)
		entries.append(function_entry(name, FUNCTION_TYPES[kind], len(bitcode)))

	function_list = struct.pack("<I", len(entries)) + b"".join(entries)
	function_list_offset = HEADER_SIZE
	metadata_offset = function_list_offset + len(function_list)
	bitcode_offset = metadata_offset
	file_size = bitcode_offset + len(bitcode)

	header = b"MTLB"
	# platform, file version 1.2, executable library, macOS 10.15
	header += struct.pack("<HHHBBHH", 0x8001, 1, 2, 0, 0x81, 10, 15)
	header += struct.pack("<QQQ", file_size, function_list_offset, len(function_list))
	header += struct.pack("<QQQQ", metadata_offset, 0, metadata_offset, 0)
	header += struct.pack("<QQ", bitcode_offset, len(bitcode))
	assert len(header) == HEADER_SIZE

	with open(argv[2], "wb") as f:
		f.write(header + function_list + bitcode)


if __name__ == "__main__":
	main(sys.argv)
