project('SCAL/UX Kernel', ['c', 'cpp', 'objc'],
    default_options: [ 'cpp_std=c++20' ])

arch = 'x86_64'
kern_c_args = [ '-ffreestanding', '-fno-stack-protector', '-nostdlib',
	'-fno-stack-check', '-fno-pic', '-fno-pie', '-mabi=sysv', '-mno-80387',
	'-mno-mmx', '-mno-3dnow', '-mno-sse', '-mno-sse2', '-mno-red-zone',
	'-mcmodel=kernel', '-D_KERNEL' ]
kern_objc_args = [ kern_c_args ]

kern_srcs = files('kern/kmem.c', 'kern/vmem.c',
	'libkern/klib.c',
)
kern_incs = include_directories(arch + '/include', './')
subdir(arch)