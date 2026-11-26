#!/bin/sh
#
# Copy the sources of the tool and the files of the port into the work
# directory of the build.
#
# usage: sync-sources.sh <port directory> <work directory>
#
# The copy is made again before the build and before the test suite
# runs: the framework copies the sources of a port only once, and a
# change of a source has to be in the build and in the test.
#
# The directories are made again before the copy: a source that was
# deleted from the port has to be gone from the work directory as well,
# or the build of a work directory of an earlier build holds a source
# that the port does not have any more.
#
# The objects of a previous build are taken away: they are not the
# objects of the sources that are there now.

set -e

CURDIR=$1
WRKSRC=$2

if [ -z "${CURDIR}" ] || [ -z "${WRKSRC}" ]; then
	echo "usage: sync-sources.sh <port directory> <work directory>" >&2
	exit 1
fi

rm -rf "${WRKSRC}/src" "${WRKSRC}/files"

mkdir -p "${WRKSRC}/src"
cp -R "${CURDIR}/src/." "${WRKSRC}/src"
rm -f "${WRKSRC}/src"/*.o "${WRKSRC}/src"/*.pieo \
    "${WRKSRC}/src"/*.full "${WRKSRC}/src"/*.debug \
    "${WRKSRC}/src"/nvidia-firmware "${WRKSRC}/src"/nvidia-firmware-test \
    "${WRKSRC}/src"/nvidia-firmware.8.gz

mkdir -p "${WRKSRC}/files"
cp -R "${CURDIR}/files/." "${WRKSRC}/files"
cp "${CURDIR}/pkg-install" "${CURDIR}/pkg-deinstall" "${WRKSRC}"
# The test suite checks the package against the files of the port: the
# pin table and the list of the files of the package.  They are copied
# here so that the test finds them in the work directory as well.
cp "${CURDIR}/Makefile" "${CURDIR}/pkg-plist" "${WRKSRC}"

exit 0
