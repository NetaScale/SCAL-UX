ldscript = meson.current_source_dir()/'linker.ld'

vxkern = executable('vxkern', 'intr.c', 'kmain.c',
	  'misc.S', 'proc_machdep.c', 'vm_machdep.c',
	kern_srcs,
    c_args : [ kern_c_args ],
	objc_args: [ kern_objc_args ],
    link_args : [ '-nostdlib', '-static',
	  '-Wl,-z,max-page-size=0x1000', '-Wl,--build-id=none',
	  '-Wl,-T' + ldscript ],
	dependencies: [ libkobjc_dep ],
	include_directories: [ libc_incs, vxkern_incs, kern_incs, '.' ]
)
