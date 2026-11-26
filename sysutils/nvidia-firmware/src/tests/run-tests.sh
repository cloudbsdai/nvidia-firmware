#!/bin/sh
#
# Test suite of the nvidia-firmware tool.  Every case runs the tool
# against a fake system below ${TMPDIR}; nothing outside of it is touched.
#
# The tool is called with the environment hooks of the test build:
#
#	NVIDIA_FIRMWARE_TEST_ROOT	the root of the fake system
#	NVIDIA_FIRMWARE_TEST_PCI	the file of PCI device ids
#	NVIDIA_FIRMWARE_TEST_KLD	"loaded" or "not"
#	NVIDIA_FIRMWARE_TEST_PIN	the version the driver is pinned to
#
# The firmware is never in the package: the tests put the files in a
# mirror of their own and the tool takes them from there.  The mirror is
# named by NVIDIA_FIRMWARE_MIRROR, and the option --from says the same
# thing on the command line.

set -u

BIN=${1:-./nvidia-firmware-test}
SRCDIR=$(dirname "$0")/..
FAMILIES=${SRCDIR}/nvidia-firmware.families
MANPAGE=${SRCDIR}/nvidia-firmware.8

: "${TMPDIR:?TMPDIR must be set; the tests never write to /tmp}"
WORK=${TMPDIR}/nvidia-firmware-test

PASS=0
FAIL=0
CASE=0

fail() {
	FAIL=$((FAIL + 1))
	printf 'not ok %d - %s\n' "$CASE" "$1"
	shift
	for line in "$@"; do
		printf '#   %s\n' "$line"
	done
}

ok() {
	PASS=$((PASS + 1))
	printf 'ok %d - %s\n' "$CASE" "$1"
}

# The hash of a file.  The base system has sha256(1), but not every
# installation does, so the other names are tried as well.
sha256_of() {
	if command -v sha256 >/dev/null 2>&1; then
		sha256 -q "$1"
	elif command -v sha256sum >/dev/null 2>&1; then
		sha256sum "$1" | cut -d ' ' -f 1
	elif command -v openssl >/dev/null 2>&1; then
		openssl dgst -sha256 -r "$1" | cut -d ' ' -f 1
	elif command -v shasum >/dev/null 2>&1; then
		shasum -a 256 "$1" | cut -d ' ' -f 1
	else
		echo "no sha256 tool found" >&2
		exit 1
	fi
}

# The size of a file in bytes.
size_of() {
	wc -c < "$1" | tr -d ' \t'
}

# The fake files of the mirror and the pin table that names them.  Every
# driver version has its own directory in the mirror and its own files.
make_share() {
	_data=$1
	_mirror=$2
	mkdir -p "${_data}"
	cp "${FAMILIES}" "${_data}/nvidia-firmware.families"
	: > "${_data}/versions.list"
	for v in 580.178.04 620.73.04; do
		mkdir -p "${_mirror}/${v}"
		for i in gsp_tu10x gsp_ga10x; do
			printf 'firmware %s %s\n' "$v" "$i" \
			    > "${_mirror}/${v}/${i}.bin"
		done
	done
	for i in ucodes_tu10x ucodes_ga10x; do
		printf 'firmware 620.73.04 %s\n' "$i" \
		    > "${_mirror}/620.73.04/${i}.bin"
	done
	for v in 580.178.04 620.73.04; do
		for f in "${_mirror}/${v}"/*.bin; do
			printf '%s\t%s\t%s\t%s\n' "$v" "$(basename "$f")" \
			    "$(size_of "$f")" "$(sha256_of "$f")" \
			    >> "${_data}/versions.list"
		done
	done
}

# A fresh case: a root with the fake data and a fake PCI bus.
new_case() {
	CASE=$((CASE + 1))
}

new_root() {
	new_case
	ROOT=${WORK}/case-${CASE}
	rm -rf "${ROOT}"
	mkdir -p "${ROOT}/boot/firmware" "${ROOT}/etc/rc.conf.d"
	make_share "${ROOT}/share/nvidia-firmware" "${ROOT}/mirror"
	PCIFILE=${ROOT}/pci
	BOOT=${ROOT}/boot/firmware/nvidia
	MIRROR=${ROOT}/mirror
	: > "${PCIFILE}"
	unset NVIDIA_FIRMWARE_TEST_KLD NVIDIA_FIRMWARE_TEST_PIN
}

# A fresh case without the data of the package: the pin table is not
# there.
new_root_without_share() {
	new_case
	ROOT=${WORK}/case-${CASE}
	rm -rf "${ROOT}"
	mkdir -p "${ROOT}/boot/firmware" "${ROOT}/etc/rc.conf.d"
	PCIFILE=${ROOT}/pci
	BOOT=${ROOT}/boot/firmware/nvidia
	MIRROR=${ROOT}/mirror
	: > "${PCIFILE}"
	unset NVIDIA_FIRMWARE_TEST_KLD NVIDIA_FIRMWARE_TEST_PIN
}

run_tool() {
	env NVIDIA_FIRMWARE_TEST_ROOT="${ROOT}" \
	    NVIDIA_FIRMWARE_TEST_PCI="${PCIFILE}" \
	    NVIDIA_FIRMWARE_TEST_KLD="${NVIDIA_FIRMWARE_TEST_KLD:-not}" \
	    NVIDIA_FIRMWARE_TEST_PIN="${NVIDIA_FIRMWARE_TEST_PIN:-}" \
	    NVIDIA_FIRMWARE_TEST_LOCK="${WORK}/nvidia-firmware.lock" \
	    NVIDIA_FIRMWARE_MIRROR="${MIRROR}" \
	    "${BIN}" "$@"
}

exists() {
	[ -f "$1" ]
}

# ---------------------------------------------------------------- test 1
# Two cards of different families: both sets of images are placed and
# nothing else is.
test_two_families() {
	new_root
	printf '0x2204 pci0:1:0:0\n0x1e04 pci0:2:0:0\n' > "${PCIFILE}"
	out=$(run_tool detect 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "two families: detect failed" "exit ${rc}" "${out}"
		return
	fi
	missing=
	for i in gsp_tu10x.bin gsp_ga10x.bin ucodes_tu10x.bin ucodes_ga10x.bin; do
		exists "${BOOT}/${i}" || missing="${missing} ${i}"
	done
	if [ -n "${missing}" ]; then
		fail "two families: images missing" "missing:${missing}"
		return
	fi
	count=$(find "${BOOT}" -maxdepth 1 -type f -name '*.bin' | wc -l | tr -d ' ')
	if [ "${count}" -ne 4 ]; then
		fail "two families: wrong number of images" "found ${count}"
		return
	fi
	ok "two cards of different families place exactly their images"
}

# ---------------------------------------------------------------- test 2
# The cards are gone: our images go away, the file of somebody else and
# the directory of another vendor stay.
test_cards_gone() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	exists "${BOOT}/gsp_ga10x.bin" || {
		fail "cards gone: the image was not placed"
		return
	}
	printf 'ours\n' > "${BOOT}/foreign.bin"
	mkdir -p "${ROOT}/boot/firmware/othervendor"
	printf 'theirs\n' > "${ROOT}/boot/firmware/othervendor/x.bin"
	: > "${PCIFILE}"
	out=$(run_tool detect 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "cards gone: detect failed" "exit ${rc}" "${out}"
		return
	fi
	if exists "${BOOT}/gsp_ga10x.bin"; then
		fail "cards gone: our image is still there"
		return
	fi
	if [ ! -f "${BOOT}/foreign.bin" ]; then
		fail "cards gone: the foreign file was removed"
		return
	fi
	if [ ! -f "${ROOT}/boot/firmware/othervendor/x.bin" ]; then
		fail "cards gone: the file of another vendor was removed"
		return
	fi
	if [ ! -d "${BOOT}" ]; then
		fail "cards gone: the directory was removed although a foreign file is in it"
		return
	fi
	ok "the images of ours go away, the files of others do not"
}

# ---------------------------------------------------------------- test 3
# The directory goes away when nothing is left in it.
test_empty_directory_removed() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	: > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	if [ -d "${BOOT}" ]; then
		fail "empty directory: the directory is still there"
		return
	fi
	ok "the directory is removed when it is empty"
}

# ---------------------------------------------------------------- test 4
# --no-delete keeps the images.
test_no_delete() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	: > "${PCIFILE}"
	run_tool detect --no-delete >/dev/null 2>&1
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "--no-delete: the image was removed"
		return
	fi
	ok "--no-delete keeps the images"
}

# ---------------------------------------------------------------- test 5
# The rc.conf setting keeps the images.
test_rc_no_delete() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	printf 'nvidia_firmware_no_delete="YES"\n' > "${ROOT}/etc/rc.conf"
	: > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "rc.conf: the image was removed"
		return
	fi
	rm -f "${ROOT}/etc/rc.conf"
	printf 'nvidia_firmware_no_delete=YES\n' \
	    > "${ROOT}/etc/rc.conf.d/nvidia_firmware"
	: > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "rc.conf.d: the image was removed"
		return
	fi
	ok "the rc.conf setting keeps the images"
}

# ---------------------------------------------------------------- test 6
# --expect-card fails when there is no card.
test_expect_card() {
	new_root
	out=$(run_tool detect --expect-card 2>&1)
	rc=$?
	if [ ${rc} -ne 1 ]; then
		fail "--expect-card: the exit code is ${rc}, not 1" "${out}"
		return
	fi
	case "${out}" in
	*"expected an NVIDIA GPU"*) ;;
	*)
		fail "--expect-card: the message is wrong" "${out}"
		return
		;;
	esac
	out=$(run_tool detect 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "detect without --expect-card: the exit code is ${rc}" "${out}"
		return
	fi
	ok "--expect-card fails without a card, detect does not"
}

# ---------------------------------------------------------------- test 7
# A forced image stays; after remove it is gone.
test_forced() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool install --force gsp_tu10x.bin >/dev/null 2>&1
	if ! exists "${BOOT}/gsp_tu10x.bin"; then
		fail "forced: the image was not placed"
		return
	fi
	if ! grep -v '^#' "${BOOT}/SHA256SUMS" | grep -q ' forced '; then
		fail "forced: the inventory does not mark the image"
		return
	fi
	run_tool detect >/dev/null 2>&1
	if ! exists "${BOOT}/gsp_tu10x.bin"; then
		fail "forced: detect removed the forced image"
		return
	fi
	run_tool remove gsp_tu10x.bin >/dev/null 2>&1
	if grep -v '^#' "${BOOT}/SHA256SUMS" | grep -q ' forced '; then
		fail "forced: the image is still marked as forced"
		return
	fi
	if exists "${BOOT}/gsp_tu10x.bin"; then
		fail "forced: the image is still there after remove"
		return
	fi
	ok "a forced image stays until it is removed"
}

# ---------------------------------------------------------------- test 8
# verify finds a changed file, a missing file and an extra file.
test_verify() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	out=$(run_tool verify 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "verify: the exit code is ${rc} on a good tree" "${out}"
		return
	fi
	printf 'x' >> "${BOOT}/gsp_ga10x.bin"
	out=$(run_tool verify 2>&1)
	rc=$?
	if [ ${rc} -ne 1 ]; then
		fail "verify: a changed file is not an error" "${out}"
		return
	fi
	run_tool detect >/dev/null 2>&1
	rm -f "${BOOT}/gsp_ga10x.bin"
	out=$(run_tool verify 2>&1)
	rc=$?
	if [ ${rc} -ne 1 ]; then
		fail "verify: a missing file is not an error" "${out}"
		return
	fi
	run_tool detect >/dev/null 2>&1
	printf 'ours\n' > "${BOOT}/extra.bin"
	out=$(run_tool verify 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "verify: an extra file of ours is not an error" "${out}"
		return
	fi
	case "${out}" in
	*extra.bin*) ;;
	*)
		fail "verify: the extra file is not reported" "${out}"
		return
		;;
	esac
	ok "verify finds a changed, a missing and an extra file"
}

# ---------------------------------------------------------------- test 9
# A file whose hash does not match is refused.
test_bad_hash() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	printf 'tampered\n' > "${MIRROR}/620.73.04/gsp_ga10x.bin"
	out=$(run_tool detect 2>&1)
	rc=$?
	if [ ${rc} -ne 1 ]; then
		fail "bad hash: detect did not fail" "exit ${rc}" "${out}"
		return
	fi
	if exists "${BOOT}/gsp_ga10x.bin"; then
		fail "bad hash: the file was placed anyway"
		return
	fi
	if ! check_no_temp_file; then
		fail "bad hash: a temporary file was left behind" \
		    "$(ls -1 "${BOOT}"/.nvidia-firmware.* 2>/dev/null)"
		return
	fi
	out=$(run_tool install --force gsp_ga10x.bin 2>&1)
	rc=$?
	if [ ${rc} -ne 1 ]; then
		fail "bad hash: install --force did not fail" "exit ${rc}" "${out}"
		return
	fi
	ok "a file whose hash does not match is refused"
}

# --------------------------------------------------------------- test 10
# A copy that fails before the rename leaves no file at the real name.
test_interrupted_copy() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	mkdir -p "${BOOT}"
	mkdir -p "${BOOT}/gsp_ga10x.bin"
	out=$(run_tool detect 2>&1)
	rc=$?
	if [ ${rc} -ne 1 ]; then
		fail "interrupted copy: detect did not fail" "exit ${rc}" "${out}"
		return
	fi
	if [ -f "${BOOT}/gsp_ga10x.bin" ]; then
		fail "interrupted copy: a file is there under the real name"
		return
	fi
	if ! check_no_temp_file; then
		fail "interrupted copy: a temporary file was left behind" \
		    "$(ls -1 "${BOOT}"/.nvidia-firmware.* 2>/dev/null)"
		return
	fi
	ok "a copy that fails before the rename leaves no file behind"
}

# --------------------------------------------------------------- test 11
# The version pin chooses the version.
test_version_pin() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	NVIDIA_FIRMWARE_TEST_PIN=580.178.04 run_tool detect >/dev/null 2>&1
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "version pin: the image of the pinned version is missing"
		return
	fi
	if exists "${BOOT}/ucodes_ga10x.bin"; then
		fail "version pin: an image of another version was placed"
		return
	fi
	if ! grep -q '580\.178\.04' "${BOOT}/SHA256SUMS"; then
		fail "version pin: the inventory does not name the version"
		return
	fi
	ok "the version pin chooses the version"
}

# --------------------------------------------------------------- test 12
# A pin to a version that is not installed is an error: the driver would
# get the firmware of a version it does not want.
test_version_pin_fallback() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	out=$(NVIDIA_FIRMWARE_TEST_PIN=999.99.99 run_tool detect 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "version pin: the run did not fail" "exit ${rc}" "${out}"
		return
	fi
	case "${out}" in
	*999.99.99*) ;;
	*)
		fail "version pin: the version that is not there is not named" \
		    "${out}"
		return
		;;
	esac
	if [ -e "${BOOT}" ]; then
		fail "version pin: the boot directory was made" "${out}"
		return
	fi
	ok "a pin to a version that is not installed is an error"
}

# --------------------------------------------------------------- test 13
# clean refuses while the module is loaded.
test_clean() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	NVIDIA_FIRMWARE_TEST_KLD=loaded run_tool clean >/dev/null 2>&1
	rc=$?
	if [ ${rc} -ne 1 ]; then
		fail "clean: the exit code is ${rc} while the module is loaded"
		return
	fi
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "clean: the image was removed although the module is loaded"
		return
	fi
	run_tool clean >/dev/null 2>&1
	if [ -e "${BOOT}/gsp_ga10x.bin" ]; then
		fail "clean: the image is still there"
		return
	fi
	ok "clean refuses while the module is loaded and works otherwise"
}

# --------------------------------------------------------------- test 14
# The JSON output parses.
test_json() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	for cmd in "list" "list --cards" "status" "verify"; do
		out=$(run_tool ${cmd} --libxo json 2>&1)
		if ! printf '%s' "${out}" | python3 -I -c \
		    'import json,sys; json.load(sys.stdin)' >/dev/null 2>&1; then
			fail "json: ${cmd} does not write JSON" "${out}"
			return
		fi
	done
	out=$(run_tool list --json 2>&1)
	if ! printf '%s' "${out}" | python3 -I -c \
	    'import json,sys; json.load(sys.stdin)' >/dev/null 2>&1; then
		fail "json: --json does not write JSON" "${out}"
		return
	fi
	ok "the JSON output parses"
}

# --------------------------------------------------------------- test 15
# The cards are listed when the table of the families is not in the data
# of the package: the program holds the same table inside itself.  The
# pin table is needed for the list of the versions, so a data directory
# without a pin table is an error.
test_list_cards_without_families() {
	new_root
	printf '0x2204 pci0:1:0:0\n' > "${PCIFILE}"
	rm -f "${ROOT}/share/nvidia-firmware/nvidia-firmware.families"
	out=$(run_tool list --cards 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "list --cards: the exit code is ${rc}" "${out}"
		return
	fi
	for want in 'pci0:1:0:0' '0x2204' 'GA10x'; do
		case "${out}" in
		*"${want}"*) ;;
		*)
			fail "list --cards: ${want} is not in the output" "${out}"
			return
			;;
		esac
	done
	#
	# The table of the package says the same as the table that is
	# compiled into the program.
	#
	cp "${FAMILIES}" "${ROOT}/share/nvidia-firmware/nvidia-firmware.families"
	out2=$(run_tool list --cards 2>&1)
	family1=$(printf '%s' "${out}" | sed -n 's/.*family=\([^ ]*\).*/\1/p')
	family2=$(printf '%s' "${out2}" | sed -n 's/.*family=\([^ ]*\).*/\1/p')
	if [ -z "${family1}" ] || [ "${family1}" != "${family2}" ]; then
		fail "list --cards: the family is ${family1} without the table of the package and ${family2} with it" \
		    "${out}" "${out2}"
		return
	fi
	# Without a pin table there is nothing to list.
	new_root_without_share
	printf '0x2204 pci0:1:0:0\n' > "${PCIFILE}"
	out=$(run_tool list --cards 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "list --cards: the list worked without a pin table" "${out}"
		return
	fi
	ok "the cards are listed when the table of the families is not there"
}

# --------------------------------------------------------------- test 16
# A file that is not in our inventory is never removed.
test_remove_foreign() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	printf 'ours\n' > "${BOOT}/foreign.bin"
	out=$(run_tool remove foreign.bin 2>&1)
	rc=$?
	if [ ${rc} -ne 1 ]; then
		fail "remove foreign: the exit code is ${rc}, not 1" "${out}"
		return
	fi
	if ! exists "${BOOT}/foreign.bin"; then
		fail "remove foreign: the file was removed anyway"
		return
	fi
	case "${out}" in
	*"not one of our images"*) ;;
	*)
		fail "remove foreign: the message is wrong" "${out}"
		return
		;;
	esac
	ok "a file that is not ours is never removed"
}

