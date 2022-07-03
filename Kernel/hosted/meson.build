kern_srcs += 'kmain.c'

vxkern = executable('vxkern', kern_srcs,
    c_args : [ kernel_c_args ],
	objc_args: [ kernel_objc_args ],
	include_directories: [ kernel_includes ]
)
