project('Valutron', ['c', 'cpp', 'objc'])

cc = 'clang'
objc = 'clang'

kern_c_args = [ '-I.', '-ffreestanding', '-fno-stack-protector', '-nostdlib',
	    '-fno-stack-check', '-fno-pic', '-fno-pie', '-mabi=sysv', '-mno-80387',
		'-mno-mmx', '-mno-3dnow', '-mno-sse', '-mno-sse2', '-mno-red-zone',
		'-mcmodel=kernel', '-MMD']

mod_c_args = [ '-ffreestanding', '-fno-stack-protector', '-nostdlib',
	    '-fno-stack-check', '-mabi=sysv', '-mno-80387',
		'-mno-mmx', '-mno-3dnow', '-mno-sse', '-mno-sse2', '-mno-red-zone',
		'-mcmodel=kernel', '-MMD']

vxkern_incs = include_directories('vxkern')

subdir('vendor/objfw/src')
subdir('vxkern')
subdir('TestDriver')