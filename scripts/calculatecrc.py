#!/usr/bin/env python3

# Copyright (c) 2023 Elliot Nunn
# Licensed under the MIT license

# Referred to https://github.com/SolraBizna/mac_qfb_driver

import sys

myself, path = sys.argv

with open(path, "r+b") as f:
    rom = bytearray(f.read())

    if len(rom) < 16:
        sys.exit("absurdly short rom")

    if rom[-8:-2] != b"\x01\x01\x5A\x93\x2B\xC7":
        sys.exit("wrong format/magic number")

    rom[-12:-8] = b"\0\0\0\0"  # clear sum field for calculation

    cksum = 0
    for byte in rom:
        cksum = ((cksum << 1) & 0xFFFFFFFF) | (cksum >> 31)
        cksum += byte

    # write sum field direct to file
    f.seek(len(rom) - 12)
    f.write(cksum.to_bytes(4, "big"))
