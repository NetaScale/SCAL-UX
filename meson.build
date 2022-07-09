project('SCAL/UX', ['c', 'cpp', 'objc'], default_options: [ 'cpp_std=c++20' ])

arch = get_option('arch')

executable('limine-deploy', 'Limine/limine-deploy.c', native: true)

kernel_c_args = [ '-D_KERNEL' ]

if (arch == 'amd64')
    kernel_c_args += [ '-ffreestanding', '-fno-stack-protector', '-nostdlib',
            '-fno-stack-check', '-fno-pic', '-fno-pie', '-mabi=sysv', '-mno-80387',
            '-mno-mmx', '-mno-3dnow', '-mno-sse', '-mno-sse2', '-mno-red-zone',
            '-mcmodel=kernel', '-D_KERNEL' ]
elif (arch == 'hosted')
    kernel_c_args += [ '-DARCH_HOSTED' ]
endif

kernel_objc_args = kernel_c_args
kernel_includes = [ include_directories('Kernel/' + arch, 'Limine', 'Kernel') ]

if (arch != 'hosted')
    mlibc = subproject('mlibc')
    libc_includes = mlibc.get_variable('libc_include_dirs')
else
    libc_includes = [ '' ]
endif

subdir('Tools')
subdir('ObjFW/src')
subdir('Kernel')
