<img src="docs/scaluxnofont.svg" width=200/>

---

This is SCAL/UX - an operating system currently targeting amd64 PCs. It is
designed with the goal of eventually accommodating the Valutron virtual machine
(which implements a Smalltalk-like language) to run atop it.

Two major components of its kernel can currently be conceptualised:

- VXK (standing for the Valutron Executive, known in short as the Executive): a
  lightweight system providing primitives like (preemptively-multitasked)
  processes, threads, virtual memory management, IPC, interrupt handling, etc.
- The Posix personality: provides interfaces similar to those of BSD Unix. This
  is currently part of the kernel together with VXK, and closely bound
  with it. It could eventually move into a userland server, parts of it might be
  suitable for implementing in Valutron code as well.

Future directions include a driver framework inspired by that of NeXTStep's
DriverKit. Support for loading Objective-C kernel modules is already present in
a minimal form.

The thing is a bit of a mess right now while I figure out what sort of direction
I want to go in and clean up various patchwork code that isn't where it should
be.

Design
------

The executive (VXK) borrows from the concepts of Mach and NetBSD. The virtual
memory manager is largely modeled after NetBSD's UVM. Some notes (mainly on its
handling of anonymous mappings can be found in [docs/vm_notes.md].

### Concepts of the Executive

The core concepts are four:
  - **Process**: a logical unit composed of an address space map and a set of
  Rights, in which Threads may run.
  - **Thread**: A thread of execution within a process.
  - **Message**: An asynchronously-sent unit of communication.
  - **Right**: A capability by which messages may be either received or sent.

Rights refer to underlying mailboxes and vary in type: some permit sending,
others receiving, and a filter may be attached, yielding a Filtered Right, which
may permit only certain kinds of messages to be sent or received.

### Concepts of the Posix personality

The Posix personality is structured as a fairly typical Unix running atop the
underlying Executive.
Its (very tentative as yet) VFS is similar to the design of the SunOS VFS.

Third-party components
----------------------

A few third-party components are used. These are some of them:
- liballoc: currently used for kernel `kmalloc`. I will get around to
implementing [Jeff Bonwick](https://www.usenix.org/conference/2001-usenix-annual-technical-conference/magazines-and-vmem-extending-slab-allocator-many)'s
updated slab allocator some day.
- mlibc: Provides a libc.
- nanoprintf: used for kernel `printf`.
- NetBSD: (`kernel/kern/queue.h`): NetBSD's `sys/queue.h`.
  - (`kernel/dev/fbterm/nbsdbold.psfu`): Bold8x16 font used for FBTerm.
- Solaris (`kernel/dev/fbterm/sun12x22.psfu`): Sun Demi Gallant font available
  for FBTerm
- ObjFW: provides an Objective-C runtime.
- Limine/`limine-terminal-port` (some files in`kernel/dev/fbterm/`): used by
  FBTerm to provide a terminal.
- Managarm LAI (`kernel/dev/acpi/lai`): Lightweight ACPI Implementation used by
  Acpi* drivers.

Licence
-------

SCAL/UX is available under the terms of the Mozilla Public Licence version 2.0.

To-dos
------

- better kernel heap
  - new kind of `vm_object_t` kVMKernel to link `vm_page_t`s into directly
  - expandable mappings (kVMKernel must expand without needing new allocations)
  - replace liballoc with a slab allocator that integrates with this
