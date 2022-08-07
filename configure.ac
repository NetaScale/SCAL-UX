AC_INIT([SCAL/UX Operating System], [0.3])

AC_CONFIG_SRCDIR([README.md])
AC_CONFIG_AUX_DIR([build-aux])

BUILDDIR="$(pwd -P)"
HOSTTOOLS=${BUILDDIR}/host_tools

AC_PROG_CC

m4_define([DEFAULT_ARCH], [x86_64])
AC_ARG_VAR(ARCH, [target architecture @<:@default: ]DEFAULT_ARCH[@:>@])
test "x$ARCH" = "x" && ARCH="DEFAULT_ARCH"

AC_ARG_WITH([sysroot],
    [AS_HELP_STRING([--with-toolchain=<path>],
        [Sysroot to use.])],
    [AS_IF([test -d "$with_sysroot"],
        [],
        [AC_MSG_ERROR([Specify an existing directory for sysroot, not "$with_sysroot"])])],
    [with_sysroot=${BUILDDIR}/sysroot])

AC_MSG_NOTICE([Using sysroot "$with_sysroot"])

AC_ARG_WITH([toolchain],
    [AS_HELP_STRING([--with-toolchain=<path>],
        [Top-level directory of a toolchain installation.])],
    [AS_IF([test -d "$with_toolchain"],
        [AC_MSG_NOTICE([Using existing target toolchain in "$with_toolchain"])],
        [AC_MSG_ERROR([Specify an existing directory for toolchain, not "$with_toolchain"])])],
    [AC_MSG_ERROR([Building a toolchain not yet supported.])])

AC_SUBST([TOOLCHAIN_ROOTDIR], [$with_toolchain])
AC_SUBST([SYSROOT], [$with_sysroot])
AC_SUBST([HOSTTOOLS], [$HOSTTOOLS])

AC_CONFIG_FILES([build.sh build-aux/$ARCH.ini])

mkdir -p $with_sysroot
mkdir -p ${HOSTTOOLS}/bin

AC_OUTPUT

chmod +x build.sh