# --------------------------------------------------------------- test 17
# The loaded driver uses the image: it is not removed.
test_remove_in_use() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool install --force gsp_ga10x.bin >/dev/null 2>&1
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "remove in use: the image was not placed"
		return
	fi
	out=$(NVIDIA_FIRMWARE_TEST_KLD=loaded run_tool remove \
	    gsp_ga10x.bin 2>&1)
	rc=$?
	if [ ${rc} -ne 1 ]; then
		fail "remove in use: the exit code is ${rc}, not 1" "${out}"
		return
	fi
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "remove in use: the image was removed anyway"
		return
	fi
	case "${out}" in
	*"loaded"*) ;;
	*)
		fail "remove in use: the message is wrong" "${out}"
		return
		;;
	esac
	ok "an image the loaded driver uses is not removed"
}

# --------------------------------------------------------------- test 18
# A card that needs the image is warned about.
test_remove_warns() {
	new_root
	printf '0x2204 pci0:1:0:0\n' > "${PCIFILE}"
	run_tool install --force gsp_ga10x.bin >/dev/null 2>&1
	out=$(run_tool remove gsp_ga10x.bin 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "remove warns: the exit code is ${rc}" "${out}"
		return
	fi
	if exists "${BOOT}/gsp_ga10x.bin"; then
		fail "remove warns: the image is still there"
		return
	fi
	case "${out}" in
	*"warning"*"pci0:1:0:0"*"gsp_ga10x.bin"*) ;;
	*)
		fail "remove warns: the warning is missing" "${out}"
		return
		;;
	esac
	ok "a card that needs the image is warned about"
}

# --------------------------------------------------------------- test 19
# The manual page is clean.
test_manpage() {
	new_case
	out=$(mandoc -T lint "${MANPAGE}" 2>&1)
	if [ -n "${out}" ]; then
		fail "mandoc: the manual page is not clean" "${out}"
		return
	fi
	ok "mandoc -T lint is clean"
}

# --------------------------------------------------------------- test 21
# The image in the boot directory is hashed before it is taken for the
# image of the version: a file that was changed is put back.
test_detect_restores_changed() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "changed image: the image was not placed"
		return
	fi
	printf 'x' >> "${BOOT}/gsp_ga10x.bin"
	out=$(run_tool detect 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "changed image: detect failed" "exit ${rc}" "${out}"
		return
	fi
	if [ "$(sha256_of "${BOOT}/gsp_ga10x.bin")" != \
	    "$(sha256_of "${MIRROR}/620.73.04/gsp_ga10x.bin")" ]; then
		fail "changed image: the file was not put back"
		return
	fi
	ok "a changed image in the boot directory is put back"
}

# --------------------------------------------------------------- test 22
# A symbolic link is never taken for an installed image and is never
# written through.
test_detect_symlink() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	printf 'victim\n' > "${ROOT}/victim"
	rm -f "${BOOT}/gsp_ga10x.bin"
	ln -s "${ROOT}/victim" "${BOOT}/gsp_ga10x.bin"
	out=$(run_tool detect 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "symlink: detect failed" "exit ${rc}" "${out}"
		return
	fi
	if [ -L "${BOOT}/gsp_ga10x.bin" ]; then
		fail "symlink: the link was left in place"
		return
	fi
	if [ ! -f "${BOOT}/gsp_ga10x.bin" ]; then
		fail "symlink: there is no file in place"
		return
	fi
	if [ "$(cat "${ROOT}/victim")" != "victim" ]; then
		fail "symlink: the file the link pointed at was changed"
		return
	fi
	ok "a symbolic link is replaced and never written through"
}

# --------------------------------------------------------------- test 23
# The temporary files of the tool have a name that cannot be guessed: they
# are made with mkstemp(3) in the directory of the image and they are
# gone when the copy is done.  A link that is in the way is never written
# through and no temporary file is left behind.
test_temp_names() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	mkdir -p "${BOOT}"
	printf 'victim\n' > "${ROOT}/victim"
	printf 'victim2\n' > "${ROOT}/victim2"
	ln -s "${ROOT}/victim" "${BOOT}/gsp_ga10x.bin.tmp"
	ln -s "${ROOT}/victim2" "${BOOT}/SHA256SUMS.tmp"
	out=$(run_tool detect 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "temporary names: detect failed" "exit ${rc}" "${out}"
		return
	fi
	if [ "$(cat "${ROOT}/victim")" != "victim" ]; then
		fail "temporary names: the file the image link pointed at was changed"
		return
	fi
	if [ "$(cat "${ROOT}/victim2")" != "victim2" ]; then
		fail "temporary names: the file the inventory link pointed at was changed"
		return
	fi
	if ! check_no_temp_file; then
		fail "temporary names: a temporary file was left behind" \
		    "$(ls -1 "${BOOT}"/.nvidia-firmware.* 2>/dev/null)"
		return
	fi
	ok "the temporary files have names that cannot be guessed"
}

