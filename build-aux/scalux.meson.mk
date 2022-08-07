all: configure build install

configure:
	mkdir -p ${WORKDIR}
	meson setup --cross-file=${CROSSFILE} --prefix /usr ${WORKDIR} ${PKG_SRCDIR}

build:
	ninja -C ${WORKDIR}

install:
	DESTDIR=${S_SYSROOT} ninja -C ${WORKDIR} install
