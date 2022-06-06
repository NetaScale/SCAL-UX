![Valutron](https://github.com/NetaScale/Valutron/raw/main/docs/logotype.png)
==============================

This is the Valutron Executive (or VXK) - the part of a Valutron native system
which runs on the bare metal. It provides a basic set of services (memory
management virtual and physical, pre-emptive multitasking, etc) sufficient for
the Valutron virtual machine to operate, as well as primitives which for reasons
of performance are better-implemented in C than Valutron code.

It also provides a part of the Valutron Subsystem for UNIX-based Applications
(or VSUA), a compatibility layer to allow some applications written to the
POSIX standard to run as native processes.

The VXK targets only x86_64 PCs for now.