# --------------------------------------------------------------- test 24
# A name of an inventory that is not one path component is refused and
# the file outside the directory is not removed.
test_inventory_traversal() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	mkdir -p "${ROOT}/boot"
	printf 'victim\n' > "${ROOT}/boot/victim.txt"
	printf '%s  ../../victim.txt  620.73.04  -  0\n' \
	    "$(sha256_of "${ROOT}/boot/victim.txt")" >> "${BOOT}/SHA256SUMS"
	out=$(run_tool clean 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "traversal: clean did not fail" "${out}"
		return
	fi
	if [ ! -f "${ROOT}/boot/victim.txt" ]; then
		fail "traversal: the file outside the directory was removed"
		return
	fi
	out=$(run_tool remove ../../victim.txt 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "traversal: remove did not fail" "${out}"
		return
	fi
	if [ ! -f "${ROOT}/boot/victim.txt" ]; then
		fail "traversal: the file outside the directory was removed"
		return
	fi
	ok "a name of an inventory that is not one path component is refused"
}

# --------------------------------------------------------------- test 25
# The commands that change the boot directory refuse while the module is
# loaded and when the state of the module is not known.
test_module_guard() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	before=$(ls -1 "${BOOT}")
	for state in loaded unknown; do
		out=$(NVIDIA_FIRMWARE_TEST_KLD=${state} run_tool detect 2>&1)
		rc=$?
		if [ ${rc} -eq 0 ]; then
			fail "module guard: detect did not fail when the module is ${state}" \
			    "${out}"
			return
		fi
		out=$(NVIDIA_FIRMWARE_TEST_KLD=${state} run_tool install \
		    --force gsp_tu10x.bin 2>&1)
		rc=$?
		if [ ${rc} -eq 0 ]; then
			fail "module guard: install did not fail when the module is ${state}" \
			    "${out}"
			return
		fi
	done
	after=$(ls -1 "${BOOT}")
	if [ "${before}" != "${after}" ]; then
		fail "module guard: the boot directory changed" "${before}" "${after}"
		return
	fi
	ok "the commands that change the boot directory refuse while the module is loaded"
}

# --------------------------------------------------------------- test 26
# An image the cards need but that the mirror does not hold is an error
# and is named; the boot directory is left alone.
test_missing_required_image() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	rm -f "${MIRROR}/620.73.04/gsp_ga10x.bin" "${BOOT}/gsp_ga10x.bin"
	out=$(run_tool detect 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "missing image: detect did not fail" "${out}"
		return
	fi
	case "${out}" in
	*gsp_ga10x.bin*) ;;
	*)
		fail "missing image: the image is not named" "${out}"
		return
		;;
	esac
	if exists "${BOOT}/gsp_ga10x.bin"; then
		fail "missing image: the image was placed anyway"
		return
	fi
	ok "an image the cards need but that is missing is an error"
}

# --------------------------------------------------------------- test 27
# verify reports a file of the boot directory whose hash is not the hash
# of the pin table, and a file whose driver version is not in the pin
# table.
test_verify_changed_file() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	printf 'x' >> "${BOOT}/gsp_ga10x.bin"
	out=$(run_tool verify 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "verify changed: verify did not fail" "${out}"
		return
	fi
	case "${out}" in
	*gsp_ga10x.bin*) ;;
	*)
		fail "verify changed: the file is not named" "${out}"
		return
		;;
	esac
	ok "verify reports a file whose hash is not the pinned one"
}

# --------------------------------------------------------------- test 28
# An empty family table is refused; the images stay in place.
test_empty_family_table() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	: > "${ROOT}/share/nvidia-firmware/nvidia-firmware.families"
	out=$(run_tool detect 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "empty family table: detect did not fail" "${out}"
		return
	fi
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "empty family table: the image was taken away"
		return
	fi
	ok "an empty family table is refused"
}

# --------------------------------------------------------------- test 29
# A boot directory with files but without the inventory is not an empty
# boot directory.
test_missing_boot_inventory() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	rm -f "${BOOT}/SHA256SUMS"
	out=$(run_tool detect 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "missing inventory: detect did not fail" "${out}"
		return
	fi
	out=$(run_tool verify 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "missing inventory: verify did not fail" "${out}"
		return
	fi
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "missing inventory: the file was taken away"
		return
	fi
	ok "a boot directory without its inventory is refused"
}

# --------------------------------------------------------------- test 30
# The newest version of the pin table is the one that is used, and a
# version that the pin table does not name is never used.
test_version_of_the_pin_table() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	out=$(run_tool detect 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "pin table version: detect failed" "exit ${rc}" "${out}"
		return
	fi
	case "${out}" in
	*"version: 620.73.04"*) ;;
	*)
		fail "pin table version: the wrong version was chosen" "${out}"
		return
		;;
	esac
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "pin table version: the image was taken away"
		return
	fi
	ok "the newest version of the pin table is the one that is used"
}

# --------------------------------------------------------------- test 31
# The package script does not fail the installation when the firmware
# cannot be fetched: the package holds the tool and the pin table, and
# the machine may have no network at all.
test_pkg_install() {
	PKGINSTALL=${SRCDIR}/../pkg-install
	if [ ! -f "${PKGINSTALL}" ]; then
		printf '# the package script is not there; the test is skipped\n'
		return
	fi
	new_root
	mkdir -p "${ROOT}/prefix/sbin"
	cat > "${ROOT}/prefix/sbin/nvidia-firmware" <<-'STUB'
	#!/bin/sh
	echo "the firmware was not placed" >&2
	exit 1
	STUB
	chmod +x "${ROOT}/prefix/sbin/nvidia-firmware"
	out=$(PREFIX="${ROOT}/prefix" sh "${PKGINSTALL}" nvidia-firmware \
	    POST-INSTALL 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "pkg-install: the script failed the installation" \
		    "exit ${rc}" "${out}"
		return
	fi
	case "${out}" in
	*"--from"*) ;;
	*)
		fail "pkg-install: the way to fetch the files later is not told" \
		    "${out}"
		return
		;;
	esac
	ok "the package script does not fail the installation when the fetch fails"
}

# --------------------------------------------------------------- test 32
# The options the manual page names are the options the parser accepts,
# and the synopsis of every command names the options of that command.
#
# The manual page is read as it is written: the Fl macro of the mdoc
# language names an option, and the argument of the macro that begins
# with a hyphen is the option without its first hyphen.
manpage_options() {
	awk '
		function opt(tok) {
			if (tok == "n")
				return "-n"
			if (tok ~ /^-/)
				return "-" tok
			return ""
		}
		/^\.Sh SYNOPSIS/ { insyn = 1; next }
		/^\.Sh / { insyn = 0 }
		insyn && /^\.Nm/ { cmd = ""; next }
		insyn && /^\.Cm / { cmd = $2; next }
		insyn && /^\.(Op )?Fl[ \t]/ {
			line = $0
			sub(/^\.(Op )?Fl[ \t]+/, "", line)
			n = split(line, a, /[ \t]+/)
			for (i = 1; i <= n; i++) {
				if (a[i] ~ /^\./)
					break
				r = opt(a[i])
				if (r == "")
					continue
				# An option that comes before the first
				# command is an option of the program and
				# not of a command.
				if (cmd == "")
					print "global\t" r
				else
					print cmd "\t" r
			}
		}
	' "${MANPAGE}"
}

manpage_all_options() {
	awk '
		/^\.(Op )?Fl[ \t]/ {
			line = $0
			sub(/^\.(Op )?Fl[ \t]+/, "", line)
			n = split(line, a, /[ \t]+/)
			for (i = 1; i <= n; i++) {
				if (a[i] ~ /^\./)
					break
				if (a[i] == "n") {
					print "-n"
					continue
				}
				if (a[i] ~ /^-/)
					print "-" a[i]
			}
		}
	' "${MANPAGE}" | sort -u
}

test_manpage_options() {
	new_case
	usage=$(run_tool 2>&1)
	printf '%s\n' "${usage}" | awk '
		/^commands:/ { in_cmds = 1; next }
		/^options:/ { in_cmds = 0 }
		in_cmds && /^[ \t]+[a-z]/ {
			cmd = $1
			for (i = 2; i <= NF; i++) {
				tok = $i
				sub(/^\[/, "", tok)
				sub(/\]$/, "", tok)
				if (tok ~ /^--?[A-Za-z][A-Za-z0-9-]*$/)
					print cmd "\t" tok
			}
		}' | sort -u > "${WORK}/usage-options"
	manpage_options | awk -F'\t' '$1 != "global"' | sort -u \
	    > "${WORK}/man-options"
	if ! diff -u "${WORK}/usage-options" "${WORK}/man-options" \
	    > "${WORK}/option-diff"; then
		fail "man page options: the synopsis and the parser differ" \
		    "$(cat "${WORK}/option-diff")"
		return
	fi
	printf '%s\n' "${usage}" | awk '
		/^options:/ { in_opts = 1; next }
		in_opts && /^[ \t]+-/ {
			tok = $1
			sub(/^\[/, "", tok)
			sub(/\]$/, "", tok)
			if (tok ~ /^--?[A-Za-z][A-Za-z0-9-]*$/)
				print tok
		}' | sort -u > "${WORK}/usage-all"
	manpage_all_options > "${WORK}/man-all"
	if ! diff -u "${WORK}/usage-all" "${WORK}/man-all" \
	    > "${WORK}/option-all-diff"; then
		fail "man page options: the manual page and the parser differ" \
		    "$(cat "${WORK}/option-all-diff")"
		return
	fi
	for opt in $(cat "${WORK}/man-all"); do
		case "${opt}" in
		--force|--driver-version|--from|--version|--libxo) extra=x ;;
		*) extra= ;;
		esac
		out=$(run_tool verify "${opt}" ${extra} 2>&1)
		case "${out}" in
		*"unknown option"*)
			fail "man page options: ${opt} is not accepted" "${out}"
			return
			;;
		esac
	done
	# The options that come before the first command are options of the
	# program: they have to be in the list of the options of the
	# program.
	for opt in $(manpage_options | awk -F'\t' '$1 == "global" { print $2 }' |
	    sort -u); do
		if ! grep -q "^${opt}\$" "${WORK}/usage-all"; then
			fail "man page options: ${opt} is not an option of the" \
			    "program" "$(cat "${WORK}/usage-all")"
			return
		fi
	done
	ok "the options of the manual page are the options the parser accepts"
}

# --------------------------------------------------------------- test 33
# install does not need the family table.
test_install_without_families() {
	new_root
	rm -f "${ROOT}/share/nvidia-firmware/nvidia-firmware.families"
	out=$(run_tool install --force gsp_ga10x.bin 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "install without families: the install did not work" \
		    "exit ${rc}" "${out}"
		return
	fi
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "install without families: the image was not placed"
		return
	fi
	ok "install does not need the family table"
}

# --------------------------------------------------------------- test 34
# The images the family table names for a family are the images that are
# placed for a card of that family, and every one of them is in the
# version the table names.
test_family_mapping() {
	new_root
	if ! grep -q '^images 620.73.04 GA10x gsp_ga10x.bin' "${FAMILIES}"; then
		fail "family mapping: the table does not map GA10x to gsp_ga10x.bin"
		return
	fi
	awk '$1 == "images" { for (i = 4; i <= NF; i++) print $2 "\t" $i }' \
	    "${FAMILIES}" | sort -u > "${WORK}/family-images"
	while IFS="$(printf '\t')" read -r version image; do
		if ! awk -v v="${version}" -v i="${image}" \
		    '{ if ($1 == v && $2 == i) found = 1 } END { exit !found }' \
		    "${ROOT}/share/nvidia-firmware/versions.list"; then
			fail "family mapping: ${version}/${image} is not in the pin table"
			return
		fi
	done < "${WORK}/family-images"
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	got=$(find "${BOOT}" -maxdepth 1 -type f -name '*bin' -exec basename {} \; |
	    sort | awk '{ printf "%s%s", sep, $0; sep = "," }')
	if [ "${got}" != "gsp_ga10x.bin,ucodes_ga10x.bin" ]; then
		fail "family mapping: the GA10x card got ${got}"
		return
	fi
	ok "the family table names the images that are placed for the family"
}

# --------------------------------------------------------------- test 35
# The firmware is fetched, never shipped with the package.  The file is
# taken from the mirror, checked against the pin table and put in place.
test_fetch_from_mirror() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	out=$(run_tool fetch 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "fetch: the fetch failed" "exit ${rc}" "${out}"
		return
	fi
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "fetch: the file was not placed" "${out}"
		return
	fi
	if [ "$(sha256_of "${BOOT}/gsp_ga10x.bin")" != \
	    "$(sha256_of "${MIRROR}/620.73.04/gsp_ga10x.bin")" ]; then
		fail "fetch: the file that was placed is not the one of the mirror"
		return
	fi
	if ! grep -q '620\.73\.04' "${BOOT}/SHA256SUMS"; then
		fail "fetch: the inventory does not name the version" "${out}"
		return
	fi
	ok "the file is fetched from the mirror and put in place"
}

# --------------------------------------------------------------- test 36
# A file whose hash is not the pinned one is never placed, and nothing of
# it is left behind.
test_fetch_hash_mismatch() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	# The file has the size the pin table gives it and another hash: only
	# the hash of the pin table can tell that it is not the file of the
	# version.
	sed -e 's/^firmware/FIRMWARE/' \
	    "${MIRROR}/620.73.04/gsp_ga10x.bin" > "${WORK}/tampered"
	mv "${WORK}/tampered" "${MIRROR}/620.73.04/gsp_ga10x.bin"
	out=$(run_tool fetch 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "hash mismatch: the fetch did not fail" "${out}"
		return
	fi
	case "${out}" in
	*gsp_ga10x.bin*) ;;
	*)
		fail "hash mismatch: the file is not named" "${out}"
		return
		;;
	esac
	if exists "${BOOT}/gsp_ga10x.bin"; then
		fail "hash mismatch: the file was placed anyway"
		return
	fi
	if [ -e "${BOOT}/SHA256SUMS" ]; then
		fail "hash mismatch: the inventory was written anyway"
		return
	fi
	if ! check_no_temp_file; then
		fail "hash mismatch: a temporary file was left behind" \
		    "$(ls -1 "${BOOT}"/.nvidia-firmware.* 2>/dev/null)"
		return
	fi
	ok "a file whose hash is not the pinned one is never placed"
}

