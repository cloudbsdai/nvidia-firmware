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
# The cross check of the family table needs the supported-gpus.json file of
# the driver releases; the paths are taken from ${NF_TEST_GPU_JSON}.

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

# A fake share directory with two driver versions.
make_share() {
	_share=$1
	mkdir -p "${_share}/580.178.04" "${_share}/620.73.04"
	cp "${FAMILIES}" "${_share}/nvidia-firmware.families"
	for v in 580.178.04 620.73.04; do
		for i in gsp_tu10x gsp_ga10x; do
			printf 'firmware %s %s\n' "$v" "$i" \
			    > "${_share}/${v}/${i}.bin"
		done
		: > "${_share}/${v}/SHA256SUMS"
		cat > "${_share}/${v}/supported-gpus.json" <<-'JSON'
		{"chips":[{"devid":"0x1E04","name":"NVIDIA GeForce RTX 2080 Ti"},
		{"devid":"0x2204","name":"NVIDIA GeForce RTX 3090"},
		{"devid":"0x2684","name":"NVIDIA GeForce RTX 4090"},
		{"devid":"0x2B85","name":"NVIDIA GeForce RTX 5090"},
		{"devid":"0x2D04","name":"NVIDIA GeForce RTX 5060 Ti"}]}
		JSON
	done
	for i in ucodes_tu10x ucodes_ga10x; do
		printf 'firmware 620.73.04 %s\n' "$i" \
		    > "${_share}/620.73.04/${i}.bin"
	done
	for v in 580.178.04 620.73.04; do
		: > "${_share}/${v}/SHA256SUMS"
		for f in "${_share}/${v}"/*.bin; do
			printf '%s  %s\n' "$(sha256_of "$f")" \
			    "$(basename "$f")" >> "${_share}/${v}/SHA256SUMS"
		done
	done
}

# A fresh case: a root with the fake share and a fake PCI bus.
new_case() {
	CASE=$((CASE + 1))
}

new_root() {
	new_case
	ROOT=${WORK}/case-${CASE}
	rm -rf "${ROOT}"
	mkdir -p "${ROOT}/share" "${ROOT}/boot/firmware" "${ROOT}/etc/rc.conf.d"
	make_share "${ROOT}/share/nvidia-firmware"
	PCIFILE=${ROOT}/pci
	BOOT=${ROOT}/boot/firmware/nvidia
	: > "${PCIFILE}"
	unset NVIDIA_FIRMWARE_TEST_KLD NVIDIA_FIRMWARE_TEST_PIN
}

# A fresh case without the share directory: the firmware is not installed.
new_root_without_share() {
	new_case
	ROOT=${WORK}/case-${CASE}
	rm -rf "${ROOT}"
	mkdir -p "${ROOT}/boot/firmware" "${ROOT}/etc/rc.conf.d"
	PCIFILE=${ROOT}/pci
	BOOT=${ROOT}/boot/firmware/nvidia
	: > "${PCIFILE}"
	unset NVIDIA_FIRMWARE_TEST_KLD NVIDIA_FIRMWARE_TEST_PIN
}

run_tool() {
	env NVIDIA_FIRMWARE_TEST_ROOT="${ROOT}" \
	    NVIDIA_FIRMWARE_TEST_PCI="${PCIFILE}" \
	    NVIDIA_FIRMWARE_TEST_KLD="${NVIDIA_FIRMWARE_TEST_KLD:-not}" \
	    NVIDIA_FIRMWARE_TEST_PIN="${NVIDIA_FIRMWARE_TEST_PIN:-}" \
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
	count=$(ls -1 "${BOOT}" | grep -c '\.bin$')
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
	printf 'tampered\n' > "${ROOT}/share/nvidia-firmware/620.73.04/gsp_ga10x.bin"
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
# A pin to a version that is not installed falls back to the newest.
test_version_pin_fallback() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	NVIDIA_FIRMWARE_TEST_PIN=999.99.99 run_tool detect >/dev/null 2>&1
	if ! exists "${BOOT}/ucodes_ga10x.bin"; then
		fail "version pin: the newest version was not used"
		return
	fi
	ok "a pin to a version that is not installed falls back"
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
# The cards are listed when the firmware is not installed, and the family
# of a card is the same with and without the share directory.
test_list_cards_without_share() {
	new_root_without_share
	printf '0x2204 pci0:1:0:0\n' > "${PCIFILE}"
	out=$(run_tool list --cards 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "list --cards: the exit code is ${rc}" "${out}"
		return
	fi
	for want in 'pci0:1:0:0' '0x2204' 'GA10x' 'not installed'; do
		case "${out}" in
		*"${want}"*) ;;
		*)
			fail "list --cards: ${want} is not in the output" "${out}"
			return
			;;
		esac
	done
	#
	# The table of the share directory says the same as the table that is
	# compiled into the program.
	#
	make_share "${ROOT}/share/nvidia-firmware"
	out2=$(run_tool list --cards 2>&1)
	family1=$(printf '%s' "${out}" | sed -n 's/.*family=\([^ ]*\).*/\1/p')
	family2=$(printf '%s' "${out2}" | sed -n 's/.*family=\([^ ]*\).*/\1/p')
	if [ -z "${family1}" ] || [ "${family1}" != "${family2}" ]; then
		fail "list --cards: the family is ${family1} without the table of the share directory and ${family2} with it" \
		    "${out}" "${out2}"
		return
	fi
	ok "the cards are listed when the firmware is not installed"
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

# --------------------------------------------------------------- test 20
# The family table agrees with the supported-gpus.json of the releases.
#
# The family of a chip is expected from its name:
#
#	Ada				AD10x
#	Blackwell, RTX PRO, RTX Spark	GB20x
#	Quadro RTX, TITAN RTX		TU10x
#	GeForce RTX 20xx		TU10x
#	GeForce RTX 30xx		GA10x
#	RTX A-series			GA10x
#	GeForce RTX 40xx, RTX 3050 A	AD10x
#	GeForce RTX 50xx		GB20x
test_family_table() {
	: "${NF_TEST_GPU_JSON:?the paths of supported-gpus.json are needed}"
	new_case
	out=$(NF_TEST_UNMATCHED="${WORK}/family-unmatched.txt" \
	    python3 -I "${SRCDIR}/tests/family-check.py" "${FAMILIES}" \
	    ${NF_TEST_GPU_JSON} 2>&1)
	rc=$?
	printf '%s\n' "${out}" | sed 's/^/# /'
	if [ ${rc} -ne 0 ]; then
		fail "family table: the table does not agree with the releases"
		return
	fi
	printf '# the devices in no range are in %s\n' \
	    "${WORK}/family-unmatched.txt"
	ok "the family table agrees with the supported-gpus.json of the releases"
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
	    "$(sha256_of "${ROOT}/share/nvidia-firmware/620.73.04/gsp_ga10x.bin")" ]; then
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
# An image the cards need but that the version does not hold is an error
# and is named; the boot directory is left alone.
test_missing_required_image() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	rm -f "${ROOT}/share/nvidia-firmware/620.73.04/gsp_ga10x.bin"
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
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "missing image: the image of the boot directory was taken away"
		return
	fi
	ok "an image the cards need but that is missing is an error"
}

