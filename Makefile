all:
	ninja -C build
	DESTDIR=/tmp/scalux ninja -C build install
	tar -cvf build/initrd.tar -C /tmp/scalux ./
	./geniso.sh

run:
	qemu-system-x86_64 build/barebones.iso  -cpu qemu64,sse,sse2,sse3 -serial stdio -smp 4 -s    -enable-kvm
