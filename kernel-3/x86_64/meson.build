ldscript = meson.current_source_dir()/'linker.ld'

kern_srcs += files ('intr.c', 'kmain.c', 'locore.S', 'vm.c')

vxkern = executable('vxkern', kern_srcs,
    c_args : [ kern_c_args ],
	objc_args: [ kern_objc_args ],
    link_args : [ '-nostdlib', '-static',
	  '-Wl,-z,max-page-size=0x1000', '-Wl,--build-id=none',
	  '-Wl,-T' + ldscript ],
    include_directories: [ kern_incs ],
    #include_directories: [ kernel_includes, libc_includes ],
    #dependencies: [ libkobjc_dep ]
)