# --------------------------------------------------------------- test 37
# The mirror can also be named on the command line, and the option wins
# over the variable of the environment.
test_fetch_from_option() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	other=${ROOT}/other-mirror
	mkdir -p "${other}/620.73.04"
	cp "${MIRROR}/620.73.04/gsp_ga10x.bin" "${other}/620.73.04/gsp_ga10x.bin"
	cp "${MIRROR}/620.73.04/ucodes_ga10x.bin" "${other}/620.73.04/ucodes_ga10x.bin"
	mkdir -p "${ROOT}/empty-mirror"
	out=$(env NVIDIA_FIRMWARE_TEST_ROOT="${ROOT}" \
	    NVIDIA_FIRMWARE_TEST_PCI="${PCIFILE}" \
	    NVIDIA_FIRMWARE_TEST_KLD=not \
	    NVIDIA_FIRMWARE_TEST_LOCK="${WORK}/nvidia-firmware.lock" \
	    NVIDIA_FIRMWARE_MIRROR="${ROOT}/empty-mirror" \
	    "${BIN}" fetch --from "${other}" 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "--from: the fetch failed" "exit ${rc}" "${out}"
		return
	fi
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "--from: the file was not placed" "${out}"
		return
	fi
	ok "the mirror can be named on the command line"
}

# --------------------------------------------------------------- test 38
# A machine that has no network: the mirror holds nothing, so the tool
# cannot find the file.  The error names the file that is missing.  The
# tests never go to the network.
test_fetch_offline() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	empty=${ROOT}/empty-mirror
	mkdir -p "${empty}"
	out=$(env NVIDIA_FIRMWARE_TEST_ROOT="${ROOT}" \
	    NVIDIA_FIRMWARE_TEST_PCI="${PCIFILE}" \
	    NVIDIA_FIRMWARE_TEST_KLD=not \
	    NVIDIA_FIRMWARE_TEST_LOCK="${WORK}/nvidia-firmware.lock" \
	    NVIDIA_FIRMWARE_MIRROR="${empty}" \
	    "${BIN}" fetch 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "offline: the fetch did not fail" "${out}"
		return
	fi
	case "${out}" in
	*gsp_ga10x.bin*)
		;;
	*)
		fail "offline: the file is not named" "${out}"
		return
		;;
	esac
	if [ -e "${BOOT}/gsp_ga10x.bin" ]; then
		fail "offline: the file was placed anyway"
		return
	fi
	ok "a machine that has no network is told which file is missing"
}

# The temporary files of the tool have a name that cannot be guessed: they
# are made with mkstemp(3) and they are gone when the copy is done.  A
# file that is left behind is a leak, and the check looks for the name the
# tool really uses.
check_no_temp_file() {
	if ls -1 "${BOOT}"/.nvidia-firmware.* >/dev/null 2>&1; then
		return 1
	fi
	return 0
}

# --------------------------------------------------------------- test 36
# Every file is checked before the boot directory is changed.  A check
# that fails leaves the boot directory as it was, and the dry run fails in
# the same way as the run that follows it.
test_integrity_before_change() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	# The second file of the card is the one that is broken: the file
	# is not the file of the version any more.
	printf 'tampered\n' > "${MIRROR}/620.73.04/ucodes_ga10x.bin"
	out=$(run_tool detect 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "integrity: detect did not fail" "${out}"
		return
	fi
	case "${out}" in
	*ucodes_ga10x.bin*) ;;
	*)
		fail "integrity: the image that is broken is not named" "${out}"
		return
		;;
	esac
	if [ -e "${BOOT}/gsp_ga10x.bin" ]; then
		fail "integrity: an image was placed although the check failed"
		return
	fi
	if [ -e "${BOOT}/SHA256SUMS" ]; then
		fail "integrity: the inventory was written although the check failed"
		return
	fi
	# The dry run has to fail in the same way.
	out=$(run_tool detect -n 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "integrity: the dry run did not fail" "${out}"
		return
	fi
	if [ -e "${BOOT}/gsp_ga10x.bin" ] || [ -e "${BOOT}/SHA256SUMS" ]; then
		fail "integrity: the dry run changed the boot directory"
		return
	fi
	# The file that is whole again makes both runs work.
	printf 'firmware 620.73.04 ucodes_ga10x\n' \
	    > "${MIRROR}/620.73.04/ucodes_ga10x.bin"
	out=$(run_tool detect 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "integrity: detect failed after the image was whole again" \
		    "exit ${rc}" "${out}"
		return
	fi
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "integrity: the image was not placed"
		return
	fi
	ok "every file is checked before the boot directory is changed"
}

# --------------------------------------------------------------- test 37
# A file of somebody else that has the name of an image is never
# overwritten.
test_foreign_not_overwritten() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	# An image of ours is in the directory, and a file of somebody else
	# has the name of the image the card needs.
	run_tool install --force gsp_tu10x.bin >/dev/null 2>&1
	printf 'theirs\n' > "${BOOT}/gsp_ga10x.bin"
	out=$(run_tool detect 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "foreign: detect did not fail" "${out}"
		return
	fi
	case "${out}" in
	*gsp_ga10x.bin*) ;;
	*)
		fail "foreign: the file is not named" "${out}"
		return
		;;
	esac
	if [ "$(cat "${BOOT}/gsp_ga10x.bin")" != "theirs" ]; then
		fail "foreign: the file of somebody else was overwritten"
		return
	fi
	ok "a file of somebody else is never overwritten"
}

# --------------------------------------------------------------- test 38
# A symbolic link is never an installed image.
test_verify_symlink() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	# A file with the same bytes, reached through a link.
	cp "${BOOT}/gsp_ga10x.bin" "${ROOT}/elsewhere.bin"
	rm -f "${BOOT}/gsp_ga10x.bin"
	ln -s "${ROOT}/elsewhere.bin" "${BOOT}/gsp_ga10x.bin"
	out=$(run_tool verify 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "verify symlink: verify did not fail" "${out}"
		return
	fi
	case "${out}" in
	*gsp_ga10x.bin*) ;;
	*)
		fail "verify symlink: the image is not named" "${out}"
		return
		;;
	esac
	out=$(run_tool list --json 2>&1)
	if ! printf '%s' "${out}" | python3 -I -c '
import json, sys
doc = json.load(sys.stdin)
def walk(node):
    if isinstance(node, dict):
        yield node
        for value in node.values():
            for item in walk(value):
                yield item
    elif isinstance(node, list):
        for value in node:
            for item in walk(value):
                yield item
bad = [n for n in walk(doc)
       if n.get("name") == "gsp_ga10x.bin" and n.get("placed") != "no"]
sys.exit(1 if bad else 0)
' >/dev/null 2>&1; then
		fail "verify symlink: the list says that the link is placed" "${out}"
		return
	fi
	ok "a symbolic link is never an installed image"
}

# --------------------------------------------------------------- test 39
# A file that is not there is not placed, even when the inventory still
# names it.
test_list_missing() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	rm -f "${BOOT}/gsp_ga10x.bin"
	out=$(run_tool list --json 2>&1)
	if ! printf '%s' "${out}" | python3 -I -c '
import json, sys
doc = json.load(sys.stdin)
def walk(node):
    if isinstance(node, dict):
        yield node
        for value in node.values():
            for item in walk(value):
                yield item
    elif isinstance(node, list):
        for value in node:
            for item in walk(value):
                yield item
bad = [n for n in walk(doc)
       if n.get("name") == "gsp_ga10x.bin" and n.get("placed") != "no"]
sys.exit(1 if bad else 0)
' >/dev/null 2>&1; then
		fail "list missing: the list says that the image is placed" "${out}"
		return
	fi
	out=$(run_tool list 2>&1)
	placed=$(printf '%s\n' "${out}" | \
	    awk '$2 == "gsp_ga10x.bin" { print $(NF-1) }' | sort -u | tr '\n' ' ')
	if printf '%s' "${placed}" | grep -q 'yes'; then
		fail "list missing: the text says that the image is placed" \
		    "${placed}" "${out}"
		return
	fi
	ok "an image that is not there is not placed"
}

# --------------------------------------------------------------- test 40
# The inventory stays when a file of somebody else is in the directory,
# and the clean still works.
test_cleanup_keeps_inventory() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	printf 'theirs\n' > "${BOOT}/foreign.bin"
	: > "${PCIFILE}"			# the card is gone
	run_tool detect >/dev/null 2>&1
	if [ ! -f "${BOOT}/foreign.bin" ]; then
		fail "cleanup: the file of somebody else was removed"
		return
	fi
	out=$(run_tool clean 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "cleanup: clean did not work" "exit ${rc}" "${out}"
		return
	fi
	if [ ! -f "${BOOT}/foreign.bin" ]; then
		fail "cleanup: the file of somebody else was removed by clean"
		return
	fi
	ok "the inventory stays when a file of somebody else is in the directory"
}

# --------------------------------------------------------------- test 41
# The JSON of the clean names every image that was removed.
test_clean_json() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	out=$(run_tool clean --json 2>&1)
	if ! printf '%s' "${out}" | python3 -I -c '
import json, sys
doc = json.load(sys.stdin)
names = []
def walk(node):
    if isinstance(node, dict):
        yield node
        for value in node.values():
            for item in walk(value):
                yield item
    elif isinstance(node, list):
        for value in node:
            for item in walk(value):
                yield item
for node in walk(doc):
    if isinstance(node, dict) and "name" in node:
        names.append(node["name"])
want = {"gsp_ga10x.bin", "ucodes_ga10x.bin"}
sys.exit(0 if want.issubset(set(names)) else 1)
' >/dev/null 2>&1; then
		fail "clean json: the removed images are not all in the output" \
		    "${out}"
		return
	fi
	ok "the JSON of the clean names every image that was removed"
}

