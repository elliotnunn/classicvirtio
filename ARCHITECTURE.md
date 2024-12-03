# Layers

```
┌──────────────────────────────┐ ┌──────────────────────────────┐ ┌──────────────────────────────┐
│  68K Device Manager          │ │  PowerPC Device Manager      │ │  Other Mac OS services       │
│                              │ │                              │ │                              │
│ (on qemu-system-m68k)        │ │ (on qemu-system-ppc)         │ │ FileMgr, EventMgr            │
└──────────────────────────────┘ └──────────────────────────────┘ │                              │
┌───────────────────────────────────────────────────────────────┐ │                              │
│  runtime-*.c                                                  │ │                              │
└───────────────────────────────────────────────────────────────┘ └──────────────────────────────┘
┌──────────────────────────────┐ ┌──────────────────────────────┐ ┌──────────────────────────────┐
│  device-9p.c                 │ │  device-input.c              │ │  device-gpu.c                │
│                              │ │                              │ │                              │
│ 9P filesystem driver         │ │ Tablet input driver          │ │ Video driver                 │
│                              │ │                              │ │                              │
└──────────────────────────────┘ │                              │ │                              │
┌──────────────────────────────┐ │                              │ │                              │
│  multifork-*.c               │ │                              │ │                              │
│                              │ │                              │ │                              │
│ Fake resource forks and      │ │                              │ │                              │
│ Finder info in various disk  │ │                              │ │                              │
│ formats (AppleDouble, Rez)   │ │                              │ │                              │
│                              │ │                              │ │                              │
└──────────────────────────────┘ │                              │ │                              │
┌──────────────────────────────┐ │                              │ │                              │
│  catalog.c                   │ │                              │ │                              │
│                              │ │                              │ │                              │
│ Map 32bit "catalog node IDs" │ │                              │ │                              │
│ to conventional pathnames    │ │                              │ │                              │
│                              │ │                              │ │                              │
└──────────────────────────────┘ │                              │ │                              │
┌─────────────┬────────────────┐ │                              │ │                              │
│             │  9buf.c        │ │                              │ │                              │
│  9p.c       │                │ │                              │ │                              │
│             │ Buffered IO    │ │                              │ │                              │
│             └────────────────┤ │                              │ │                              │
│ Plan 9 filesystem protocol   │ │                              │ │                              │
│ as a C API                   │ │                              │ │                              │
│                              │ │                              │ │                              │
└──────────────────────────────┘ └──────────────────────────────┘ └──────────────────────────────┘
┌────────────────────────────────────────────────────────────────────────────────────────────────┐
│  virtqueue.c                                                                                   │
│                                                                                                │
│ Platform-independent Virtio queue layer                                                        │
└────────────────────────────────────────────────────────────────────────────────────────────────┘
┌────────────────────────────────────────────────────────────────────────────────────────────────┐
│  transport-*.c                                                                                 │
│                                                                                                │
│ Platform-dependent Virtio transport layer                                                      │
└────────────────────────────────────────────────────────────────────────────────────────────────┘
┌──────────────────────────────────────────────┐  ┌──────────────────────────────────────────────┐
│  NuBus Virtio device                         │  │  PCI Virtio device                           │
│ (on qemu-system-m68k)                        │  │ (on qemu-system-ppc)                         │
│                                              │  │                                              │
└──────────────────────────────────────────────┘  └──────────────────────────────────────────────┘
```

# 9P fids

The canonical text on 9p "file IDs" is here:
http://ericvh.github.io/9p-rfc/rfc9p2000.html

Our 9P layer keeps a bitmask of the fids 0 through 31, and when it
catches an invalid attempt to open an already-open one, it transparently
issues a Tclunk first, to make everything succeed. These 32 special fids
are distributed among source files manually:

- 0-31 are auto-closed by 9p.c when re-use is attempted
- 0 = root
- 1 = .classicvirtio.nosync.noindex
- 2-7 = device-9p.c
- 8-15 = multifork-\*.c
- 16-23 = catalog.c
