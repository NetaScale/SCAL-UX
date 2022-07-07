ldscript = meson.current_source_dir()/'linker.ld'

kern_srcs += [ 'intr.c', 'kmain.c', 'vm_machdep.c', 'locore.S']

vxkern = executable('vxkern', kern_srcs,
    c_args : [ kernel_c_args ],
	objc_args: [ kernel_objc_args ],
    link_args : [ '-nostdlib', '-static',
	  '-Wl,-z,max-page-size=0x1000', '-Wl,--build-id=none',
	  '-Wl,-T' + ldscript ],
	include_directories: [ kernel_includes, libc_includes ],
	dependencies: [ libkobjc_dep ]
)