# --------------------------------------------------------------- test 42
# The package scripts use the prefix of the package, they do their work
# in the phase that is theirs and they fail when the tool is not there.
test_pkg_scripts() {
	PKGINSTALL=${SRCDIR}/../pkg-install
	PKGDEINSTALL=${SRCDIR}/../pkg-deinstall
	if [ ! -f "${PKGINSTALL}" ] || [ ! -f "${PKGDEINSTALL}" ]; then
		printf '# the package scripts are not there; the test is skipped\n'
		return
	fi
	new_root
	mkdir -p "${ROOT}/prefix/sbin"
	cat > "${ROOT}/prefix/sbin/nvidia-firmware" <<-'STUB'
	#!/bin/sh
	echo "$*" >> "${NF_TEST_STUB_LOG}"
	exit 0
	STUB
	chmod +x "${ROOT}/prefix/sbin/nvidia-firmware"
	NF_TEST_STUB_LOG=${ROOT}/stub.log
	export NF_TEST_STUB_LOG
	: > "${NF_TEST_STUB_LOG}"

	# The install script does nothing before the files of the package
	# are in place: the tool is not there yet.
	out=$(env -u PREFIX PKG_PREFIX="${ROOT}/prefix" sh "${PKGINSTALL}" \
	    nvidia-firmware PRE-INSTALL 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "pkg scripts: the install script failed in PRE-INSTALL" "${out}"
		return
	fi
	if [ -s "${NF_TEST_STUB_LOG}" ]; then
		fail "pkg scripts: the install script ran the tool in PRE-INSTALL" \
		    "$(cat "${NF_TEST_STUB_LOG}")"
		return
	fi

	# The install script uses the prefix of the package.
	out=$(env -u PREFIX PKG_PREFIX="${ROOT}/prefix" sh "${PKGINSTALL}" \
	    nvidia-firmware POST-INSTALL 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "pkg scripts: the install script failed in POST-INSTALL" "${out}"
		return
	fi
	if ! grep -q '^fetch$' "${NF_TEST_STUB_LOG}"; then
		fail "pkg scripts: the install script did not run fetch" \
		    "${out}" "$(cat "${NF_TEST_STUB_LOG}")"
		return
	fi

	# The module has to be unloaded before the package can go away.
	if command -v kldstat >/dev/null 2>&1 && kldstat -q -m nvidia \
	    >/dev/null 2>&1; then
		printf '# the nvidia module is loaded; the deinstall is not tested\n'
		return
	fi

	: > "${NF_TEST_STUB_LOG}"
	# The deinstall script does nothing before the package goes away.
	out=$(env -u PREFIX PKG_PREFIX="${ROOT}/prefix" sh "${PKGDEINSTALL}" \
	    nvidia-firmware PRE-DEINSTALL 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "pkg scripts: the deinstall script failed in PRE-DEINSTALL" \
		    "${out}"
		return
	fi
	if [ -s "${NF_TEST_STUB_LOG}" ]; then
		fail "pkg scripts: the deinstall script ran the tool too early" \
		    "$(cat "${NF_TEST_STUB_LOG}")"
		return
	fi

	# The removal of the package takes the firmware out.
	out=$(env -u PREFIX PKG_PREFIX="${ROOT}/prefix" sh "${PKGDEINSTALL}" \
	    nvidia-firmware DEINSTALL 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "pkg scripts: the deinstall script failed" "exit ${rc}" "${out}"
		return
	fi
	if ! grep -q '^clean$' "${NF_TEST_STUB_LOG}"; then
		fail "pkg scripts: the deinstall script did not run clean" \
		    "${out}" "$(cat "${NF_TEST_STUB_LOG}")"
		return
	fi

	# Without the tool the script has to fail: the firmware would stay
	# in /boot.
	rm -f "${ROOT}/prefix/sbin/nvidia-firmware"
	out=$(env -u PREFIX PKG_PREFIX="${ROOT}/prefix" sh "${PKGDEINSTALL}" \
	    nvidia-firmware DEINSTALL 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "pkg scripts: the deinstall script did not fail without the tool" \
		    "${out}"
		return
	fi
	ok "the package scripts use the prefix of the package and their phase"
}

# --------------------------------------------------------------- test 43
# The last value of the setting in the configuration is the one that
# counts.
test_rc_last_value() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	printf 'nvidia_firmware_no_delete="YES"\nnvidia_firmware_no_delete="NO"\n' \
	    > "${ROOT}/etc/rc.conf"
	: > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	if exists "${BOOT}/gsp_ga10x.bin"; then
		fail "rc.conf: the image was kept although the last value is NO"
		return
	fi
	# The file of rc.conf.d is read after rc.conf, so it has the last
	# word.
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	printf 'nvidia_firmware_no_delete="NO"\n' > "${ROOT}/etc/rc.conf"
	printf 'nvidia_firmware_no_delete="YES"\n' \
	    > "${ROOT}/etc/rc.conf.d/nvidia_firmware"
	: > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "rc.conf.d: the image was removed although the last value is YES"
		return
	fi
	ok "the last value of the setting in the configuration is the one that counts"
}

# --------------------------------------------------------------- test 44
# The version of the program and the version of the schema are in the
# JSON output of every command, and the program writes its own version
# when it is asked for it.
test_version_of_the_program() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	out=$(run_tool --version 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "program version: the version was not written" \
		    "exit ${rc}" "${out}"
		return
	fi
	case "${out}" in
	*nvidia-firmware*)
		;;
	*)
		fail "program version: the name of the program is not in the" \
		    "output" "${out}"
		return
		;;
	esac
	# The version is ours and it is the date of the day the package
	# was made: three numbers with dots in between.
	version=$(printf '%s' "${out}" | sed -n 's/^nvidia-firmware //p')
	case "${version}" in
	[0-9][0-9][0-9][0-9].[0-9][0-9].[0-9][0-9]*)
		;;
	*)
		fail "program version: the version ${version} is not a date" \
		    "version" "${out}"
		return
		;;
	esac
	out=$(run_tool --version --json 2>&1)
	if ! printf '%s' "${out}" | python3 -I -c '
import json, sys
doc = json.load(sys.stdin)
def walk(node):
    if isinstance(node, dict):
        yield node
        for value in node.values():
            for item in walk(value):
                yield item
    elif isinstance(node, list):
        for value in node:
            for item in walk(value):
                yield item
want = ("version", "version_major", "version_minor", "version_patch",
        "schema_version")
holders = [n for n in walk(doc) if "schema_version" in n]
bad = [n for n in holders if not all(k in n for k in want)]
sys.exit(1 if bad or not holders else 0)
' >/dev/null 2>&1; then
		fail "program version: the JSON does not hold the version" "${out}"
		return
	fi
	out=$(run_tool list --json 2>&1)
	if ! printf '%s' "${out}" | python3 -I -c '
import json, sys
doc = json.load(sys.stdin)
def walk(node):
    if isinstance(node, dict):
        yield node
        for value in node.values():
            for item in walk(value):
                yield item
    elif isinstance(node, list):
        for value in node:
            for item in walk(value):
                yield item
want = ("version", "version_major", "version_minor", "version_patch",
        "schema_version")
holders = [n for n in walk(doc) if "schema_version" in n]
bad = [n for n in holders if not all(k in n for k in want)]
sys.exit(1 if bad or not holders else 0)
' >/dev/null 2>&1; then
		fail "program version: the JSON of the list does not hold the" \
		    "version" "${out}"
		return
	fi
	ok "the version of the program is in the text and in the JSON"
}

# --------------------------------------------------------------- test 45
# A remove that failed does not say that it removed anything.
test_remove_failure() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	# A directory where the image is: it cannot be removed.
	rm -f "${BOOT}/gsp_ga10x.bin"
	mkdir "${BOOT}/gsp_ga10x.bin"
	out=$(run_tool remove gsp_ga10x.bin 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "remove failure: the remove did not fail" "${out}"
		return
	fi
	case "${out}" in
	*"removed:"*)
		fail "remove failure: the tool says that it removed the image" \
		    "${out}"
		return
		;;
	esac
	if [ ! -d "${BOOT}/gsp_ga10x.bin" ]; then
		fail "remove failure: the directory was removed"
		return
	fi
	ok "a remove that failed does not say that it removed anything"
}

# --------------------------------------------------------------- test 46
# The package holds the tool, the pin table and the table of the
# families, and no firmware at all.  The rule of devd is there as well.
test_package_layout() {
	PORT=${SRCDIR}/..
	MAKEFILE=${PORT}/Makefile
	PLIST=${PORT}/pkg-plist
	TABLE=${PORT}/files/versions.list
	if [ ! -f "${MAKEFILE}" ] || [ ! -f "${PLIST}" ] || \
	    [ ! -f "${TABLE}" ]; then
		printf '# the port is not there; the test is skipped\n'
		return
	fi
	data=$(awk -F= '/^FW_DATA_DIR=/ { print $2 }' "${MAKEFILE}" | \
	    tr -d ' \t')
	if [ "${data}" != '${DATADIR}' ]; then
		fail "package layout: the data directory of the port is ${data}" \
		    "and not \${DATADIR}"
		return
	fi
	if grep -q 'nvidia-firmware/nvidia-firmware' "${PLIST}"; then
		fail "package layout: the plist has the directory twice"
		return
	fi
	if grep -q 'SIZES' "${PLIST}"; then
		fail "package layout: the plist names a file that is not installed"
		return
	fi
	# No firmware is shipped with the package.
	if grep -qE 'gsp_[a-z0-9]*\.bin|ucodes_[a-z0-9]*\.bin' "${PLIST}"; then
		fail "package layout: the plist names a firmware" \
		    "$(grep -E 'gsp_[a-z0-9]*\.bin|ucodes_[a-z0-9]*\.bin' \
		    "${PLIST}")"
		return
	fi
	# The port fetches nothing at build time.
	if grep -qE '^MASTER_SITES' "${MAKEFILE}" || \
	    grep -qE '^DISTFILES=.+' "${MAKEFILE}"; then
		fail "package layout: the port fetches a file at build time" \
		    "$(grep -E '^(DISTFILES|MASTER_SITES)' "${MAKEFILE}")"
		return
	fi
	# The pin table and the table of the families are installed.
	for f in versions.list nvidia-firmware.families; do
		if ! grep -q "^%%DATADIR%%/${f}\$" "${PLIST}"; then
			fail "package layout: the plist does not name ${f}"
			return
		fi
	done
	if ! grep -q '^etc/devd/nvidia_firmware.conf$' "${PLIST}"; then
		fail "package layout: the plist does not name the rule of devd"
		return
	fi
	# The pin table is the one of the port.
	if ! grep -q 'versions.list' "${MAKEFILE}"; then
		fail "package layout: the pin table is not installed by the port"
		return
	fi
	# Every image the table of the families names is in the pin table.
	for image in $(awk '$1 == "images" { for (i = 4; i <= NF; i++) print $i }' \
	    "${FAMILIES}" | sort -u); do
		if ! awk -v i="${image}" \
		    '{ if ($2 == i) found = 1 } END { exit !found }' "${TABLE}"; then
			fail "package layout: ${image} is not in the pin table"
			return
		fi
	done
	ok "the package holds the tool and the pin table and no firmware"
}

# --------------------------------------------------------------- test 47
# The dry run changes nothing and says what the run will do.
test_dry_run() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	out=$(run_tool detect -n 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "dry run: detect -n failed" "exit ${rc}" "${out}"
		return
	fi
	case "${out}" in
	*"placed: gsp_ga10x.bin"*) ;;
	*)
		fail "dry run: the image that would be placed is not named" "${out}"
		return
		;;
	esac
	if [ -e "${BOOT}" ]; then
		fail "dry run: the boot directory was made"
		return
	fi
	ok "the dry run changes nothing"
}

# --------------------------------------------------------------- test 48
# The images of the older versions stay when they are asked for, and
# every image of a version can be placed at once.
test_keep_versions_and_all() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	NVIDIA_FIRMWARE_TEST_PIN=580.178.04 run_tool detect >/dev/null 2>&1
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "keep versions: the image of the pinned version is missing"
		return
	fi
	: > "${PCIFILE}"
	run_tool detect --keep-versions >/dev/null 2>&1
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "keep versions: the image of the older version was removed"
		return
	fi
	: > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	if exists "${BOOT}/gsp_ga10x.bin"; then
		fail "keep versions: the image of the older version stayed"
		return
	fi
	out=$(run_tool install --all 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "install all: the install did not work" "exit ${rc}" "${out}"
		return
	fi
	for i in gsp_ga10x.bin gsp_tu10x.bin ucodes_ga10x.bin ucodes_tu10x.bin; do
		if ! exists "${BOOT}/${i}"; then
			fail "install all: ${i} was not placed"
			return
		fi
	done
	ok "the images of the older versions stay when they are asked for"
}

