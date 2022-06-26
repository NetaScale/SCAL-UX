kern_srcs = files(
  'devicekit/objc.m', 'devicekit/autoconf.m',
  'devicekit/DKDevice.m',

  'kern/klibc.c', 'kern/ksrv.c', 'kern/liballoc_sysdep.c', 'kern/liballoc.c',
  'kern/process.c', 'kern/sched.c', 'kern/vm.c', 'kern/vm_fault.c',
  'kern/vm_fork.c', 'kern/vm_object.c', 'kern/vm_pager.c', 'kern/waitq.c',

  'kern/test_vm.c',

  'posix/dev.c', 'posix/exec.c', 'posix/main.c', 'posix/posix_proc.c',
  'posix/sys.c', 'posix/vfs.c', 'posix/tmpfs.c', 'posix/tty.c',
  'posix/vm_posix.c')
subdir('dev')
subdir(arch)
