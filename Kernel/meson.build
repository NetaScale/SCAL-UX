kern_srcs = files(
    'devicekit/autoconf.m', 'devicekit/DKDevice.m', 'devicekit/DKDisk.m',
    'devicekit/DKLogicalDisk.m',

    'kern/kmem.c', 'kern/ksrv.c', 'kern/liballoc_sysdep.c', 'kern/liballoc.c',
    'kern/task.c', 'kern/vm_compressor.c', 'kern/vm_kernel.c', 'kern/vm.c',
    'kern/vmem.c',

    'libkern/klib.c', 'libkern/lz4.c', 'libkern/uuid.c',

    'posix/dev.c', 'posix/event.c', 'posix/exec.c', 'posix/main.c',
    'posix/proc.c', 'posix/sys.c', 'posix/tmpfs.c', 'posix/tty.c',
    'posix/vfs.c', 'posix/vm_posix.c', 'posix/signal.c'
)

subdir('dev')
subdir(arch)
