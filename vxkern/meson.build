ldscript = meson.current_source_dir()/'linker.ld'

vxkern = executable('vxkern', 'intr.c', 'kernel.c', 'ksrv.c', 'klibc.c',
    'liballoc_sysdep.c', 'liballoc.c', 'misc.S', 'vm_machdep.c', 'vm.c',
	'test.m',
    c_args : [ kern_c_args, objc_args_common ],
	objc_args: [ kern_c_args, objc_args ],
    link_args : [ objc_args_common, '-nostdlib', '-static', '-Wl,-z,max-page-size=0x1000',
	    '-Wl,--build-id=none', '-Wl,--hash-style=sysv', '-Wl,-T' + ldscript ],
	dependencies: [ libkobjc_dep ]
)