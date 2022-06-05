ldscript = meson.current_source_dir()/'linker.ld'

vxkern = executable('vxkern', 'intr.c', 'kernel.c', 'liballoc_sysdep.c',
    'liballoc.c', 'misc.S', 'vm_machdep.c', 'vm.c', 'test.m',
    c_args : kern_c_args,
	objc_args: [ objc_args, '-fobjc-runtime=gnustep-2.0' ],
    link_args : [ '-nostdlib', '-static', '-Wl,-z,max-page-size=0x1000',
	    '-Wl,--build-id=none', '-Wl,-T' + ldscript ],
	dependencies: [ libkobjc_dep ]
)