# --------------------------------------------------------------- test 49
# The version of the install chooses the version, and the status tells
# what is in the boot directory.
test_version_and_status() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	out=$(run_tool install --all --driver-version 580.178.04 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "version and status: the install did not work" "exit ${rc}" \
		    "${out}"
		return
	fi
	if ! grep -q '580\.178\.04' "${BOOT}/SHA256SUMS"; then
		fail "version and status: the inventory does not name the version" \
		    "${out}"
		return
	fi
	# A version that is not installed is an error.
	out=$(run_tool install --all --driver-version 999.99.99 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "version and status: the install of a version that is not" \
		    "there did not fail" "${out}"
		return
	fi
	out=$(run_tool status 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "version and status: the status did not work" "exit ${rc}" "${out}"
		return
	fi
	case "${out}" in
	*gsp_ga10x.bin*) ;;
	*)
		fail "version and status: the image is not in the status" "${out}"
		return
		;;
	esac
	ok "the version of the install chooses the version and the status tells what is there"
}

# --------------------------------------------------------------- test 50
# A no-delete policy that cannot be read means that nothing is deleted:
# the tool cannot tell that it may take the images away.
#
# A test that runs as root reads a file that has no rights for others,
# so the tool is told that the file cannot be read.
test_rc_unreadable() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	printf 'nvidia_firmware_no_delete="YES"\n' > "${ROOT}/etc/rc.conf"
	chmod 000 "${ROOT}/etc/rc.conf"
	: > "${PCIFILE}"			# the card is gone
	deny=${ROOT}/etc/rc.conf
	NVIDIA_FIRMWARE_TEST_DENY="${deny}"
	export NVIDIA_FIRMWARE_TEST_DENY
	out=$(run_tool detect 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		chmod 644 "${ROOT}/etc/rc.conf"
		fail "unreadable policy: the run did not fail" "exit ${rc}" \
		    "${out}"
		return
	fi
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		chmod 644 "${ROOT}/etc/rc.conf"
		fail "unreadable policy: the image was removed anyway" "${out}"
		return
	fi
	case "${out}" in
	*"no-delete policy"*) ;;
	*)
		chmod 644 "${ROOT}/etc/rc.conf"
		fail "unreadable policy: the reason is not told" "${out}"
		return
		;;
	esac
	out=$(run_tool status 2>&1)
	rc=$?
	unset NVIDIA_FIRMWARE_TEST_DENY
	chmod 644 "${ROOT}/etc/rc.conf"
	if [ ${rc} -eq 0 ]; then
		fail "unreadable policy: the status did not fail" "${out}"
		return
	fi
	ok "a no-delete policy that cannot be read means that nothing is deleted"
}

# --------------------------------------------------------------- test 51
# A boot directory that cannot be read is an error and not an empty
# directory: the tool cannot tell which files are its own.
#
# A test that runs as root reads a directory that has no rights for
# others, so the tool is told that the directory cannot be read.
test_boot_unreadable() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	before=$(ls -1 "${BOOT}")
	chmod 0333 "${BOOT}"
	deny=${BOOT}
	NVIDIA_FIRMWARE_TEST_DENY="${deny}"
	export NVIDIA_FIRMWARE_TEST_DENY
	out=$(run_tool detect 2>&1)
	rc=$?
	unset NVIDIA_FIRMWARE_TEST_DENY
	chmod 0755 "${BOOT}"
	if [ ${rc} -eq 0 ]; then
		fail "unreadable boot: the run did not fail" "exit ${rc}" "${out}"
		return
	fi
	after=$(ls -1 "${BOOT}")
	if [ "${before}" != "${after}" ]; then
		fail "unreadable boot: the boot directory changed" "${before}" \
		    "${after}"
		return
	fi
	ok "a boot directory that cannot be read is an error"
}

# --------------------------------------------------------------- test 52
# The state of the nvidia module and the change of the boot directory are
# one thing under one lock: a run that finds the lock taken does not
# change anything.
test_lock() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	LOCK=${WORK}/nvidia-firmware.lock
	: > "${LOCK}"
	python3 -I -c '
import fcntl, os, sys, time
fd = os.open(sys.argv[1], os.O_RDWR)
fcntl.lockf(fd, fcntl.LOCK_EX)
sys.stdout.write("locked\n")
sys.stdout.flush()
time.sleep(60)
' "${LOCK}" > "${WORK}/lock.out" 2>&1 &
	holder=$!
	i=0
	while [ ! -s "${WORK}/lock.out" ] && [ ${i} -lt 100 ]; do
		sleep 0.1
		i=$((i + 1))
	done
	if [ ! -s "${WORK}/lock.out" ]; then
		kill "${holder}" 2>/dev/null
		fail "lock: the lock of the test was not taken"
		return
	fi
	out=$(run_tool detect 2>&1)
	rc=$?
	kill "${holder}" 2>/dev/null
	wait "${holder}" 2>/dev/null
	if [ ${rc} -eq 0 ]; then
		fail "lock: the run did not fail while the lock was held" \
		    "exit ${rc}" "${out}"
		return
	fi
	case "${out}" in
	*lock*) ;;
	*)
		fail "lock: the reason is not told" "${out}"
		return
		;;
	esac
	ok "the tool does not change anything while another run holds the lock"
}

# --------------------------------------------------------------- test 53
# A machine without a PCI bus: the command says why, changes nothing and
# succeeds, so that the package can be installed in a jail.
test_no_pci_bus() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	before=$(ls -1 "${BOOT}")
	saved=${PCIFILE}
	PCIFILE=none
	out=$(run_tool detect 2>&1)
	rc=$?
	PCIFILE=${saved}
	if [ ${rc} -ne 0 ]; then
		fail "no pci bus: the run did not succeed" "exit ${rc}" "${out}"
		return
	fi
	case "${out}" in
	*"PCI bus"*) ;;
	*)
		fail "no pci bus: the reason is not told" "${out}"
		return
		;;
	esac
	after=$(ls -1 "${BOOT}")
	if [ "${before}" != "${after}" ]; then
		fail "no pci bus: the boot directory changed" "${before}" \
		    "${after}"
		return
	fi
	ok "a machine without a PCI bus is told why and nothing is changed"
}

# --------------------------------------------------------------- test 54
# The inventory is written when a copy could not be put in place: an
# image that is in the boot directory and that the inventory does not
# name is a file of somebody else.
test_inventory_after_partial_failure() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	mkdir -p "${BOOT}"
	# The third image of the version cannot be put in place: there is a
	# directory in the way.
	mkdir "${BOOT}/ucodes_ga10x.bin"
	out=$(run_tool install --all 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "partial failure: the install did not fail" "${out}"
		return
	fi
	if ! exists "${BOOT}/SHA256SUMS"; then
		fail "partial failure: the inventory was not written" "${out}"
		return
	fi
	if ! grep -q 'gsp_ga10x.bin' "${BOOT}/SHA256SUMS"; then
		fail "partial failure: the image that was placed is not named" \
		    "${out}"
		return
	fi
	out=$(run_tool status 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "partial failure: the status does not work on the directory" \
		    "${out}"
		return
	fi
	ok "the inventory is written after a partial failure"
}

# --------------------------------------------------------------- test 55
# The table of the families does not lose the list of the families when
# the memory is gone: the tool says that it cannot read the table
# instead of dying.
test_family_alloc_fail() {
	new_root
	cat > "${ROOT}/share/nvidia-firmware/nvidia-firmware.families" <<-'FAMILIES'
	family GA10x
	attaches yes
	range 0x2204 0x2204
	family TU10x
	attaches yes
	range 0x1e04 0x1e04
	FAMILIES
	out=$(NVIDIA_FIRMWARE_TEST_FAMILIES_ALLOC_FAIL=2 run_tool detect 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "alloc fail: the run did not fail" "exit ${rc}" "${out}"
		return
	fi
	if [ ${rc} -gt 128 ]; then
		fail "alloc fail: the tool died of a signal" "exit ${rc}" "${out}"
		return
	fi
	if [ -e "${BOOT}" ]; then
		fail "alloc fail: the boot directory was made" "${out}"
		return
	fi
	ok "the table of the families does not lose the list when it cannot grow"
}

# --------------------------------------------------------------- test 56
# The boot directory the tool writes in is one that only its owner can
# write in: a directory that others can write in is not safe, because the
# file that is checked could be replaced between the check and the
# rename.
test_boot_directory_private() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	mkdir -p "${BOOT}"
	chmod 0777 "${BOOT}"
	out=$(run_tool fetch 2>&1)
	rc=$?
	chmod 0755 "${BOOT}"
	if [ ${rc} -eq 0 ]; then
		fail "private dir: the fetch did not fail" "${out}"
		return
	fi
	if [ -f "${BOOT}/gsp_ga10x.bin" ]; then
		fail "private dir: the file was placed anyway"
		return
	fi
	ok "a boot directory that others can write in is refused"
}

# --------------------------------------------------------------- test 58
# The list of the firmware the package supports is the pin table with the
# families that the files serve.
test_list_supported() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	out=$(run_tool list --supported 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "supported list: the exit code is ${rc}" "${out}"
		return
	fi
	for want in gsp_ga10x.bin ucodes_ga10x.bin GA10x 'RTX 30xx'; do
		case "${out}" in
		*"${want}"*) ;;
		*)
			fail "supported list: ${want} is not in the output" "${out}"
			return
			;;
		esac
	done
	out=$(run_tool list --supported --json 2>&1)
	if ! printf '%s' "${out}" | python3 -I -c '
import json, sys
doc = json.load(sys.stdin)
names = []
def walk(node):
    if isinstance(node, dict):
        yield node
        for value in node.values():
            for item in walk(value):
                yield item
    elif isinstance(node, list):
        for value in node:
            for item in walk(value):
                yield item
for node in walk(doc):
    if isinstance(node, dict) and "name" in node:
        names.append(node["name"])
want = {"gsp_ga10x.bin", "ucodes_ga10x.bin"}
sys.exit(0 if want.issubset(set(names)) else 1)
' >/dev/null 2>&1; then
		fail "supported list: the JSON does not name the files" "${out}"
		return
	fi
	ok "the list of the supported firmware is the pin table"
}

# --------------------------------------------------------------- test 60
# The install never overwrites a file of somebody else: the file that has
# the name of an image and that the inventory does not name is refused
# before anything is fetched.
test_install_foreign_file() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	mkdir -p "${BOOT}"
	printf '# sha256  image  version  forced  size\n' > "${BOOT}/SHA256SUMS"
	printf 'theirs\n' > "${BOOT}/gsp_ga10x.bin"
	out=$(run_tool install --all 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "install foreign: the install did not fail" "${out}"
		return
	fi
	case "${out}" in
	*gsp_ga10x.bin*) ;;
	*)
		fail "install foreign: the file is not named" "${out}"
		return
		;;
	esac
	if [ "$(cat "${BOOT}/gsp_ga10x.bin")" != "theirs" ]; then
		fail "install foreign: the file of somebody else was overwritten"
		return
	fi
	ok "the install never overwrites a file of somebody else"
}

# --------------------------------------------------------------- test 61
# The dry run of the install checks the files it would place and says
# what it would do: it fails in the same way as the run that follows it
# and it places nothing.
test_install_dry_run() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	# The file of the mirror is not the file that is pinned.
	sed -e 's/^firmware/FIRMWARE/' \
	    "${MIRROR}/620.73.04/gsp_ga10x.bin" > "${WORK}/tampered"
	mv "${WORK}/tampered" "${MIRROR}/620.73.04/gsp_ga10x.bin"
	out=$(run_tool install --all -n 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "install dry run: the dry run did not fail" "${out}"
		return
	fi
	if printf '%s\n' "${out}" | grep -q '^installed: '; then
		fail "install dry run: the dry run says that a file was installed" \
		    "${out}"
		return
	fi
	if [ -e "${BOOT}" ]; then
		fail "install dry run: the boot directory was made" "${out}"
		return
	fi
	# The file is whole again: the dry run says what it would do.
	printf 'firmware 620.73.04 gsp_ga10x\n' \
	    > "${MIRROR}/620.73.04/gsp_ga10x.bin"
	out=$(run_tool install --all -n 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "install dry run: the dry run failed with a whole mirror" \
		    "exit ${rc}" "${out}"
		return
	fi
	case "${out}" in
	*"would install: gsp_ga10x.bin"*) ;;
	*)
		fail "install dry run: the dry run does not say what it would do" \
		    "${out}"
		return
		;;
	esac
	if [ -e "${BOOT}" ]; then
		fail "install dry run: the boot directory was made" "${out}"
		return
	fi
	ok "the dry run of the install checks the files and places nothing"
}

