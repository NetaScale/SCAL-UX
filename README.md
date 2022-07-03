<img src="Docs/scaluxnofont.svg" width=200/>

---

This is SCAL/UX - an operating system currently targeting amd64 PCs. It is
designed with the goal of eventually accommodating the Valutron virtual machine
(which implements a Smalltalk-like language) to run atop it.

This is a grounds-up rewrite of the lower parts of the kernel, which had turned
into a mess because of a lack of proper planning and design. The other parts
will be migrated over when the new lower parts are in a proper, reliable
conditions. Right now they are not present.

Some plans
----------

VMM:

The VMM is modeled mostly after NetBSD's UVM with some reference to Mach VMM
(and its derivates in the BSDs: macOS, )

- [ ] VM Compressor: LZ4 compression of pages as first-line destination for
  swapped-out pages.
- [ ] Swapping to files.

Allocators:

Several allocators are planned, resembling those in NetBSD. Bonwick's Slab,
VMEM, and Magazine as described in his 2001 paper are the desing basis.

- [ ] VMEM: Despite its name, not limited to allocation of virtual address
  space. It should deal in any sort of interval scale (for example, PIDs.) To
  have support for several strategies (instant fit, best fit, and next fit).
  Instant fit runs in constant time. Next fit doesn't use the power-of-2
  freelists, but instead looks for the next free segment after the last one
  allocated; useful for e.g. PID allocation to prevent excessive reuse.
- [ ] KMEM: Extended slab allocator. To use VMEM to acquire its backing virtual
  address space. SMP scalability is achieved by per-CPU caches (called
  magazines; the technique is similar to that of the Ravenbrook Memory Pool
  System's allocation points.)

Third-party components
----------------------

Several third-party components are used. These are some of them:
- mlibc: Provides libc.
- liballoc: Provides one of the in-kernel allocators.
- nanoprintf: used for `kprintf`.
- NetBSD: (`kernel/kern/queue.h`): NetBSD's `sys/queue.h`.
  - (`kernel/dev/fbterm/nbsdbold.psfu`): Bold8x16 font used for FBTerm.
- Solaris (`kernel/dev/fbterm/sun12x22.psfu`): Sun Demi Gallant font available
  for FBTerm
- ObjFW: provides an Objective-C runtime.
- Limine/`limine-terminal-port` (some files in`kernel/dev/fbterm/`): used by
  FBTerm to provide a terminal.
- Managarm LAI (`kernel/dev/acpi/lai`): Lightweight ACPI Implementation used by
  Acpi* drivers.
- LZ4 (`Kernel/libkern/lz4.{c,h}`): Used by VM Compressor to compress pages.

Licence
-------

SCAL/UX is available under the terms of the Mozilla Public Licence version 2.0.
