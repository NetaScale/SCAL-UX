rm vendor/limine/limine-deploy
make -C vendor/limine
	rm -rf build/iso_root
	mkdir -p build/iso_root
	cp build/kernel/amd64/vxkern build/TestDriver/libTestDriver.so \
		build/initrd.tar \
		limine.cfg vendor/limine/limine.sys vendor/limine/limine-cd.bin \
		vendor/limine/limine-cd-efi.bin build/iso_root/
	xorriso -as mkisofs -b limine-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-cd-efi.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		build/iso_root -o build/barebones.iso
	vendor/limine/limine-deploy build/barebones.iso
	rm -rf build/iso_root