# --------------------------------------------------------------- test 62
# The JSON of an install that failed names no file that is installed.
test_install_json_failure() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	sed -e 's/^firmware/FIRMWARE/' \
	    "${MIRROR}/620.73.04/gsp_ga10x.bin" > "${WORK}/tampered"
	mv "${WORK}/tampered" "${MIRROR}/620.73.04/gsp_ga10x.bin"
	out=$(run_tool install --all --json 2>/dev/null)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "install json: the install did not fail" "${out}"
		return
	fi
	if ! printf '%s' "${out}" | python3 -I -c '
import json, sys
doc = json.load(sys.stdin)
node = doc.get("install", doc)
installed = node.get("installed", [])
failed = node.get("failed", [])
sys.exit(0 if not installed and failed else 1)
' >/dev/null 2>&1; then
		fail "install json: the JSON names a file that is not installed" \
		    "${out}"
		return
	fi
	ok "the JSON of an install that failed names no installed file"
}

# --------------------------------------------------------------- test 63
# The forced mark of a file survives its replacement: a file that was
# asked for stays asked for.
test_forced_mark_kept() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool install --force gsp_ga10x.bin >/dev/null 2>&1
	if ! grep -q 'gsp_ga10x.bin.*forced' "${BOOT}/SHA256SUMS"; then
		fail "forced mark: the inventory does not mark the file" \
		    "$(cat "${BOOT}/SHA256SUMS" 2>/dev/null)"
		return
	fi
	# The file is changed: the detect puts it back.
	printf 'tampered\n' > "${BOOT}/gsp_ga10x.bin"
	run_tool detect >/dev/null 2>&1
	if ! grep -q 'gsp_ga10x.bin.*forced' "${BOOT}/SHA256SUMS"; then
		fail "forced mark: the mark is gone after the file was put back" \
		    "$(cat "${BOOT}/SHA256SUMS" 2>/dev/null)"
		return
	fi
	ok "the forced mark of a file survives its replacement"
}

# --------------------------------------------------------------- test 64
# The image is taken out of the release of the driver: the mirror holds
# the release and not the images.
test_fetch_from_release() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	release=NVIDIA-FreeBSD-x86_64-620.73.04
	mkdir -p "${WORK}/release/${release}/firmware"
	cp "${MIRROR}/620.73.04/gsp_ga10x.bin" \
	    "${WORK}/release/${release}/firmware/gsp_ga10x.bin"
	cp "${MIRROR}/620.73.04/ucodes_ga10x.bin" \
	    "${WORK}/release/${release}/firmware/ucodes_ga10x.bin"
	mkdir -p "${ROOT}/release-mirror"
	(cd "${WORK}/release" && tar -cJf \
	    "${ROOT}/release-mirror/${release}.tar.xz" "${release}")
	saved=${MIRROR}
	MIRROR=${ROOT}/release-mirror
	out=$(run_tool fetch 2>&1)
	rc=$?
	MIRROR=${saved}
	if [ ${rc} -ne 0 ]; then
		fail "release: the fetch failed" "exit ${rc}" "${out}"
		return
	fi
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "release: the file was not placed" "${out}"
		return
	fi
	if [ "$(sha256_of "${BOOT}/gsp_ga10x.bin")" != \
	    "$(sha256_of "${MIRROR}/620.73.04/gsp_ga10x.bin")" ]; then
		fail "release: the file that was placed is not the one of the" \
		    "release" "${out}"
		return
	fi
	ok "the image is taken out of the release of the driver"
}

# --------------------------------------------------------------- test 65
# The tar of the base system is the one that unpacks the release: a
# program of somebody else that is called tar is never run.
test_tar_not_from_path() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	release=NVIDIA-FreeBSD-x86_64-620.73.04
	mkdir -p "${WORK}/release/${release}/firmware"
	cp "${MIRROR}/620.73.04/gsp_ga10x.bin" \
	    "${WORK}/release/${release}/firmware/gsp_ga10x.bin"
	mkdir -p "${ROOT}/release-mirror"
	(cd "${WORK}/release" && tar -cJf \
	    "${ROOT}/release-mirror/${release}.tar.xz" "${release}")
	fake=${WORK}/fakebin
	mkdir -p "${fake}"
	cat > "${fake}/tar" <<-'STUB'
	#!/bin/sh
	touch "${NF_TEST_TAR_MARKER}"
	exit 0
	STUB
	chmod +x "${fake}/tar"
	marker=${WORK}/tar-marker
	saved=${MIRROR}
	MIRROR=${ROOT}/release-mirror
	NF_TEST_TAR_MARKER="${marker}"
	export NF_TEST_TAR_MARKER
	out=$(PATH="${fake}:${PATH}" run_tool fetch 2>&1)
	rc=$?
	unset NF_TEST_TAR_MARKER
	MIRROR=${saved}
	if [ -e "${marker}" ]; then
		fail "tar path: the program of somebody else was run" "${out}"
		return
	fi
	if [ ${rc} -ne 0 ]; then
		fail "tar path: the fetch failed" "exit ${rc}" "${out}"
		return
	fi
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "tar path: the file was not placed" "${out}"
		return
	fi
	ok "the tar of the base system is used and not the one of the PATH"
}

# --------------------------------------------------------------- test 66
# A release that holds a member that is not one of the release is
# refused before anything of it is read: the file is not placed and the
# boot directory is as it was.
test_release_member_outside() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	mkdir -p "${WORK}/flat-release"
	cp "${MIRROR}/620.73.04/gsp_ga10x.bin" "${WORK}/flat-release/"
	mkdir -p "${ROOT}/release-mirror"
	(cd "${WORK}/flat-release" && tar -cJf \
	    "${ROOT}/release-mirror/NVIDIA-FreeBSD-x86_64-620.73.04.tar.xz" \
	    gsp_ga10x.bin)
	saved=${MIRROR}
	MIRROR=${ROOT}/release-mirror
	out=$(run_tool fetch 2>&1)
	rc=$?
	MIRROR=${saved}
	if [ ${rc} -eq 0 ]; then
		fail "release member: the release was accepted" "${out}"
		return
	fi
	if [ -e "${BOOT}/gsp_ga10x.bin" ]; then
		fail "release member: the file was placed anyway" "${out}"
		return
	fi
	if [ -e "${BOOT}/SHA256SUMS" ]; then
		fail "release member: the inventory was written anyway" "${out}"
		return
	fi
	ok "a release that holds a member that is not one of it is refused"
}

# --------------------------------------------------------------- test 67
# Every file that is placed is hashed once: a file of thirty or seventy
# megabytes is not read twice.
test_one_hash_pass() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	log=${WORK}/hashes
	: > "${log}"
	NVIDIA_FIRMWARE_TEST_HASHLOG="${log}"
	export NVIDIA_FIRMWARE_TEST_HASHLOG
	run_tool fetch >/dev/null 2>&1
	unset NVIDIA_FIRMWARE_TEST_HASHLOG
	count=$(wc -l < "${log}" | tr -d ' \t')
	# The card needs two files and every file is hashed once.
	if [ "${count}" -ne 2 ]; then
		fail "hash count: ${count} digests for two files" \
		    "$(cat "${log}")"
		return
	fi
	ok "every file that is placed is hashed once"
}

# --------------------------------------------------------------- test 68
# A line of the pin table with more than four fields is refused: the
# table of the package and the table that the README is made of read the
# same lines the same way.
test_pin_table_five_fields() {
	new_root
	printf '620.73.04\tgsp_ga10x.bin\t1\t%064d\textra\n' 0 \
	    > "${ROOT}/share/nvidia-firmware/versions.list"
	out=$(run_tool list --supported 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "pin table: the line with five fields was accepted" "${out}"
		return
	fi
	case "${out}" in
	*"four fields"*) ;;
	*)
		fail "pin table: the reason is not told" "${out}"
		return
		;;
	esac
	ok "a line of the pin table with more than four fields is refused"
}

# --------------------------------------------------------------- test 69
# The boot directory has to be ours: a directory that belongs to somebody
# else can hold a file that is replaced between the check and the
# rename.  The way to the directory has to be safe as well.
test_boot_directory_not_ours() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	mkdir -p "${BOOT}"
	# A directory on the way that others can write in.
	chmod 0777 "${ROOT}/boot/firmware"
	out=$(run_tool fetch 2>&1)
	rc=$?
	chmod 0755 "${ROOT}/boot/firmware"
	if [ ${rc} -eq 0 ]; then
		fail "boot directory: the fetch did not fail" "${out}"
		return
	fi
	if [ -f "${BOOT}/gsp_ga10x.bin" ]; then
		fail "boot directory: the file was placed anyway" "${out}"
		return
	fi
	ok "a boot directory that others can write in on the way is refused"
}

# --------------------------------------------------------------- test 70
# A file that the inventory names and that is not there any more is put
# back: the driver that is running cannot be using a file that is not
# there.
test_fetch_restores_missing_file() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool install --force gsp_ga10x.bin >/dev/null 2>&1
	rm -f "${BOOT}/gsp_ga10x.bin"
	out=$(run_tool fetch 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "fetch restore: the fetch did not restore the file" \
		    "exit ${rc}" "${out}"
		return
	fi
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "fetch restore: the file was not restored" "${out}"
		return
	fi
	ok "a file that the inventory names and that is gone is restored"
}

# --------------------------------------------------------------- test 71
# The expectation of an adapter is kept when the machine has no PCI bus:
# the command would have found nothing at all.
test_expect_card_no_bus() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	saved=${PCIFILE}
	PCIFILE=none
	out=$(run_tool detect --expect-card 2>&1)
	rc=$?
	PCIFILE=${saved}
	if [ ${rc} -eq 0 ]; then
		fail "expect card: the run did not fail without a PCI bus" "${out}"
		return
	fi
	ok "the expectation of an adapter is kept without a PCI bus"
}

# --------------------------------------------------------------- test 72
# The version of the program is not an option of a command: the old form
# of a command that took a driver version is refused, and the option that
# replaces it is named.
test_version_option_after_command() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	out=$(run_tool detect --version 580.178.04 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "version option: the run did not fail" "${out}"
		return
	fi
	case "${out}" in
	*--driver-version*) ;;
	*)
		fail "version option: the option that replaces it is not named" \
		    "${out}"
		return
		;;
	esac
	ok "the version of the program is not an option of a command"
}

# --------------------------------------------------------------- test 73
# The message of the lock says that the command needs the rights of root:
# "Permission denied" alone leaves the reader without an idea.
test_lock_message_names_root() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	# The lock cannot be taken: the path of the lock is a directory.
	mkdir -p "${WORK}/lockdir"
	out=$(env NVIDIA_FIRMWARE_TEST_ROOT="${ROOT}" \
	    NVIDIA_FIRMWARE_TEST_PCI="${PCIFILE}" \
	    NVIDIA_FIRMWARE_TEST_KLD=not \
	    NVIDIA_FIRMWARE_TEST_LOCK="${WORK}/lockdir" \
	    NVIDIA_FIRMWARE_MIRROR="${MIRROR}" \
	    "${BIN}" detect 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "lock message: the run did not fail" "${out}"
		return
	fi
	case "${out}" in
	*root*|*doas*) ;;
	*)
		fail "lock message: the rights that are needed are not named" \
		    "${out}"
		return
		;;
	esac
	ok "the message of the lock says that the command needs root"
}

# --------------------------------------------------------------- test 74
# A bus that cannot be opened is not a bus that is not there: the
# message tells how to get to the bus.
test_pci_denied() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	saved=${PCIFILE}
	PCIFILE=denied
	out=$(run_tool detect 2>&1)
	rc=$?
	PCIFILE=${saved}
	if [ ${rc} -eq 0 ]; then
		fail "pci denied: the run did not fail" "${out}"
		return
	fi
	case "${out}" in
	*"no PCI bus"*)
		fail "pci denied: the bus is reported as one that is not there" \
		    "${out}"
		return
		;;
	esac
	case "${out}" in
	*"/dev/pci"*) ;;
	*)
		fail "pci denied: the way to the bus is not named" "${out}"
		return
		;;
	esac
	ok "a bus that cannot be opened is not a bus that is not there"
}

