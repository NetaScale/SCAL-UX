rm vendor/limine/limine-deploy
make -C vendor/limine

	rm -rf iso_root
	mkdir -p iso_root
	cp build/kernel/amd64/vxkern build/TestDriver/libTestDriver.so \
		limine.cfg vendor/limine/limine.sys vendor/limine/limine-cd.bin \
		vendor/limine/limine-cd-efi.bin init iso_root/
	xorriso -as mkisofs -b limine-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-cd-efi.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o barebones.iso
	vendor/limine/limine-deploy barebones.iso
	rm -rf iso_root