# --------------------------------------------------------------- test 27
# verify reports an image of the share directory that is missing.
test_verify_missing_share_image() {
	new_root
	rm -f "${ROOT}/share/nvidia-firmware/620.73.04/gsp_ga10x.bin"
	out=$(run_tool verify 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "verify missing: verify did not fail" "${out}"
		return
	fi
	case "${out}" in
	*gsp_ga10x.bin*) ;;
	*)
		fail "verify missing: the image is not named" "${out}"
		return
		;;
	esac
	ok "verify reports an image of the share directory that is missing"
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
# Only a directory that is a driver version counts as one.
test_version_directory() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	mkdir -p "${ROOT}/share/nvidia-firmware/999.0.0"
	run_tool detect >/dev/null 2>&1
	out=$(run_tool detect 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "version directory: detect failed" "exit ${rc}" "${out}"
		return
	fi
	case "${out}" in
	*"version: 620.73.04"*) ;;
	*)
		fail "version directory: the wrong version was chosen" "${out}"
		return
		;;
	esac
	if ! exists "${BOOT}/gsp_ga10x.bin"; then
		fail "version directory: the image was taken away"
		return
	fi
	ok "only a directory that is a driver version counts as one"
}

# --------------------------------------------------------------- test 31
# The package script fails when the firmware is not placed.
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
	exit 1
	STUB
	chmod +x "${ROOT}/prefix/sbin/nvidia-firmware"
	out=$(PREFIX="${ROOT}/prefix" sh "${PKGINSTALL}" nvidia-firmware \
	    POST-INSTALL 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "pkg-install: the script did not fail" "${out}"
		return
	fi
	case "${out}" in
	*nvidia-firmware*) ;;
	*)
		fail "pkg-install: the reason is not told" "${out}"
		return
		;;
	esac
	ok "the package script fails when the firmware is not placed"
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
				if (r != "")
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
	manpage_options | sort -u > "${WORK}/man-options"
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
		--force|--version|--libxo) extra=x ;;
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
		if [ ! -f "${ROOT}/share/nvidia-firmware/${version}/${image}" ]; then
			fail "family mapping: ${version}/${image} is not in the share directory"
			return
		fi
	done < "${WORK}/family-images"
	printf '0x2204\n' > "${PCIFILE}"
	run_tool detect >/dev/null 2>&1
	got=$(ls -1 "${BOOT}" | grep 'bin$' | sort |
	    awk '{ printf "%s%s", sep, $0; sep = "," }')
	if [ "${got}" != "gsp_ga10x.bin,ucodes_ga10x.bin" ]; then
		fail "family mapping: the GA10x card got ${got}"
		return
	fi
	ok "the family table names the images that are placed for the family"
}