# --------------------------------------------------------------- test 75
# The work directory holds the sources of the port and nothing else: a
# source that was deleted from the port is not in the work directory any
# more.
test_sync_sources_removes_obsolete() {
	SYNC=${SRCDIR}/../files/sync-sources.sh
	if [ ! -f "${SYNC}" ]; then
		printf '# the script is not there; the test is skipped\n'
		return
	fi
	new_root
	work=${ROOT}/work
	mkdir -p "${work}/src"
	printf 'obsolete\n' > "${work}/src/obsolete.c"
	sh "${SYNC}" "${SRCDIR}/.." "${work}" >/dev/null 2>&1
	if [ -e "${work}/src/obsolete.c" ]; then
		fail "sync sources: the obsolete file is still there"
		return
	fi
	if [ ! -f "${work}/src/nvidia-firmware.c" ]; then
		fail "sync sources: the sources were not copied"
		return
	fi
	ok "the work directory holds the sources of the port and nothing else"
}

# --------------------------------------------------------------- test 76
# The rule of devd calls the tool that the package installs: the path of
# the tool is the path of the prefix the package was built with.
test_devd_rule_prefix() {
	MAKERULE=${SRCDIR}/../files/make-devd-rule.sh
	TEMPLATE=${SRCDIR}/../files/nvidia_firmware.conf
	MAKFILE=${SRCDIR}/../Makefile
	if [ ! -f "${MAKERULE}" ] || [ ! -f "${TEMPLATE}" ]; then
		printf '# the files of the rule are not there; the test is skipped\n'
		return
	fi
	new_root
	# The port does not install the rule as it is: the path of the tool
	# in the rule is the path of the prefix the package was built with.
	if grep -q 'NF_INSTALL_DATA.*nvidia_firmware.conf' "${MAKFILE}"; then
		fail "devd rule: the port installs the rule as it is" \
		    "$(grep -n 'nvidia_firmware.conf' "${MAKFILE}")"
		return
	fi
	out=${ROOT}/nvidia_firmware.conf
	sh "${MAKERULE}" /opt/nvidia-test "${out}" "${TEMPLATE}" >/dev/null 2>&1
	if ! grep -q 'call "/opt/nvidia-test/sbin/nvidia-firmware fetch"' \
	    "${out}"; then
		fail "devd rule: the rule does not call the tool of the prefix" \
		    "$(cat "${out}" 2>/dev/null)"
		return
	fi
	if grep -q '@PREFIX@' "${out}"; then
		fail "devd rule: the rule holds the placeholder" \
		    "$(cat "${out}" 2>/dev/null)"
		return
	fi
	ok "the rule of devd calls the tool of the prefix of the package"
}

# --------------------------------------------------------------- test 77
# The synopsis of every command that fetches names the option that says
# where the files come from.
test_manpage_from_option() {
	new_case
	for cmd in detect fetch install; do
		if ! awk -v cmd="${cmd}" '
			/^\.Sh SYNOPSIS/ { insyn = 1; next }
			/^\.Sh / { insyn = 0 }
			insyn && /^\.Cm / { in_cmd = ($2 == cmd) }
			insyn && in_cmd && /Fl[ \t]+-from/ { found = 1 }
			END { exit !found }' "${MANPAGE}"; then
			fail "man page: the synopsis of ${cmd} does not name" \
			    "--from"
			return
		fi
	done
	ok "the synopsis of every command that fetches names --from"
}

# --------------------------------------------------------------- test 78
# The prefix of the rule of devd is a path and nothing else.  A prefix
# that holds a quote or a newline would end the string of the call of the
# rule and would put a call of its own into the file, and devd(8) runs
# the calls of a rule as root.  The generator refuses such a prefix.
test_devd_rule_injection() {
	MAKERULE=${SRCDIR}/../files/make-devd-rule.sh
	TEMPLATE=${SRCDIR}/../files/nvidia_firmware.conf
	if [ ! -f "${MAKERULE}" ] || [ ! -f "${TEMPLATE}" ]; then
		printf '# the files of the rule are not there; the test is skipped\n'
		return
	fi
	new_root
	marker=${WORK}/nf-pwn
	out=${ROOT}/nvidia_firmware.conf
	# The prefix ends the string of the call and adds a call of its own.
	payload='/safe"
	}
	attach "evil" {
	    call "/bin/sh -c "touch '"${marker}"'""
	}
	attach "tail" {
	    call "/dummy'
	sh "${MAKERULE}" "${payload}" "${out}" "${TEMPLATE}" >/dev/null 2>&1
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "devd rule: a prefix that is not a path was accepted" \
		    "$(cat "${out}" 2>/dev/null)"
		return
	fi
	if [ -e "${marker}" ]; then
		fail "devd rule: the prefix of the caller was run"
		return
	fi
	if [ -f "${out}" ] && grep -q 'nf-pwn' "${out}"; then
		fail "devd rule: the rule holds a call of the caller" \
		    "$(cat "${out}")"
		return
	fi
	ok "the rule of devd refuses a prefix that is not a path"
}

# --------------------------------------------------------------- test 79
# Every placeholder of a line is the prefix: a line that holds it twice
# is written out with the prefix in both places.
test_devd_rule_two_placeholders() {
	MAKERULE=${SRCDIR}/../files/make-devd-rule.sh
	if [ ! -f "${MAKERULE}" ]; then
		printf '# the files of the rule are not there; the test is skipped\n'
		return
	fi
	new_root
	template=${ROOT}/two.conf
	out=${ROOT}/two-out.conf
	printf 'call "@PREFIX@/one @PREFIX@/two"\n' > "${template}"
	sh "${MAKERULE}" /opt/test "${out}" "${template}" >/dev/null 2>&1
	if ! grep -qF 'call "/opt/test/one /opt/test/two"' "${out}"; then
		fail "devd rule: the second placeholder was not replaced" \
		    "$(cat "${out}" 2>/dev/null)"
		return
	fi
	ok "every placeholder of the rule of devd is the prefix"
}

# --------------------------------------------------------------- test 80
# The directory of the pin table is a value of the caller.  It is not
# written into the text of a command of the compiler: it is written into
# a header with the escaping that a C string needs, so that a path that
# holds a quote or a semicolon is data and never shell syntax.
test_paths_header() {
	GENERATOR=${SRCDIR}/embed-paths.sh
	new_root
	# The path is not written into the text of a command of the
	# compiler: the compiler is given the header.
	if grep -q 'CFLAGS.*NF_DATA_DIR' "${SRCDIR}/Makefile"; then
		fail "paths: the path is in the arguments of the compiler" \
		    "$(grep -n 'NF_DATA_DIR' "${SRCDIR}/Makefile")"
		return
	fi
	if [ ! -f "${GENERATOR}" ]; then
		printf '# the generator of the paths is not there; the test is skipped\n'
		return
	fi
	marker=${WORK}/nf-pwn
	out=${ROOT}/nf_paths.h
	payload="/x'; touch ${marker}; #"
	NF_DATA_PATH="${payload}" sh "${GENERATOR}" "${out}" >/dev/null 2>&1
	rc=$?
	if [ -e "${marker}" ]; then
		fail "paths: the path of the caller was run"
		return
	fi
	if [ ${rc} -ne 0 ]; then
		fail "paths: the generator refused the path of the caller" \
		    "$(cat "${out}" 2>/dev/null)"
		return
	fi
	if ! grep -qF "#define NF_DATA_DIR \"${payload}\"" "${out}"; then
		fail "paths: the header does not hold the path of the caller" \
		    "$(cat "${out}" 2>/dev/null)"
		return
	fi
	ok "the path of the data is written into a header and not into a command"
}

# --------------------------------------------------------------- test 81
# The size of an image is the size of the pin, and the limit is enforced
# while the file is written: a file that is much bigger than the pin
# cannot fill the disk of the machine.  The limit of the test run is
# small, so a copy that is not bounded would be stopped by the system
# instead of by the tool.
test_size_limit() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	# The file of the mirror is much bigger than the pin.
	dd if=/dev/zero of="${MIRROR}/620.73.04/gsp_ga10x.bin" bs=1024 \
	    count=1024 2>/dev/null
	out=$( (ulimit -f 64; run_tool install --all) 2>&1 )
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "size limit: the install of a file that is too big worked" \
		    "${out}"
		return
	fi
	if [ ${rc} -gt 128 ]; then
		fail "size limit: the copy was not bounded by the tool" \
		    "exit ${rc}" "${out}"
		return
	fi
	if [ -e "${BOOT}/gsp_ga10x.bin" ]; then
		fail "size limit: the file that is too big was put in place" \
		    "${out}"
		return
	fi
	ok "the copy of an image stops at the size of the pin"
}

# --------------------------------------------------------------- test 82
# The report of a dry run that succeeds names the files it would place
# and no file that failed: a dry run places nothing, so it has no file
# that failed.
test_install_dry_run_json() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	out=$(run_tool install --all -n --json 2>/dev/null)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "dry run json: the dry run failed" "exit ${rc}" "${out}"
		return
	fi
	if ! printf '%s' "${out}" | python3 -I -c '
import json, sys
doc = json.load(sys.stdin)
node = doc.get("install", doc)
would = [i.get("name") for i in node.get("would_install", [])]
failed = node.get("failed", [])
sys.exit(0 if "gsp_ga10x.bin" in would and not failed else 1)
' >/dev/null 2>&1; then
		fail "dry run json: the report names a file that failed" "${out}"
		return
	fi
	out=$(run_tool install --all -n 2>&1)
	if printf '%s\n' "${out}" | grep -q '^not installed: '; then
		fail "dry run json: the text of the dry run names a file that" \
		    "was not installed" "${out}"
		return
	fi
	ok "the report of a dry run names no file that failed"
}

rm -rf "${WORK}"
mkdir -p "${WORK}"

test_two_families
test_cards_gone
test_empty_directory_removed
test_no_delete
test_rc_no_delete
test_expect_card
test_forced
test_verify
test_bad_hash
test_interrupted_copy
test_version_pin
test_version_pin_fallback
test_clean
test_json
test_list_cards_without_families
test_remove_foreign
test_remove_in_use
test_remove_warns
test_manpage
test_detect_restores_changed
test_detect_symlink
test_temp_names
test_inventory_traversal
test_module_guard
test_missing_required_image
test_verify_changed_file
test_empty_family_table
test_missing_boot_inventory
test_version_of_the_pin_table
test_pkg_install
test_manpage_options
test_install_without_families
test_family_mapping
test_fetch_from_mirror
test_fetch_hash_mismatch
test_fetch_from_option
test_fetch_offline
test_integrity_before_change
test_foreign_not_overwritten
test_verify_symlink
test_list_missing
test_cleanup_keeps_inventory
test_clean_json
test_pkg_scripts
test_rc_last_value
test_version_of_the_program
test_remove_failure
test_package_layout
test_dry_run
test_keep_versions_and_all
test_version_and_status
test_rc_unreadable
test_boot_unreadable
test_lock
test_no_pci_bus
test_inventory_after_partial_failure
test_family_alloc_fail
test_boot_directory_private
test_list_supported
test_install_foreign_file
test_install_dry_run
test_install_json_failure
test_forced_mark_kept
test_fetch_from_release
test_tar_not_from_path
test_release_member_outside
test_one_hash_pass
test_pin_table_five_fields
test_boot_directory_not_ours
test_fetch_restores_missing_file
test_expect_card_no_bus
test_version_option_after_command
test_lock_message_names_root
test_pci_denied
test_sync_sources_removes_obsolete
test_devd_rule_prefix
test_manpage_from_option
test_devd_rule_injection
test_devd_rule_two_placeholders
test_paths_header
test_size_limit
test_install_dry_run_json

printf '1..%d\n' "$((PASS + FAIL))"
printf '# passed %d, failed %d\n' "$PASS" "$FAIL"
[ "${FAIL}" -eq 0 ]
