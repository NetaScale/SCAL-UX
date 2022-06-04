ldscript = meson.current_source_dir()/'linker.ld'

vxkern = executable('vxkern', 'intr.c', 'kernel.c', 'liballoc_sysdep.c',
    'liballoc.c', 'vm_machdep.c', 'vm.c',
    c_args : [ '-I.', '-ffreestanding', '-fno-stack-protector',
	    '-fno-stack-check', '-fno-pic', '-fno-pie', '-mabi=sysv', '-mno-80387',
		'-mno-mmx', '-mno-3dnow', '-mno-sse', '-mno-sse2', '-mno-red-zone',
		'-mcmodel=kernel', '-MMD'],
    link_args : [ '-nostdlib', '-static', '-Wl,-z,max-page-size=0x1000',
	    '-Wl,--build-id=none', '-Wl,-T' + ldscript ]
)