#!/bin/sh
#
# Install the firmware images into the staging directory and write the
# SHA256SUMS file of every version.
#
# usage: install-firmware.sh <stagedir> <prefix> <work tree> <versions.list> \
#	<families file>
#
# Every line of the pin table is
#
#	<version> <image> <size> <sha256>
#
# The image is copied into the staging directory first and the copy is
# the file that is checked against the pin: the file that is installed is
# the file that was checked, so that nothing can be changed between the
# check and the install.  The hash that is written into the SHA256SUMS
# file is the pinned one and not the hash of whatever was on the disk.
#
# The SHA256SUMS file is in the format of sha256(1): a hash, two spaces and
# the name of the image.

set -e

STAGEDIR=$1
PREFIX=$2
TREE=$3
TABLE=$4
FAMILIES=$5

if [ -z "${STAGEDIR}" ] || [ -z "${PREFIX}" ] || [ -z "${TREE}" ] || \
    [ -z "${TABLE}" ] || [ -z "${FAMILIES}" ]; then
	echo "usage: install-firmware.sh <stagedir> <prefix> <tree> <table> <families>" >&2
	exit 1
fi

# The program that computes the hash.  Its name is not fixed: the base
# system puts sha256(1) in different places.
SHA256=
for candidate in sha256 sha256sum openssl; do
	if command -v "${candidate}" >/dev/null 2>&1; then
		SHA256=${candidate}
		break
	fi
done
if [ -z "${SHA256}" ]; then
	echo "install-firmware.sh: no sha256 program is there" >&2
	exit 1
fi

hash_of() {
	case "${SHA256}" in
	sha256)
		sha256 -q "$1"
		;;
	sha256sum)
		sha256sum "$1" | cut -d ' ' -f 1
		;;
	openssl)
		openssl dgst -sha256 -r "$1" | cut -d ' ' -f 1
		;;
	esac
}

# The size and the hash the pin table gives an image.
pin_of() {
	awk -v v="$1" -v i="$2" \
	    '{ if ($1 == v && $2 == i) { print $3; print $4 } }' "${TABLE}"
}

SHARE=${STAGEDIR}${PREFIX}

for version in $(awk '{ if ($1 !~ /^#/) print $1 }' "${TABLE}" | \
    sort -u); do
	mkdir -p "${SHARE}/${version}"
	: > "${SHARE}/${version}/SHA256SUMS"
	for image in $(awk -v v="${version}" \
	    '{ if ($1 == v) print $2 }' "${TABLE}" | sort); do
		file=$(find "${TREE}/${version}" -type f -name "${image}" | \
		    head -1)
		if [ -z "${file}" ]; then
			echo "install-firmware.sh: ${version}/${image} is missing" \
			    >&2
			exit 1
		fi
		size=$(pin_of "${version}" "${image}" | sed -n 1p)
		sha=$(pin_of "${version}" "${image}" | sed -n 2p)
		if [ -z "${size}" ] || [ -z "${sha}" ]; then
			echo "install-firmware.sh: ${version}/${image} is not in" \
			    "the pin table" >&2
			exit 1
		fi
		# The copy is the file that is checked and the file that is
		# installed.
		tmp="${SHARE}/${version}/.${image}.$$"
		rm -f "${tmp}"
		cp -p "${file}" "${tmp}"
		have_size=$(stat -f %z "${tmp}")
		if [ "${have_size}" != "${size}" ]; then
			echo "install-firmware.sh: ${version}/${image} has the" \
			    "size ${have_size}, the pin table says ${size}" >&2
			rm -f "${tmp}"
			exit 1
		fi
		have_sha=$(hash_of "${tmp}")
		if [ "${have_sha}" != "${sha}" ]; then
			echo "install-firmware.sh: ${version}/${image} has the" \
			    "hash ${have_sha}, the pin table says ${sha}" >&2
			rm -f "${tmp}"
			exit 1
		fi
		mv -f "${tmp}" "${SHARE}/${version}/${image}"
		printf '%s  %s\n' "${sha}" "${image}" \
		    >> "${SHARE}/${version}/SHA256SUMS"
		echo "=> ${version}/${image}: size and hash are the pinned ones"
	done
	# The chip list of the release travels with the images so that the
	# tool can name the cards it finds.
	cp -p "${TREE}/${version}/doc/supported-gpus/supported-gpus.json" \
	    "${SHARE}/${version}/supported-gpus.json"
	echo "=> installed ${version}"
done

# The family table is the same for every version.
cp -p "${FAMILIES}" "${SHARE}/nvidia-firmware.families"

exit 0
