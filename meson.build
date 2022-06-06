project('Valutron', ['c', 'cpp', 'objc'])

cc = 'clang'
objc = 'clang'
arch = 'x86_64'

kern_c_args = [ '-I.', '-ffreestanding', '-fno-stack-protector', '-nostdlib',
	    '-fno-stack-check', '-fno-pic', '-fno-pie', '-mabi=sysv', '-mno-80387',
		'-mno-mmx', '-mno-3dnow', '-mno-sse', '-mno-sse2', '-mno-red-zone',
		'-mcmodel=kernel', '-MMD']

mod_c_args = [ '-ffreestanding', '-fno-stack-protector', '-nostdlib',
	    '-fno-stack-check', '-mabi=sysv', '-mno-80387',
		'-mno-mmx', '-mno-3dnow', '-mno-sse', '-mno-sse2', '-mno-red-zone',
		'-mcmodel=kernel', '-MMD']

objc_args_common = [ '-Xclang', '-fobjc-runtime=objfw', '-Xclang', '-fblocks' ]

objc_args = [ objc_args_common, '-fexceptions', '-fobjc-exceptions',
    '-funwind-tables', '-fconstant-string-class=OFConstantString', '-std=gnu11',
	'-fno-common', '-Xclang', '-fno-constant-cfstrings' ]

libc_incs = include_directories('Libc/include')
vxkern_incs = include_directories('kernel')

subdir('Libc')
subdir('vendor/objfw/src')
subdir('kernel')
#subdir('kernel')
subdir('TestDriver')