# --------------------------------------------------------------- test 35
# The image that is installed is the image that was checked against the
# pin: an image that does not match the pin fails the build, and nothing
# of it is left behind.
test_install_firmware() {
	SCRIPT=${SRCDIR}/../files/install-firmware.sh
	if [ ! -f "${SCRIPT}" ]; then
		printf '# the install script is not there; the test is skipped\n'
		return
	fi
	new_root
	TREE=${ROOT}/tree
	STAGE=${ROOT}/stage
	VERSION=620.73.04
	IMAGE=gsp_ga10x.bin
	mkdir -p "${TREE}/${VERSION}/doc/supported-gpus" "${STAGE}"
	printf 'the pinned firmware\n' > "${TREE}/${VERSION}/${IMAGE}"
	printf '{}\n' > "${TREE}/${VERSION}/doc/supported-gpus/supported-gpus.json"
	TABLE=${ROOT}/versions.list
	SIZE=$(stat -f %z "${TREE}/${VERSION}/${IMAGE}")
	SHA=$(sha256_of "${TREE}/${VERSION}/${IMAGE}")
	printf '%s  %s  %s  %s\n' "${VERSION}" "${IMAGE}" "${SIZE}" "${SHA}" \
	    > "${TABLE}"
	out=$(sh "${SCRIPT}" "${STAGE}" "/share" "${TREE}" "${TABLE}" \
	    "${FAMILIES}" 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "install firmware: the pinned image was not installed" \
		    "exit ${rc}" "${out}"
		return
	fi
	if [ ! -f "${STAGE}/share/${VERSION}/${IMAGE}" ]; then
		fail "install firmware: the image was not staged"
		return
	fi
	if [ "$(sha256_of "${STAGE}/share/${VERSION}/${IMAGE}")" != "${SHA}" ]; then
		fail "install firmware: the staged image is not the pinned one"
		return
	fi
	if ! grep -q "^${SHA}  ${IMAGE}\$" \
	    "${STAGE}/share/${VERSION}/SHA256SUMS"; then
		fail "install firmware: the file of hashes does not name the pinned hash"
		return
	fi
	# The image on the disk is not the pinned one any more.
	printf 'a different firmware\n' > "${TREE}/${VERSION}/${IMAGE}"
	rm -f "${STAGE}/share/${VERSION}/${IMAGE}"
	out=$(sh "${SCRIPT}" "${STAGE}" "/share" "${TREE}" "${TABLE}" \
	    "${FAMILIES}" 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "install firmware: the build did not fail" "${out}"
		return
	fi
	if [ -f "${STAGE}/share/${VERSION}/${IMAGE}" ]; then
		fail "install firmware: the image that does not match the pin was installed"
		return
	fi
	if [ -f "${STAGE}/share/${VERSION}/SHA256SUMS" ] &&
	    grep -q "${IMAGE}" "${STAGE}/share/${VERSION}/SHA256SUMS"; then
		fail "install firmware: the image that does not match the pin is named"
		return
	fi
	ok "the image that is installed is the image that was checked"
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
# Every source is checked before the boot directory is changed.  A check
# that fails leaves the boot directory as it was, and the dry run fails in
# the same way as the run that follows it.
test_integrity_before_change() {
	new_root
	printf '0x2204\n' > "${PCIFILE}"
	# The second image of the card is the one that is broken: the file
	# is not the file of the version any more.
	printf 'tampered\n' \
	    > "${ROOT}/share/nvidia-firmware/620.73.04/ucodes_ga10x.bin"
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
	case "${out}" in
	*ucodes_ga10x.bin*) ;;
	*)
		fail "integrity: the dry run does not name the image" "${out}"
		return
		;;
	esac
	if [ -e "${BOOT}/gsp_ga10x.bin" ] || [ -e "${BOOT}/SHA256SUMS" ]; then
		fail "integrity: the dry run changed the boot directory"
		return
	fi
	# The image that is whole again makes both runs work.
	printf 'firmware 620.73.04 ucodes_ga10x\n' \
	    > "${ROOT}/share/nvidia-firmware/620.73.04/ucodes_ga10x.bin"
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
	ok "every source is checked before the boot directory is changed"
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
	if ! grep -q '^detect$' "${NF_TEST_STUB_LOG}"; then
		fail "pkg scripts: the install script did not run detect" \
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
# The chip list is read whatever order the members are in.
test_json_member_order() {
	new_root
	printf '0x2204 pci0:1:0:0\n' > "${PCIFILE}"
	for v in 580.178.04 620.73.04; do
		cat > "${ROOT}/share/nvidia-firmware/${v}/supported-gpus.json" <<-'JSON'
		{"chips":[{"name" : "NVIDIA GeForce RTX 3090","devid":"0x2204"},
		{"devid":"0x2684","name":"NVIDIA GeForce RTX 4090"}]}
		JSON
	done
	out=$(run_tool list --cards 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "member order: list --cards failed" "exit ${rc}" "${out}"
		return
	fi
	case "${out}" in
	*"RTX 3090"*) ;;
	*)
		fail "member order: the name of the chip is not in the output" "${out}"
		return
		;;
	esac
	ok "the chip list is read whatever order the members are in"
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
# The package holds the firmware of every version of the pin table, in
# the one directory that the manual page and the tool name.
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
	share=$(awk -F= '/^FW_SHARE_DIR=/ { print $2 }' "${MAKEFILE}" | \
	    tr -d ' \t')
	if [ "${share}" != '${DATADIR}' ]; then
		fail "package layout: the share directory of the port is ${share}" \
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
	if ! grep -q 'versions.list' "${MAKEFILE}"; then
		fail "package layout: the versions of the port are not made from" \
		    "the pin table"
		return
	fi
	for v in $(awk '{ if ($1 !~ /^#/) print $1 }' "${TABLE}" | sort -u); do
		for f in SHA256SUMS supported-gpus.json; do
			if ! grep -q "^%%DATADIR%%/${v}/${f}\$" "${PLIST}"; then
				fail "package layout: the plist does not name" \
				    "${v}/${f}"
				return
			fi
		done
		for image in $(awk -v v="${v}" \
		    '{ if ($1 == v) print $2 }' "${TABLE}"); do
			if ! grep -q "^%%DATADIR%%/${v}/${image}\$" "${PLIST}"; then
				fail "package layout: the plist does not name" \
				    "${v}/${image}"
				return
			fi
		done
	done
	# Every image the table of the families names is in the pin table.
	for image in $(awk '$1 == "images" { for (i = 4; i <= NF; i++) print $i }' \
	    "${FAMILIES}" | sort -u); do
		if ! awk -v i="${image}" \
		    '{ if ($2 == i) found = 1 } END { exit !found }' "${TABLE}"; then
			fail "package layout: ${image} is not in the pin table"
			return
		fi
	done
	ok "the package holds the firmware of the pin table in one directory"
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
	out=$(run_tool install --all --version 580.178.04 2>&1)
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
	out=$(run_tool install --all --version 999.99.99 2>&1)
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
test_list_cards_without_share
test_remove_foreign
test_remove_in_use
test_remove_warns
test_manpage
if [ -n "${NF_TEST_GPU_JSON:-}" ]; then
	test_family_table
else
	printf '# the family table check needs NF_TEST_GPU_JSON\n'
fi
test_detect_restores_changed
test_detect_symlink
test_temp_names
test_inventory_traversal
test_module_guard
test_missing_required_image
test_verify_missing_share_image
test_empty_family_table
test_missing_boot_inventory
test_version_directory
test_pkg_install
test_manpage_options
test_install_without_families
test_family_mapping
test_install_firmware
test_integrity_before_change
test_foreign_not_overwritten
test_verify_symlink
test_list_missing
test_cleanup_keeps_inventory
test_clean_json
test_pkg_scripts
test_rc_last_value
test_json_member_order
test_remove_failure
test_package_layout
test_dry_run
test_keep_versions_and_all
test_version_and_status

printf '1..%d\n' "$((PASS + FAIL))"
printf '# passed %d, failed %d\n' "$PASS" "$FAIL"
[ "${FAIL}" -eq 0 ]
