TestDriver = shared_module('TestDriver', 'test.m',
    c_args : [ mod_c_args, objc_args_common, '-fPIC' ],
	include_directories: [ vxkern_incs, libc_incs ],
	objc_args: [ mod_c_args, kern_objc_args, '-fPIC' ],
    link_args : [ objc_args_common, '-nostdlib', '-fPIC', '-Wl,--hash-style=sysv',
	  '-Wl,-z,max-page-size=0x1000', '-Wl,--allow-shlib-undefined' ]
	#dependencies: [ libkobjc_dep ]
)