PORTNAME=	nvidia-firmware
# The version of the package is ours: the date of the day the package was
# made.  The versions of the NVIDIA driver are data in the pin table and
# are never the version of the package.
DISTVERSION=	2026.11.26
CATEGORIES=	sysutils
MAINTAINER=	ports@cloudbsd.org
COMMENT=	Fetch and place the NVIDIA GSP firmware the adapters need
WWW=		https://www.nvidia.com

# The port has no configuration step: the pins of the firmware are a file
# of the port and there is nothing to ask about.
CONFIGURE=	none

# The port fetches nothing: the sources of the tool are in the port and
# the firmware is fetched by the tool when the machine needs it.  The
# empty list of the files of the release says that there is no file to
# fetch, and the distinfo of the port is empty for the same reason.
DISTFILES=

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
# The directory the pin table and the table of the families are installed
# in.  The tool is told where it is when it is built.  It is the one
# directory the manual page names: ${PREFIX}/share/nvidia-firmware.
FW_DATA_DIR=	${DATADIR}

# The package holds the tool, the pin table and the licence.  The
# firmware itself is never shipped with it: the tool fetches the file
# from download.nvidia.com when the adapters of the machine need it, and
# checks it against the pin table before it puts it in place.
#
# The table of the pins is the one place where the pins are written
# down: the files of the package and the table of the families are made
# from it, so that a file cannot be in one of them and not in the other.

# === Values and the shell ===
# No value of the caller is written into the text of a recipe.  A path
# that holds a space, a quote or a semicolon is data, and data that is
# written into the text of a shell command is read as code.  Every value
# is handed to the shell through the environment instead, and the recipe
# reads it from there as "$$NF_NAME".  The paths are quoted; the commands
# of the ports framework are words of their own and are left unquoted so
# that their own words stay words.
#
# The exports are at the end of the file, after the framework has been
# read: the values they hand over are the ones the framework computes.

do-extract:
	@$$NF_MKDIR "$$NF_WRKSRC"

do-build:
	@$$NF_SH "$$NF_CURDIR/files/sync-sources.sh" "$$NF_CURDIR" \
	    "$$NF_WRKSRC"
	cd "$$NF_WRKSRC/src" && $$NF_SETENVI $$NF_MAKE_CMD $$NF_MAKE_FLAGS \
	    Makefile "NF_DATA_DIR=$$NF_FW_DATA_DIR" \
	    "NF_VERSION=$$NF_DISTVERSION" all

do-install:
	@$$NF_ECHO_MSG "===> Installing the tool"
	$$NF_MKDIR "$$NF_STAGEDIR$$NF_PREFIX/sbin"
	$$NF_INSTALL_PROGRAM "$$NF_WRKSRC/src/nvidia-firmware" \
	    "$$NF_STAGEDIR$$NF_PREFIX/sbin/nvidia-firmware"
	$$NF_MKDIR "$$NF_STAGEDIR$$NF_PREFIX/share/man/man8"
	$$NF_INSTALL_MAN "$$NF_WRKSRC/src/nvidia-firmware.8" \
	    "$$NF_STAGEDIR$$NF_PREFIX/share/man/man8/nvidia-firmware.8"
	@$$NF_ECHO_MSG "===> Installing the pin table and the table of the families"
	$$NF_MKDIR "$$NF_STAGEDIR$$NF_FW_DATA_DIR"
	$$NF_INSTALL_DATA "$$NF_CURDIR/files/versions.list" \
	    "$$NF_STAGEDIR$$NF_FW_DATA_DIR/versions.list"
	$$NF_INSTALL_DATA "$$NF_CURDIR/src/nvidia-firmware.families" \
	    "$$NF_STAGEDIR$$NF_FW_DATA_DIR/nvidia-firmware.families"
	@$$NF_ECHO_MSG "===> Installing the rule of devd"
	$$NF_MKDIR "$$NF_STAGEDIR$$NF_PREFIX/etc/devd"
	$$NF_SH "$$NF_CURDIR/files/make-devd-rule.sh" "$$NF_PREFIX" \
	    "$$NF_STAGEDIR$$NF_PREFIX/etc/devd/nvidia_firmware.conf" \
	    "$$NF_CURDIR/files/nvidia_firmware.conf"

.PHONY: test

# The test suite of the tool.  The program of the test suite carries the
# version of the package, so that the two are always the same.
#
# The suite runs in a directory of its own below the work directory of
# the build: it writes nothing outside of it.

test: extract
	@$$NF_ECHO_MSG "===> Running the test suite of the tool"
	@$$NF_SH "$$NF_CURDIR/files/sync-sources.sh" "$$NF_CURDIR" \
	    "$$NF_WRKSRC"
	@$$NF_MKDIR "$$NF_WRKDIR/tmp"
	cd "$$NF_WRKSRC/src" && $$NF_SETENVI TMPDIR="$$NF_WRKDIR/tmp" \
	    $$NF_MAKE_CMD $$NF_MAKE_FLAGS Makefile \
	    "NF_VERSION=$$NF_DISTVERSION" test

.include <bsd.port.mk>

# The values the recipes above read from the environment.  They are
# exported here, after the framework has been read, because the framework
# is what computes them.
export NF_STAGEDIR=${STAGEDIR}
export NF_PREFIX=${PREFIX}
export NF_DISTVERSION=${DISTVERSION}
export NF_WRKDIR=${WRKDIR}
export NF_WRKSRC=${WRKSRC}
export NF_DISTDIR=${_DISTDIR}
export NF_FILESDIR=${FILESDIR}
export NF_CURDIR=${.CURDIR}
export NF_FW_DATA_DIR=${FW_DATA_DIR}
export NF_MKDIR=${MKDIR}
export NF_CP=${CP}
export NF_RM=${RM}
export NF_INSTALL_PROGRAM=${INSTALL_PROGRAM}
export NF_INSTALL_MAN=${INSTALL_MAN}
export NF_INSTALL_DATA=${INSTALL_DATA}
export NF_ECHO_MSG=${ECHO_MSG}
export NF_SETENVI=${SETENVI}
export NF_MAKE_CMD=${MAKE_CMD}
export NF_MAKE_FLAGS=${MAKE_FLAGS}
export NF_SH=${SH}
