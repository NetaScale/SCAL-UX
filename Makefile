all:
	ninja -C build
	DESTDIR=/tmp/scalux ninja -C build install
	tar -cvf build/initrd.tar -C /tmp/scalux ./
	./geniso.sh
