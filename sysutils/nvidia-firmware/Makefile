PORTNAME=	nvidia-firmware
DISTVERSION=	620.73.04
CATEGORIES=	sysutils
MAINTAINER=	ports@cloudbsd.org
COMMENT=	NVIDIA GSP firmware for every supported driver version
WWW=		https://www.nvidia.com

# The versions of the driver the package holds the firmware of.  The
# table of the pins is the one place where the versions are written down:
# the sites of the releases and the files of the releases are made from
# it, so that a version cannot be in one of them and not in the others.
#
# The lines of the table that are not a version begin with a '#', and a
# '#' in a makefile begins a comment: the versions are the lines that
# begin with a digit.
FW_VERSIONS!=	/usr/bin/awk '/^[0-9]/ { print $$1 }' \
		    ${.CURDIR}/files/versions.list | /usr/bin/sort -u
MASTER_SITES!=	/usr/bin/awk '/^[0-9]/ { print $$1 }' \
		    ${.CURDIR}/files/versions.list | /usr/bin/sort -u | \
		    while read v; do \
		    printf 'https://download.nvidia.com/XFree86/FreeBSD-x86_64/%s/ ' \
		    "$$v"; \
		    done
DISTFILES!=	/usr/bin/awk '/^[0-9]/ { print $$1 }' \
		    ${.CURDIR}/files/versions.list | /usr/bin/sort -u | \
		    while read v; do \
		    printf 'NVIDIA-FreeBSD-x86_64-%s.tar.xz ' "$$v"; \
		    done

# The NVIDIA Driver License Agreement, which is doc/license.txt in every
# NVIDIA FreeBSD release.  Clause 1.1(d) allows the redistribution of the
# binaries as long as they are not changed and every recipient gets the
# agreement, so the package ships the licence text.
LICENSE=		NVIDIA
LICENSE_GROUPS=	none
LICENSE_NAME=	"NVIDIA driver license"
LICENSE_FILE=	${FILESDIR}/license.txt
LICENSE_PERMS=	dist-mirror dist-sell pkg-mirror pkg-sell auto-accept

# The tool is written in the style of the FreeBSD base system: it builds
# with the base makefiles and installs into ${PREFIX}/sbin.  It is a
# program, so the package is not the same on every architecture.
#
# The directory the firmware and the family table are installed in.  The
# tool is told where it is when it is built.  It is the one directory the
# manual page and the README name: ${PREFIX}/share/nvidia-firmware.
FW_SHARE_DIR=	${DATADIR}

do-extract:
	@${MKDIR} ${WRKSRC}
	@for v in ${FW_VERSIONS}; do \
		${MKDIR} ${WRKSRC}/$$v; \
		${TAR} -xf ${_DISTDIR}/NVIDIA-FreeBSD-x86_64-$$v.tar.xz \
		    -C ${WRKSRC}/$$v --strip-components 1 \
		    "NVIDIA-FreeBSD-x86_64-$$v/firmware" \
		    "NVIDIA-FreeBSD-x86_64-$$v/doc/supported-gpus"; \
	done
	@${MKDIR} ${WRKSRC}/src
	@${CP} -R ${.CURDIR}/src/. ${WRKSRC}/src
	@${MKDIR} ${WRKSRC}/files
	@${CP} ${.CURDIR}/files/install-firmware.sh \
	    ${.CURDIR}/files/versions.list ${WRKSRC}/files
	@${CP} ${.CURDIR}/pkg-install ${.CURDIR}/pkg-deinstall ${WRKSRC}
	# The test suite checks the package against the files of the port:
	# the directory the firmware is installed in and the list of the
	# files of the package.  They are copied here so that the test
	# finds them in the work directory as well.
	@${CP} ${.CURDIR}/Makefile ${.CURDIR}/pkg-plist ${WRKSRC}

do-build:
	cd ${WRKSRC}/src && ${SETENVI} ${MAKE_CMD} ${MAKE_FLAGS} Makefile \
	    NF_SHARE_DIR=${FW_SHARE_DIR} all

do-install:
	@${ECHO_MSG} "===> Installing the firmware"
	cd ${.CURDIR} && ${SH} ${FILESDIR}/install-firmware.sh \
	    "${STAGEDIR}" "${FW_SHARE_DIR}" "${WRKSRC}" \
	    "${FILESDIR}/versions.list" \
	    "${.CURDIR}/src/nvidia-firmware.families"
	${MKDIR} ${STAGEDIR}${PREFIX}/sbin
	${INSTALL_PROGRAM} ${WRKSRC}/src/nvidia-firmware \
	    ${STAGEDIR}${PREFIX}/sbin/nvidia-firmware
	${MKDIR} ${STAGEDIR}${PREFIX}/share/man/man8
	${INSTALL_MAN} ${WRKSRC}/src/nvidia-firmware.8 \
	    ${STAGEDIR}${PREFIX}/share/man/man8/nvidia-firmware.8

.PHONY: test

# The test suite of the tool.  It needs the chip lists of the releases, so
# the extraction has to have happened.
test: extract
	@${ECHO_MSG} "===> Running the test suite of the tool"
	@${MKDIR} ${WRKDIR}/tmp
	cd ${WRKSRC}/src && ${SETENVI} TMPDIR=${WRKDIR}/tmp \
	    NF_TEST_GPU_JSON="${WRKSRC}/580.178.04/doc/supported-gpus/supported-gpus.json ${WRKSRC}/620.73.04/doc/supported-gpus/supported-gpus.json" \
	    ${MAKE_CMD} ${MAKE_FLAGS} Makefile test

.include <bsd.port.mk>
