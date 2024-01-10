#!/usr/bin/env python3

# Copyright (c) 2023 Elliot Nunn
# Licensed under the MIT license

# Reduces the size of the average PEF by about 2%
# Not worth enabling by default

import re
import struct
import sys


def squishpef(old):
    (seccnt,) = struct.unpack_from(">H", old, 32)

    # calculate how much to copy across unharmed
    headlen = len(old)
    for i in range(seccnt):
        (secofs,) = struct.unpack_from(">L", old, 40 + 28 * i + 20)
        if secofs:
            headlen = min(headlen, secofs)

    new = bytearray(old[:headlen])

    for i in range(seccnt):
        sechdr = 40 + 28 * i

        seclen, secofs, seckind = struct.unpack_from(">LLb", new, sechdr + 16)
        sec = old[secofs : secofs + seclen]

        # convert "data" to "pidata"
        if seckind == 1:
            sec = packdata(sec)
            seckind = 2

        while len(new) % 4:  # enough alignment for code sections
            new.append(0)
        secofs = len(new)
        new.extend(sec)

        struct.pack_into(">LLb", new, sechdr + 16, len(sec), secofs, seckind)

    return new


def piarg(n):
    new = bytearray()
    nbytes = max(1, (n.bit_length() + 6) // 7)
    for shift in reversed(range(0, nbytes * 7, 7)):
        new.append(0x80 | ((n >> shift) & 0x7F))
    new[-1] &= 0x7F  # clear the "continue" bit in the last byte
    return new


def piop(op, arg):
    if 1 <= arg <= 31:
        return bytes([(op << 5) | arg])
    else:
        return bytes([(op << 5) | 0]) + piarg(arg)


def packdata(data):
    data = memoryview(data)

    # integers refer to uncompressed byte, while bytes/bytearray are compressed
    ops = []

    # first pass, greedily find compressible sections
    # (poor but serviceable algorithm)
    while data:
        if m := re.match(rb"\x00{4,}", data, re.DOTALL):  # repeat zero
            ops.append(piop(0, len(m.group(0))))
            data = data[len(m.group(0)) :]

        elif m := re.match(rb"(.)\1{5,}", data, re.DOTALL):  # repeat single byte
            ops.append(piop(2, 1) + piarg(len(m.group(0)) - 1) + bytes([data[0]]))
            data = data[len(m.group(0)) :]

        elif m := re.match(rb"(..)(?:(..)\1){2,}", data, re.DOTALL) or re.match(rb"(...)(?:(.)\1){2,}", data, re.DOTALL):  # xx12xx34xx
            comsize = len(m.group(1))
            cussize = len(m.group(2))
            rpts = (len(m.group(0)) - comsize) // (comsize + cussize)

            ops.append(piop(3, comsize))
            ops.append(piarg(cussize))
            ops.append(piarg(rpts))
            ops.append(data[:comsize])
            data = data[comsize:]

            for i in range(rpts):
                ops.append(data[:cussize])
                data = data[cussize + comsize :]

        else:
            # this one byte will be stored uncompressed
            # (i.e. squished with its neighbours into a blockmove command)
            ops.append(data[0])
            data = data[1:]

    # second pass, glom the compressible and incompressible parts together
    new = bytearray()
    i = 0
    while i < len(ops):
        if not isinstance(ops[i], int):
            # copy "custom" command from the big while loop above
            new.extend(ops[i])
            i += 1

        else:
            n = 0
            while i + n < len(ops) and isinstance(ops[i + n], int):
                n += 1

            # blockmove command of these bytes
            new.extend(piop(1, n))
            new.extend(ops[i : i + n])
            i += n

    return new


if __name__ == "__main__":
    i, o = sys.argv[1:]

    import os

#     os.system(f"~/Documents/mac/tbxi-patches/cfmtool.py {i} /tmp/testlong") # DEBUG ONLY

    pef = open(i, "rb").read()
    pef2 = squishpef(pef)
    open(o, "wb").write(pef2)

    print(f"Compressed {i} {len(pef)} -> {o} {len(pef2)} ({100*len(pef2)/len(pef):.2f}%)", file=sys.stderr)

#     os.system(f"~/Documents/mac/tbxi-patches/cfmtool.py {o} /tmp/testshort") # DEBUG ONLY
