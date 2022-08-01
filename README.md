<img src="Docs/scaluxnofont.svg" width=200/>

---

**SCAL/UX**™ - the operating system for those who take scalability seriously

Welcome to the homepage of SCAL/UX™ operating system, a high-performance,
low-cost operating system providing a complete solution for enterprise-level
applications. SCAL/UX provides a complete UNIX®-like environment with a full set
of industry-standard tools and applications on the basis of its object-oriented
kernel.

That concludes the facetious part of the readme.

Welcome to the SCAL/UX repository.

***The First Hobbyist's Operating System with Virtual Memory Compression***

This is an operating system currently targeting amd64 PCs. It is
designed with the goal of eventually accommodating the Valutron virtual machine
(which implements a Smalltalk-like language) to run atop it.

This is a grounds-up rewrite of the lower parts of the kernel, which had turned
into a mess because of a lack of proper planning and design. The other parts
will be migrated over when the new lower parts are in a proper, reliable
conditions. Right now they are not present.

Some plans
----------

#### VMM

The VMM is modeled mostly after NetBSD's UVM with some reference to Mach VMM
(and its derivates in the BSDs: macOS, FreeBSD, etc.)

- [x] (initial PoC only) VM Compressor: LZ4 compression of pages as first-line
  destination for swapped-out pages (Linux calls this ZRam).
- [ ] Swapping to files.

#### Allocators

Several allocators are planned, resembling those in NetBSD. The design basis is
Bonwick and Adams' (2001) paper "Magazines and Vmem: Extending the Slab
Allocator to Many CPUs and Arbitrary Resources."

- [x] VMem: Despite its name, not limited to allocation of virtual address
  space. It should deal in any sort of interval scale (for example, PIDs.) To
  have support for several strategies (instant fit, best fit, and next fit).
  Instant fit runs in constant time. Next fit doesn't use the power-of-2
  freelists, but instead looks for the next free segment after the last one
  allocated; useful for e.g. PID allocation to prevent excessive reuse.
- [ ] KMem: Extended slab allocator. To use VMem to acquire its backing virtual
  address space. SMP scalability is achieved by per-CPU caches (called
  magazines; the technique is similar to that of the Ravenbrook Memory Pool
  System's allocation points.)
- [x] kmalloc: General-purpose for odd sizes. Currently provided by liballoc.
  VMem itself could possibly replace it?
- [ ] Better physical page allocation - currently just uses a single system-wide
  free list. Would be nice if it were NUMA-aware, could allocate contiguous
  spans of physical pages, and perhaps subject to constraints on location (e.g.
  for DMA buffers).

VMem, KMem, and the virtual memory manager itself have an interesting mutual
dependency on one-another; VMem must get boundary tag structures allocated by
KMem, which gets VMem to dole out memory which must be backed with pages by the
virtual memory manager. To eliminate the endless recursion, nested calls into
VMem set a flag which instructs VMem to use a local cache of free boundary tag
structures; this cache must be filled with extra structures (enough to satisfy
the entire operation) before any non-nested operation to ensure that no infinite
loop can occur.

#### DeviceKit

Object-oriented driver framework. Inspired by NeXTSTEP's DriverKit, Apple
I/OKit/DriverKit, NetBSD autoconf.

- Storage: Stack approach used. See [static hierarchy] and [runtime stack] for
  an illustration of how NVMe class hierarchy and stack look.
- [ ] Need some way to take a set of pages, mark them busy so they can't be
  interfered with, and pass along to (only a single at once; i.e. only one thing
  can operate on them?) other subsystems for processing. This is really a VMM
  thing but DeviceKit will probably be the first to take advantage.

  Major reason why: NVMe's PRPs give scatter-gather capabilities; we can do
  zero-copy I/O directly into the page cache if we want.

[static hierarchy]: Docs/storage_hier.png
[runtime stack]: Docs/storage_runtime.png

Misc todos
----------

- [ ] Abolish vm_map_t's queue, use VMem instead; VMem to maintain a red-n-black
  tree of its regions so that lookup can be done easily.

Third-party components
----------------------

Several third-party components are used. These are some of them:
- mlibc: Provides libc.
- liballoc: Provides one of the in-kernel allocators.
- nanoprintf: used for `kprintf`.
- NetBSD: (`kernel/sys/queue.h`): NetBSD's `sys/queue.h`.
  - (`kernel/dev/fbterm/nbsdbold.psfu`): Bold8x16 font used for FBTerm.
  - (`Kernel/dev/nvmereg.h`): NVMe register definitions.
- Solaris (`kernel/dev/fbterm/sun12x22.psfu`): Sun Demi Gallant font available
  for FBTerm
- ObjFW: provides an Objective-C runtime.
- Limine/`limine-terminal-port` (some files in`kernel/dev/fbterm/`): used by
  FBTerm to provide a terminal.
- Managarm LAI (`kernel/dev/acpi/lai`): Lightweight ACPI Implementation used by
  Acpi* drivers.
- LZ4 (`Kernel/libkern/lz4.{c,h}`): Used by VM Compressor to compress pages.
- libuuid (`Kernel/libkern/uuid*`)

Licence
-------

SCAL/UX is available under the terms of the Mozilla Public Licence version 2.0.
