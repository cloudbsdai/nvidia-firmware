# $FreeBSD$
#
# Build the nvidia-firmware tool.  The program is written in the style of
# the FreeBSD base system so that it can move into the base tree without a
# change: it uses the base makefiles, libxo for the output and libmd for
# the hashes.

PREFIX?=	/usr/local
SH?=		/bin/sh
BINDIR?=	${PREFIX}/sbin
MANDIR?=	${PREFIX}/man/man

PROG=		nvidia-firmware
SRCS=		nvidia-firmware.c \
		nf_boot.c \
		nf_cards.c \
		nf_families.c \
		nf_fetch.c \
		nf_pin.c \
		nf_rc.c \
		nf_util.c
HDRS=		nf_boot.h \
		nf_cards.h \
		nf_families.h \
		nf_fetch.h \
		nf_pin.h \
		nf_rc.h \
		nf_util.h
MAN=		nvidia-firmware.8

# The family table is compiled into the program as well: the tool has to
# name the family of a card even when the firmware is not installed.  The
# table stays the one source of truth; embed-families.sh only makes a copy
# of it for the compiler.
FAMILIES=	nvidia-firmware.families
FAMILIES_DATA=	nf_families_data.h
# The directory of the pin table is a value of the caller: it comes from
# the prefix the package is built with.  It is not written into the text
# of a command of the compiler, where a quote or a semicolon of the path
# would be read as shell syntax.  The path is read from the environment by
# embed-paths.sh, which writes it into a header with the escaping that a C
# string needs, and the compiler is given the header.
PATHS_DATA=	nf_paths.h
# The objects are named after the sources, and the object that holds the
# table has to be made again when the table changes.  DPADD is not enough
# here: it makes the program depend on the header, not the object that is
# compiled with it.
FAMILIES_OBJS=	${SRCS:S/.c$/.o/} ${SRCS:S/.c$/.pieo/}

CLEANFILES+=	${FAMILIES_DATA} ${PATHS_DATA} ${VERSION_DATA}

CFLAGS+=	-Wall -Wextra -Werror
NF_DATA_DIR?=	${PREFIX}/share/nvidia-firmware
# The path is handed to the generator through the environment: it is
# never written into the text of a command of the compiler.
export NF_DATA_PATH=${NF_DATA_DIR}
CFLAGS+=	-include ${PATHS_DATA}
CFLAGS+=	-DNF_BOOT_DIR='"/boot/firmware/nvidia"'
#
# The version of the program is the version of the package.  The port
# gives its own DISTVERSION to the build, so that the program and the
# package always carry the same version.
#
# The version is a value of the caller and it is not written into the
# text of a command of the compiler: a version that holds a quote or a
# semicolon would end the argument of the compiler and would be read as
# shell syntax.  The version is read from the environment by
# embed-version.sh, which writes it into a header with the escaping that
# a C string needs, and the compiler is given the header.  A version that
# is not a date version is refused: the build stops instead of writing a
# header that holds a version the program cannot read.
NF_VERSION?=	0.0.0
export NF_VERSION=${NF_VERSION}
VERSION_DATA=	nf_version.h
CFLAGS+=	-include ${VERSION_DATA}
LDADD=		-lxo -lmd

# The test flavour of the program honours the environment hooks that
# replace the boot directory, the share directory, the PCI bus, the
# version pin and the state of the kernel module.  The installed program
# ignores them.
TESTPROG=	nvidia-firmware-test
TESTCFLAGS=	${CFLAGS} -DNVIDIA_FIRMWARE_TESTING

all: ${PROG}

${FAMILIES_DATA}: ${FAMILIES} ${.CURDIR}/embed-families.sh
	${SH} ${.CURDIR}/embed-families.sh ${FAMILIES} ${FAMILIES_DATA}

${PATHS_DATA}: ${.CURDIR}/embed-paths.sh
	${SH} ${.CURDIR}/embed-paths.sh ${PATHS_DATA}

${VERSION_DATA}: ${.CURDIR}/embed-version.sh
	${SH} ${.CURDIR}/embed-version.sh ${VERSION_DATA}

${FAMILIES_OBJS}: ${FAMILIES_DATA} ${PATHS_DATA} ${VERSION_DATA}

${TESTPROG}: ${SRCS} ${HDRS} ${FAMILIES_DATA} ${PATHS_DATA} ${VERSION_DATA}
	${CC} ${TESTCFLAGS} ${CPPFLAGS} -o ${TESTPROG} ${SRCS} ${LDFLAGS} ${LDADD}

test: ${TESTPROG}
	@${SH} ${.CURDIR}/tests/run-tests.sh ${.CURDIR}/${TESTPROG}

.include <bsd.prog.mk>
