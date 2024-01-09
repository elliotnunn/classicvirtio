# Copyright (c) 2023 Elliot Nunn
# Licensed under the MIT license

# Build the drivers (PPC and 68k) with:
#     PATH=Retro68-build/toolchain/bin make
all: classic ndrv
classic: build/classic/declrom
ndrv: build/ndrv/ndrvloader

# Tell make not to delete the intermediate files
# (helpful when debugging this ususual build process)
.SECONDARY:

# Create subdirectories for the build to go into
$(shell mkdir -p build/classic build/ndrv)

# The supported Virtio devices for each Mac platform (see device-9p.c etc)
#     "CLASSIC" means a 68k DRVR for a NuBus device under qemu-system-m68k
#     "NDRV" means a PowerPC NDRV for a PCI device under qemu-system-ppc
DEVICES_CLASSIC = 9p input
DEVICES_NDRV = 9p input gpu

# And these are the C files that each device-*.c depends on (some are arch-specific)
SUPPORT_CLASSIC = $(filter-out %-ndrv.c,$(filter-out slotexec-%.c,$(filter-out device-%.c,$(filter-out ndrvloader.c,$(wildcard *.c)))))
SUPPORT_NDRV = $(filter-out %-classic.c,$(filter-out slotexec-%.c,$(filter-out device-%.c,$(filter-out ndrvloader.c,$(wildcard *.c)))))

############################# CLASSIC DRVR #############################

# The classic drivers are linked together into one "card declaration ROM":
#     qemu-system-m68k -device nubus-virtio-mmio,romfile=build/classic/declrom
# This ROM incorporates the actual DRVRs and a few "slotexec" functions.
SLOTEXECS = $(patsubst slotexec-%.c,%,$(wildcard slotexec-*.c))
build/classic/declrom: declrom.s \
		$(patsubst %,build/classic/drvr-%,$(DEVICES_CLASSIC)) \
		$(patsubst %,build/classic/slotexec-%,$(SLOTEXECS))
	m68k-apple-macos-as -o build/classic/declrom.o declrom.s
	m68k-apple-macos-ld -o build/classic/declrom.elf build/classic/declrom.o
	m68k-apple-macos-objcopy -O binary -j .text build/classic/declrom.elf $@
	python3 scripts/calculatecrc.py $@

# The "slotexec" helper functions are also needed in the declaration ROM,
# but are built using different compiler options (PC-relative addressing FYI)
build/classic/slotexec-%.o: slotexec-%.c
	m68k-apple-macos-gcc -c -Os -mpcrel -o $@ $^

# Wrap an "sExecBlock" header around the slotexec code using a linker script
build/classic/slotexec-%.elf: slotexec.lds build/classic/slotexec-%.o
	m68k-apple-macos-ld -o $@ --script $^ $(DRVRSTATICLIBS)

# Extract the "sExecBlock" from an ELF, ready for inclusion in the decl ROM
build/classic/slotexec-%: build/classic/slotexec-%.elf
	m68k-apple-macos-objcopy -O binary -j .exec $^ $@

# Compile the DRVR code files (slotexec code has different compiler options)
build/classic/%.o: %.c
	m68k-apple-macos-gcc -c -Os -ffunction-sections -fdata-sections -o $@ $<

# Link each DRVR, creating the Device Manager header using a linker script.
# "Classic 68k runtime" is statically linked, so pull in a libc and some MacOS glue code.
DRVRSTATICLIBS = $(shell for x in libInterface.a libgcov.a libg.a libm.a libstdc++.a libgcc.a libc.a; do m68k-apple-macos-gcc -print-file-name=$$x; done)
build/classic/drvr-%.elf: drvr.lds build/classic/drvr.o build/classic/device-%.o $(patsubst %.c,build/classic/%.o,$(SUPPORT_CLASSIC))
	m68k-apple-macos-ld --emit-relocs --gc-sections -o $@ --script $^ $(DRVRSTATICLIBS)

# The DRVR has some assembly code to fix up interior pointers and call through to C.
build/classic/drvr.o: drvr.s
	m68k-apple-macos-as -o $@ $^

# Besides extracting the pure DRVR from an ELF file, this is an important postlink step:
# Append a list of "relocations" (i.e. pointer offsets) to be "fixed up" at runtime.
build/classic/drvr-%: build/classic/drvr-%.elf
	m68k-apple-macos-objcopy -O binary -j .drvr $^ $@
	(m68k-apple-macos-objdump -r $^ | fgrep R_68K_32; echo 00000000) \
		| cut -d ' ' -f1 \
		| python3 -c 'import sys; sys.stdout.buffer.write(bytes.fromhex(sys.stdin.read()))' \
		>>$@

############################# POWERPC NDRV #############################

# Open Firmware compatible program to load our drivers into the device tree
# (These section addresses match the Mac OS Trampoline loader, so they're safe.)
build/ndrv/ndrvloader: ndrvloader.s ndrvloader.c build/ndrv/allndrv
	powerpc-apple-macos-gcc -O2 -e entrytvec -Wl,--section-start=.data=0x100000 -Wl,--section-start=.text=0x200000 -o $@ ndrvloader.s ndrvloader.c

# Glom all the NDRVs together: the loader shim can unpick them
build/ndrv/allndrv: $(patsubst %,build/ndrv/ndrv-%,$(DEVICES_NDRV))
	cat $^ >$@

build/ndrv/%.o: %.c
	powerpc-apple-macos-gcc -c -O3 -ffunction-sections -fdata-sections -o $@ $<

NDRVSTATICLIBS = $(shell for x in libStdCLib.a libDriverServicesLib.a libNameRegistryLib.a libPCILib.a libVideoServicesLib.a libInterfaceLib.a libControlsLib.a libgcc.a libc.a; do powerpc-apple-macos-gcc -print-file-name=$$x; done)
build/ndrv/ndrv-%.so: ndrv.lds build/ndrv/device-%.o $(patsubst %.c,build/ndrv/%.o,$(SUPPORT_NDRV))
	powerpc-apple-macos-ld --gc-sections --gc-keep-exported -bE:ndrv.exp -o $@ --script $^ $(NDRVSTATICLIBS)

build/ndrv/ndrv-%: build/ndrv/ndrv-%.so
	MakePEF -o $@ $^
