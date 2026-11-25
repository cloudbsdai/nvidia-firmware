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
		nf_rc.c \
		nf_share.c \
		nf_util.c
HDRS=		nf_boot.h \
		nf_cards.h \
		nf_families.h \
		nf_rc.h \
		nf_share.h \
		nf_util.h
MAN=		nvidia-firmware.8

# The family table is compiled into the program as well: the tool has to
# name the family of a card even when the firmware is not installed.  The
# table stays the one source of truth; embed-families.sh only makes a copy
# of it for the compiler.
FAMILIES=	nvidia-firmware.families
FAMILIES_DATA=	nf_families_data.h
# The objects are named after the sources, and the object that holds the
# table has to be made again when the table changes.  DPADD is not enough
# here: it makes the program depend on the header, not the object that is
# compiled with it.
FAMILIES_OBJS=	${SRCS:S/.c$/.o/} ${SRCS:S/.c$/.pieo/}

CLEANFILES+=	${FAMILIES_DATA}

CFLAGS+=	-Wall -Wextra -Werror
NF_SHARE_DIR?=	${PREFIX}/share/nvidia-firmware
CFLAGS+=	-DNF_SHARE_DIR='"${NF_SHARE_DIR}"'
CFLAGS+=	-DNF_BOOT_DIR='"/boot/firmware/nvidia"'
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

${FAMILIES_OBJS}: ${FAMILIES_DATA}

${TESTPROG}: ${SRCS} ${HDRS} ${FAMILIES_DATA}
	${CC} ${TESTCFLAGS} ${CPPFLAGS} -o ${TESTPROG} ${SRCS} ${LDFLAGS} ${LDADD}

test: ${TESTPROG}
	@${SH} ${.CURDIR}/tests/run-tests.sh ${.CURDIR}/${TESTPROG}

.include <bsd.prog.mk>
