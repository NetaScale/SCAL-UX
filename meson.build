project('SCAL/UX', ['c', 'cpp', 'objc'])

arch = 'amd64'

kern_c_args = [ '-I.', '-ffreestanding', '-fno-stack-protector', '-nostdlib',
	    '-fno-stack-check', '-fno-pic', '-fno-pie', '-mabi=sysv', '-mno-80387',
		'-mno-mmx', '-mno-3dnow', '-mno-sse', '-mno-sse2', '-mno-red-zone',
		'-mcmodel=kernel', '-D_KERNEL' ]

mod_c_args = [ '-ffreestanding', '-fno-stack-protector', '-nostdlib',
	    '-fno-stack-check', '-mabi=sysv', '-mno-80387',
		'-mno-mmx', '-mno-3dnow', '-mno-sse', '-mno-sse2', '-mno-red-zone',
		'-mcmodel=kernel', '-D_KERNEL' ]

objc_args_common = [ '-Xclang', '-fobjc-runtime=objfw', '-Xclang', '-fblocks' ]

kern_objc_args = [ kern_c_args, '-fno-exceptions', '-fno-objc-exceptions',
    '-funwind-tables', '-fconstant-string-class=OFConstantString', '-std=gnu11',
	'-fno-common', '-Xclang', '-fno-constant-cfstrings', '-nostdinc' ]

vxkern_incs = include_directories('kernel')

mlibc = subproject('mlibc', default_options: [
	#'static=true',
	#'headers_only=true'
])
libc_incs = mlibc.get_variable('libc_include_dirs')
subdir('tools')
subdir('vendor/objfw/src')
subdir('kernel')
subdir('TestDriver')
