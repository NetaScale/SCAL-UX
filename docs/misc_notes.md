- todo: put cross compiler patch in etc
- build initcode like:
```
x86_64-scalux-gcc -N -e start -Ttext 0 -o initcode.elf -nostdlib -nostdinc initcode.s
x86_64-scalux-objcopy -S -O binary initcode.elf initcode.bin
xxd -i initcode.bin
```

```
.globl start

start:
	movq $2, %rax
	movq $init, %rdi
	int $128

init:
	.string "/init\0"
```