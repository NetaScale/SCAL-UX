all:
	ninja -C build
	DESTDIR=/tmp/scalux ninja -C build install
	tar -cf build/initrd.tar -C /tmp/scalux ./


iso: all
	rm -rf build/iso_root
	mkdir -p build/iso_root
	cp build/Kernel/amd64/vxkern Kernel/amd64/limine.cfg \
		build/initrd.tar \
		Limine/limine.sys Limine/limine-cd.bin \
		Limine/limine-cd-efi.bin build/iso_root/
	xorriso -as mkisofs -b limine-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-cd-efi.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		build/iso_root -o build/barebones.iso
	build/limine-deploy build/barebones.iso
	rm -rf build/iso_root

configure:
	meson --cross-file=Builddefs/amd64.ini build

run:
	qemu-system-x86_64 build/barebones.iso  -cpu qemu64,sse,sse2,sse3 -serial stdio -smp 4 -s    -enable-kvm

runnokvm:
	qemu-system-x86_64 build/barebones.iso  -cpu qemu64,sse,sse2,sse3 -serial stdio -smp 4 -s
