#!/usr/bin/env lua

-- Borrowed from https://github.com/SolraBizna/mac_qfb_driver

if #arg ~= 2 then
   print("Usage: calculate_crc.lua input.bin output.rom")
   os.exit(1)
end

local f = assert(io.open(arg[1],"rb"))
local a = f:read("*a")
f:close()
if #a < 16 then
   error("absurdly short ROM")
end

if a:sub(-8,-2) ~= "\x01\x01\x5A\x93\x2B\xC7\x00" then
   error("Input ROM isn't an Apple-format NuBus declaration ROM")
end
local rom_length = (">I"):unpack(a, #a-15)
if rom_length ~= #a then
   error("ROM is corrupt, length is wrong")
end
if a:sub(-12,-9) ~= "\x00\x00\x00\x00" then
   error("Input ROM should have a zeroed-out checksum")
end

-- Apple's documentation calls this a CRC. It is, heh, not.
local sum = 0
for n=1,#a do
   sum = ((sum << 1) & 0xFFFFFFFF) | (sum >> 31)
   sum = sum + a:byte(n)
end

print(("Declaration ROM checksum: %08X"):format(sum))
local f = assert(io.open(arg[2], "wb"))
assert(f:write(a:sub(1,#a-12),(">I"):pack(sum),a:sub(-8,-1)))
f:close()
