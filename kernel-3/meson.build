project('SCAL/UX Kernel', ['c', 'cpp', 'objc', 'swift'],
    default_options: [ 'cpp_std=c++20' ])

arch = 'x86_64'
kern_c_args = [ '-ffreestanding', '-fno-stack-protector', '-nostdlib',
	'-fno-stack-check', '-fno-pic', '-fno-pie', '-mabi=sysv', '-mno-80387',
	'-mno-mmx', '-mno-3dnow', '-mno-sse', '-mno-sse2', '-mno-red-zone',
	'-mcmodel=kernel', '-D_KERNEL' ]
kern_objc_args = [ kern_c_args ]

kern_c_args += [ '--param', 'asan-globals=0', '--param', 'asan-stack=0',
  '-fsanitize=kernel-address' ]

kern_srcs = files(
  'devicekit/DKDevice.m', 'devicekit/DKDisk.m', 'devicekit/DKLogicalDisk.m',

  'ext2fs/ext2_vfsops.m',

  'kern/kasan.c', 'kern/kmem_slab.c', 'kern/liballoc.c',
  'kern/liballoc_sysdep.c', 'kern/task.c', 'kern/vmem.c',

  'libkern/klib.c', 'libkern/uuid.c',

  'posix/posix_main.c', 'posix/vfs.c',

  'tmpfs/tmpfs.c',

  'vm/vm_kernel.c','vm/vm_page.c', 'vm/vm_pageout.c', 'vm/vm.c'
)

kern_incs = [ include_directories(arch + '/include', './') ]

subdir('tools')
subdir('libkobjfw')
subdir('dev')
subdir(arch)
