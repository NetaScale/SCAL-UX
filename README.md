<img src="docs/scaluxnofont.svg" width=200/>

---

This is SCAL/UX - an operating system currently targeting amd64 PCs. It is
designed with the goal of eventually accommodating the Valutron virtual machine
(which implements a Smalltalk-like language) to run atop it.

Two major components of its kernel can currently be conceptualised:

- The executive: a lightweight system providing primitives like (preemptively-multitasked)
  processes, threads, virtual memory management, IPC, interrupt handling, etc;
- The Posix personality: provides interfaces similar to those of BSD Unix. This
  is currently part of the kernel together with the executive, and closely bound
  with it. It could eventually move into a userland server.

Future directions include a driver framework inspired by that of NeXTStep's
DriverKit. Support for loading Objective-C kernel modules is already present in
a minimal form.

The thing is a bit of a mess right now while I figure out what sort of direction
I want to go in and clean up various patchwork code that isn't where it should
be.

Design
------

The executive borrows from the concepts of Mach and NetBSD. The virtual memory
manager is largely modeled after NetBSD's UVM. Some notes on its handling of
anonymous mappings can be found in [docs/vm_notes.md].

The (very tentative as yet) VFS is similar to the design of the SunOS VFS.

Third-party components
----------------------

A few third-party components are used at the moment. These are some of them:
- liballoc: currently used for kernel `kmalloc`. I will get around to
implementing [Jeff Bonwick](https://www.usenix.org/conference/2001-usenix-annual-technical-conference/magazines-and-vmem-extending-slab-allocator-many)'s
updated slab allocator some day.
- nanoprintf: used for kernel `printf`.
- ObjFW: provides an Objective-C runtime.

To-dos
------

- better kernel heap
  - new kind of `vm_object_t` kVMKernel to link `vm_page_t`s into directly
  - expandable mappings (kVMKernel must expand without needing new allocations)
  - slab allocator to closely integrate with this
