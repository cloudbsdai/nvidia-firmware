#!/bin/sh
#
# The files that are shipped or that the public reads must not name the
# work of the project or the machines of the project.
#
# The package is ours and the repository is public: a number of a work
# package, a name of a host or a path of a home directory are ours and
# are not of the reader.  The lint looks for them and fails when it
# finds one.
#
# The lint looks at:
#
#   - the number of a work package: "Track #1234" and every "#1234";
#   - a path of a home directory;
#   - the name of a machine of the project;
#   - the words of ours that the reader does not need: the fleet, the
#     repositories that only we can open and the name of another system of
#     ours;
#   - every address of the copy: it has to name a host of the public, so
#     that no link points at a place where the reader cannot go.
#
# The names of the machines of the project are not in this file: they are
# in a file that is not in the repository, and NF_INTERNAL_NAMES_FILE
# names that file.  A checkout without that file checks the two rules
# above that do not need it; the CI runs with the file and checks the
# names of the machines as well.
#
# It also looks at the version of the package: the version is ours and it
# is the date of the day the package was made.  A version of NVIDIA can
# never be the version of the package.
#
# Everything happens in a directory of its own below ${TMPDIR}; when
# TMPDIR is not set, the directory is made below /var/tmp.  Nothing
# outside of it is touched, and it is taken away when the lint is over.

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PORT=${ROOT}/sysutils/nvidia-firmware

# A directory of its own for every run: two runs of the lint do not
# share one, and neither of them takes the workspace of the other away.
WORK=$(mktemp -d "${TMPDIR:-/var/tmp}/nvidia-firmware-docs-test.XXXXXX") || {
	printf 'Bail out! cannot make a workspace for the lint\n' >&2
	exit 1
}
trap 'rm -rf "${WORK}"' EXIT INT TERM

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

new_case() {
	CASE=$((CASE + 1))
}

# The files that are shipped with the package or that the public reads.
# The list is read a line at a time and every line is quoted: a path that
# holds a space is data and never two paths.
shipped_list() {
	cat <<-LIST
	${ROOT}/README.md
	${PORT}/Makefile
	${PORT}/pkg-descr
	${PORT}/pkg-message
	${PORT}/pkg-install
	${PORT}/pkg-deinstall
	${PORT}/pkg-plist
	${PORT}/files/versions.list
	${PORT}/files/nvidia_firmware.conf
	${PORT}/src/nvidia-firmware.8
	${PORT}/src/nvidia-firmware.c
	${PORT}/src/nvidia-firmware.families
	${PORT}/src/nf_boot.c
	${PORT}/src/nf_boot.h
	${PORT}/src/nf_cards.c
	${PORT}/src/nf_cards.h
	${PORT}/src/nf_families.c
	${PORT}/src/nf_families.h
	${PORT}/src/nf_fetch.c
	${PORT}/src/nf_fetch.h
	${PORT}/src/nf_pin.c
	${PORT}/src/nf_pin.h
	${PORT}/src/nf_rc.c
	${PORT}/src/nf_rc.h
	${PORT}/src/nf_util.c
	${PORT}/src/nf_util.h
	LIST
}

shipped_list > "${WORK}/shipped"

# A copy of the tree in a directory of its own.  The copy holds the tests,
# the Makefile of the tree and every file that is shipped, so that the
# lint can be run in it.  One function and not two: a file that the lint
# needs is added in one place and not forgotten in the other copy.
copy_tree() {
	dest=$1
	mkdir -p "${dest}/tests"
	cp "${ROOT}/tests/docs-test.sh" "${ROOT}/tests/gen-supported-table.py" \
	    "${dest}/tests/"
	cp "${ROOT}/Makefile" "${dest}/Makefile"
	while IFS= read -r file; do
		[ -f "${file}" ] || continue
		rel=${file#"${ROOT}/"}
		mkdir -p "${dest}/$(dirname "${rel}")"
		cp "${file}" "${dest}/${rel}"
	done < "${WORK}/shipped"
}

# The words of ours that the copy the public reads must not hold: the
# fleet of machines, the repositories that only we can open and the name
# of another system of ours.  "CloudBSDFleet" is a "cloudbsd" with a
# capital letter, so one pattern refuses both.  The words are matched
# without regard to the case: a word that is written in another case is
# the same word.  The word "fleet" is matched as a word and not as a
# part of one: a word of the reader that holds it is not a word of ours.
INTERNAL_WORD_PATTERN='cloudbsd|internalpkg|internal repositor|(^|[^[:alnum:]_])fleet([^[:alnum:]_]|$)'

# The hosts that the copy may name: the download server of NVIDIA and
# the project of the sources.  A host that is a subdomain of one of them
# is the same host for this check.  Every other host is a place where the
# reader cannot go, or a place that is ours.
PUBLIC_HOSTS='nvidia.com github.com opensource.org fsf.org spdx.org'

# The hosts of the addresses of one file, one per line.  An address is a
# URL with a scheme, or the path of a repository of git.  The path of a
# repository of git is written "git@host:path": a mail address that is
# followed by a colon is not one, and is not read as one.
#
# Every scheme is read, and not a list of the schemes that are known: a
# scheme that is not in the list is a place the reader cannot go to as
# well.  A scheme without an authority, such as a mail address, has no
# host to name and is not read as one.  An address without a scheme is
# not an address here: it is a word of the text, and the text of a
# sentence is not a link.
#
# The copy is written in Markdown: an address is often in a backtick and
# ends a sentence with a dot.  The backtick is not a part of the address
# and the dot is not a part of the host, so neither is read as one.
link_hosts() {
	grep -ohE '[A-Za-z][A-Za-z0-9+.-]*://[^[:space:]"<>)]+|git@[A-Za-z0-9.-]+:[^[:space:]"<>)]+' \
	    "$1" 2>/dev/null | while IFS= read -r addr; do
		case "${addr}" in
		*://*)
			rest=${addr#*://}
			host=${rest%%/*}
			host=${host%%\?*}
			host=${host%%\#*}
			host=${host%%:*}
			;;
		*@*)
			host=${addr#*@}
			host=${host%%:*}
			;;
		*)
			continue
			;;
		esac
		host=$(printf '%s' "${host}" | sed -e 's/[.,;:`]*$//')
		[ -n "${host}" ] || continue
		printf '%s\n' "${host}" | tr 'A-Z' 'a-z'
	done
}

# Is the host one that the reader can go to?
host_is_public() {
	for allowed in ${PUBLIC_HOSTS}; do
		case "$1" in
		"${allowed}"|*."${allowed}") return 0 ;;
		esac
	done
	return 1
}

# The names of the machines of the project.  They are in a file that is
# not in the repository: one pattern per line.
NAMES_FILE=${NF_INTERNAL_NAMES_FILE:-${ROOT}/tests/internal-names.local}

# ---------------------------------------------------------------- test 1
# No number of a work package and no path of a home directory in the
# files that are shipped or that the public reads.
#
# The pattern of a number of a work package is a "#" and four digits: the
# number of a work package is written that way in every place it is
# written.
new_case
offenders=
while IFS= read -r file; do
	[ -f "${file}" ] || continue
	found=$(grep -nE '#[0-9]{4}|/home/|Track #|track #' "${file}" 2>/dev/null)
	[ -n "${found}" ] || continue
	offenders="${offenders}${file}:
${found}
"
done < "${WORK}/shipped"
if [ -n "${offenders}" ]; then
	fail "no internal id: a shipped file names an internal id" "${offenders}"
else
	ok "no shipped file names a work package or a home directory"
fi

# ---------------------------------------------------------------- test 2
# The name of a machine of the project is not in the files that are
# shipped or that the public reads.
new_case
if [ ! -f "${NAMES_FILE}" ]; then
	printf '# the names of the machines are in %s; the check is skipped\n' \
	    "${NAMES_FILE}"
elif [ ! -s "${NAMES_FILE}" ]; then
	fail "no internal host: the file of the names of the machines is empty" \
	    "${NAMES_FILE}"
else
	offenders=
	while IFS= read -r file; do
		[ -f "${file}" ] || continue
		found=$(grep -nE -f "${NAMES_FILE}" "${file}" 2>/dev/null)
		[ -n "${found}" ] || continue
		offenders="${offenders}${file}:
${found}
"
	done < "${WORK}/shipped"
	if [ -n "${offenders}" ]; then
		fail "no internal host: a shipped file names a machine of the" \
		    "project" "${offenders}"
	else
		ok "no shipped file names a machine of the project"
	fi
fi

# ---------------------------------------------------------------- test 3
# The version of the package is ours and it is the date of the day the
# package was made.  A version of NVIDIA is data and is never the
# version of the package.
new_case
DISTVERSION=$(awk -F= '/^DISTVERSION=/ { gsub(/[ \t]/, "", $2); print $2 }' \
    "${PORT}/Makefile")
if [ -z "${DISTVERSION}" ]; then
	fail "our version: the port has no DISTVERSION"
else
	case "${DISTVERSION}" in
	[0-9][0-9][0-9][0-9].[0-9][0-9].[0-9][0-9]*)
		;;
	*)
		fail "our version: the version of the package is ${DISTVERSION}" \
		    "and is not a date version"
		DISTVERSION=
		;;
	esac
fi
if [ -n "${DISTVERSION}" ]; then
	driver=$(awk '{ if ($1 !~ /^#/) print $1 }' \
	    "${PORT}/files/versions.list" | sort -u)
	clash=
	for v in ${driver}; do
		if [ "${v}" = "${DISTVERSION}" ]; then
			clash="${v}"
			break
		fi
	done
	if [ -n "${clash}" ]; then
		fail "our version: the version of the package is the version of" \
		    "a driver (${clash})"
	else
		ok "the version of the package is a date version and is not a" \
		    "version of a driver"
	fi
fi

# ---------------------------------------------------------------- test 4
# The program carries the version of the package.  The port gives its own
# DISTVERSION to the build, so that the two are always the same.
new_case
if [ -z "${DISTVERSION}" ]; then
	printf '# the version of the package is not known; the test is skipped\n'
elif ! grep -q 'NF_VERSION=\$\$NF_DISTVERSION' "${PORT}/Makefile"; then
	fail "our version: the port does not give its own version to the" \
	    "program" "$(grep -n 'NF_VERSION' "${PORT}/Makefile")"
else
	BIN=${PORT}/src/nvidia-firmware
	if [ ! -x "${BIN}" ]; then
		ok "the port gives its own version to the program"
	else
		out=$("${BIN}" --version 2>&1)
		case "${out}" in
		*"${DISTVERSION}"*)
			ok "the program carries the version of the package"
			;;
		*0.0.0*)
			# The program was built on its own, without the
			# version of the port.
			ok "the port gives its own version to the program"
			;;
		*)
			fail "our version: the program does not carry the version" \
			    "of the package" "want: ${DISTVERSION}" "got: ${out}"
			;;
		esac
	fi
fi

# ---------------------------------------------------------------- test 5
# The table of the firmware the package supports is made of the pin table
# and of the table of the chip families.  The README holds the table that
# is made of them: a table that is not the one of the files is a table
# that is wrong.
#
# The table is made by a program: a machine without that program cannot
# check the table, and the check is not skipped: the target claims that
# the pins and the files that are shipped agree, and it cannot claim that
# when it did not look.
new_case
if ! command -v python3 >/dev/null 2>&1; then
	fail "supported table: the table cannot be made" \
	    "python3 is not there; the check of the table is not optional"
else
	python3 -I "${ROOT}/tests/gen-supported-table.py" > "${WORK}/table" \
	    2>"${WORK}/table.err"
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "supported table: the table could not be made" \
		    "$(cat "${WORK}/table.err")"
	else
		awk '/^<!-- BEGIN SUPPORTED FIRMWARE -->$/ { inblock = 1; next }
		     /^<!-- END SUPPORTED FIRMWARE -->$/ { inblock = 0 }
		     inblock { print }' "${ROOT}/README.md" > "${WORK}/readme"
		if [ ! -s "${WORK}/readme" ]; then
			fail "supported table: the README holds no table" \
			    "$(cat "${WORK}/table")"
		elif ! diff -u "${WORK}/table" "${WORK}/readme" \
		    > "${WORK}/table.diff"; then
			fail "supported table: the table of the README is not the" \
			    "table of the files; the table is made by" \
			    "tests/gen-supported-table.py and is not written by hand" \
			    "$(cat "${WORK}/table.diff")"
		else
			ok "the table of the README is the table of the files"
		fi
	fi
fi

# ---------------------------------------------------------------- test 6
# The word that names the architecture of a family is not in the table of
# the README: the table names the families and the lines of products of
# the families, and nothing else.
new_case
if grep -qi 'ampere' "${ROOT}/README.md"; then
	fail "supported table: the README names an architecture" \
	    "$(grep -ni 'ampere' "${ROOT}/README.md")"
else
	ok "the README names no architecture"
fi

# ---------------------------------------------------------------- test 7
# The package ships no firmware.  The list of the files of the package
# names no firmware, the port fetches no file at build time, and there is
# no firmware in the tree of the port.
new_case
offenders=$(grep -E 'gsp_[a-z0-9]*\.bin|ucodes_[a-z0-9]*\.bin' \
    "${PORT}/pkg-plist" 2>/dev/null)
if [ -n "${offenders}" ]; then
	fail "no firmware in the package: the list of the files names a" \
	    "firmware" "${offenders}"
elif grep -qE '^MASTER_SITES' "${PORT}/Makefile" || \
    grep -qE '^DISTFILES=.+' "${PORT}/Makefile"; then
	fail "no firmware in the package: the port fetches a file at build" \
	    "time" "$(grep -E '^(DISTFILES|MASTER_SITES)' "${PORT}/Makefile")"
else
	blobs=$(find "${PORT}" -type d -name work -prune -o \
	    -type f -name '*.bin' -print 2>/dev/null)
	if [ -n "${blobs}" ]; then
		fail "no firmware in the package: the tree of the port holds a" \
		    "firmware" "${blobs}"
	else
		ok "the package ships no firmware and the port fetches none"
	fi
fi

# ---------------------------------------------------------------- test 8
# The README is for the user: it says what the user can do and what the
# user needs to know.  The work of the project and the tools of the
# project are not in it.
new_case
offenders=$(grep -niE 'makefile contract|preflight|install-deps|REPO_PURPOSE|bmake|commit hook|make check' \
    "${ROOT}/README.md" 2>/dev/null)
if [ -n "${offenders}" ]; then
	fail "user README: the README names the work of the project" \
	    "${offenders}"
else
	ok "the README names nothing but what the user needs to know"
fi

# ---------------------------------------------------------------- test 9
# The port has no configuration step and says so, and the tree checks the
# pins of the package instead of printing that it does nothing.
new_case
if ! grep -q '^CONFIGURE=\s*none' "${PORT}/Makefile"; then
	fail "configuration: the port does not say that it has no" \
	    "configuration step" "$(grep -n '^CONFIGURE' "${PORT}/Makefile")"
elif grep -q 'nothing to configure' "${ROOT}/Makefile"; then
	fail "configuration: the tree prints that it has nothing to do" \
	    "$(grep -n 'nothing to configure' "${ROOT}/Makefile")"
elif ! grep -q 'tests/docs-test.sh' "${ROOT}/Makefile"; then
	fail "configuration: the tree does not check the pins of the package" \
	    "$(grep -n 'configure' "${ROOT}/Makefile")"
else
	ok "the port has no configuration step and the tree checks the pins"
fi

# --------------------------------------------------------------- test 10
# The column of the hash holds the whole hash.  A prefix of a hash cannot
# tell a file from another one, and the column says "sha256".
new_case
if [ ! -f "${WORK}/table" ]; then
	printf '# the table was not made; the check is skipped\n'
else
	bad=$(awk -F'|' '
		/^\| Driver release/ { next }
		/^\|---/ { next }
		/^\|/ {
			digest = $5
			gsub(/[ \t]/, "", digest)
			if (length(digest) != 64)
				print $0
		}' "${WORK}/table")
	if [ -n "${bad}" ]; then
		fail "the hash of the table: a row holds a hash that is not the" \
		    "whole hash" "${bad}"
	else
		ok "every hash of the table is the whole hash"
	fi
fi

# --------------------------------------------------------------- test 11
# Every family code of the table has a legend in the README: a code
# that nobody can read is a code that means nothing to the reader.
new_case
if [ ! -f "${WORK}/table" ]; then
	printf '# the table was not made; the check is skipped\n'
else
	python3 -I "${ROOT}/tests/gen-supported-table.py" --codes \
	    > "${WORK}/codes" 2>"${WORK}/codes.err"
	missing=
	while IFS= read -r code; do
		[ -n "${code}" ] || continue
		pattern=$(printf '`%s`' "${code}")
		if ! grep -qF "${pattern}" "${ROOT}/README.md"; then
			missing="${missing} ${code}"
		fi
	done < "${WORK}/codes"
	if [ -n "${missing}" ]; then
		fail "the legend of the families: a family of the table has no" \
		    "legend in the README" "missing:${missing}"
	else
		ok "every family of the table has a legend in the README"
	fi
fi

# --------------------------------------------------------------- test 12
# The lint reads the paths one at a time: a path that holds a space is
# one path and not two.  The lint is run in a tree that lives in such a
# path, and a file that holds a number of a work package has to be found.
new_case
if [ -n "${NF_LINT_NO_SPACE_CASE:-}" ]; then
	printf '# the case of the path with a space is skipped in a copy\n'
else
	SPACE="${WORK}/a repo"
	copy_tree "${SPACE}"
	printf '\nA work package of the project: Track #1234\n' \
	    >> "${SPACE}/sysutils/nvidia-firmware/pkg-descr"
	out=$(cd "${SPACE}" && NF_LINT_NO_SPACE_CASE=1 sh tests/docs-test.sh 2>&1)
	rc=$?
	if [ ${rc} -eq 0 ]; then
		fail "a path with a space: the lint did not find the work package" \
		    "${out}"
	elif ! printf '%s\n' "${out}" | grep -q 'no internal id'; then
		fail "a path with a space: the lint failed for another reason" \
		    "${out}"
	else
		ok "the lint finds a work package in a tree that lives in a path" \
		    "with a space"
	fi
fi

# --------------------------------------------------------------- test 13
# The lint runs without TMPDIR.  The variable is not set by a shell and
# the targets of the root Makefile do not set it: a lint that needs it
# fails on an ordinary machine, and the targets that run it fail with it.
#
# Two things are checked here.  The first one is that the lint ran at
# all: a lint that could not run has no plan, and the plan is the line
# that counts the cases.  The second one is that the missing variable
# cost no case: the lint is run twice, once with the variable and once
# without it, and the run without it may not fail more cases than the
# run with it.  The verdict of the other rules is not the verdict of
# this one, so a tree that holds a file the lint refuses does not make
# this case fail.
new_case
if [ -n "${NF_LINT_NO_TMPDIR_CASE:-}" ]; then
	printf '# the case of the missing TMPDIR is skipped in a copy\n'
else
	out=$(cd "${ROOT}" && env -u TMPDIR NF_LINT_NO_SPACE_CASE=1 \
	    NF_LINT_NO_TMPDIR_CASE=1 sh tests/docs-test.sh 2>&1)
	rc=$?
	with=$(cd "${ROOT}" && TMPDIR="${WORK}" NF_LINT_NO_SPACE_CASE=1 \
	    NF_LINT_NO_TMPDIR_CASE=1 sh tests/docs-test.sh 2>&1 | \
	    grep -c '^not ok')
	without=$(printf '%s\n' "${out}" | grep -c '^not ok')
	if ! printf '%s\n' "${out}" | grep -qE '^1\.\.[0-9]+$'; then
		fail "no TMPDIR: the lint did not run without TMPDIR" \
		    "exit ${rc}" "${out}"
	elif [ "${without}" -gt "${with}" ]; then
		fail "no TMPDIR: the lint fails more cases without TMPDIR than" \
		    "with it" "with TMPDIR: ${with}" "without TMPDIR: ${without}" \
		    "${out}"
	else
		ok "the lint runs without TMPDIR and loses no case to it"
	fi
fi

# --------------------------------------------------------------- test 14
# The copy of the installation does not promise that the firmware was
# placed: the installation succeeds on a machine that has no network and
# no firmware is placed there.  The copy has to say what the tool does
# and how the firmware is fetched later.
new_case
offenders=
for file in "${ROOT}/README.md" "${PORT}/pkg-message" "${PORT}/pkg-descr"; do
	[ -f "${file}" ] || continue
	found=$(grep -nE 'installation places|places the firmware|is placed in /boot/firmware/nvidia' \
	    "${file}" 2>/dev/null)
	[ -n "${found}" ] || continue
	offenders="${offenders}${file}:
${found}
"
done
if [ -n "${offenders}" ]; then
	fail "install copy: the copy promises that the firmware was placed" \
	    "${offenders}"
else
	ok "the copy does not promise that the firmware was placed"
fi

# --------------------------------------------------------------- test 15
# The copy tells how the firmware is fetched later: the command that
# fetches it and the command that says what is in place.
new_case
missing=
for file in "${ROOT}/README.md" "${PORT}/pkg-message"; do
	[ -f "${file}" ] || continue
	grep -q 'nvidia-firmware fetch' "${file}" || \
	    missing="${missing} ${file}:fetch"
	grep -q 'nvidia-firmware status' "${file}" || \
	    missing="${missing} ${file}:status"
done
if [ -n "${missing}" ]; then
	fail "install copy: the copy does not tell how to fetch the firmware" \
	    "${missing}"
else
	ok "the copy tells how the firmware is fetched later"
fi

# --------------------------------------------------------------- test 16
# The copy that the public reads names the product and nothing else.  The
# machines of the project, the repositories that only we can open and the
# name of another system of ours are ours and are not of the reader: a
# reader has a machine with a graphics adapter and a package, and the
# names of our machines mean nothing to them.
new_case
offenders=
while IFS= read -r file; do
	[ -f "${file}" ] || continue
	found=$(grep -niE "${INTERNAL_WORD_PATTERN}" "${file}" 2>/dev/null)
	[ -n "${found}" ] || continue
	offenders="${offenders}${file}:
${found}
"
done < "${WORK}/shipped"
if [ -n "${offenders}" ]; then
	fail "no internal name: a shipped file names the machines of the" \
	    "project, a repository of ours or another system of ours; reword" \
	    "the file" "${offenders}"
else
	ok "no shipped file names the machines of the project, a repository of" \
	    "ours or another system of ours"
fi

# --------------------------------------------------------------- test 17
# No link to a place where the reader cannot go.  Every address of the
# copy that the public reads has to name a host of the public: a link to
# a repository of ours is a link to a place where the reader gets nothing.
new_case
offenders=
while IFS= read -r file; do
	[ -f "${file}" ] || continue
	for host in $(link_hosts "${file}"); do
		if ! host_is_public "${host}"; then
			offenders="${offenders}${file}: ${host}
"
		fi
	done
done < "${WORK}/shipped"
if [ -n "${offenders}" ]; then
	fail "no private address: a shipped file names a host that the reader" \
	    "cannot go to; remove the link, or add the host to PUBLIC_HOSTS in" \
	    "tests/docs-test.sh" "${offenders}"
else
	ok "every address of the shipped files names a host of the public"
fi

# --------------------------------------------------------------- test 18
# The two rules above can fail: a rule that is never exercised cannot be
# trusted.  A copy of the tree is planted with the words and with the
# addresses, and the lint has to fail on every one of them.  Every word
# and every address has its own host, so that the output says which one
# was found: a rule that catches one of them catches the other as well,
# and the output would not tell the two apart.
#
# The copy holds two addresses of the public as well, written the way the
# copy of a reader writes them: in a backtick and at the end of a
# sentence.  The lint may not refuse them.
new_case
if [ -n "${NF_LINT_NO_PLANT_CASE:-}" ]; then
	printf '# the case of the planted copy is skipped in a copy\n'
else
	PLANT="${WORK}/planted repo"
	copy_tree "${PLANT}"
	{
		printf '\nThe packages of this tree go to the CloudBSDFleet.\n'
		printf 'The internal repository of the project is here.\n'
		printf 'The name of the package is InternalPkg.\n'
		printf 'https://https-host.invalid/repo\n'
		printf 'git@git-host.invalid:nvidia-firmware.git\n'
		printf 'svn://svn-host.invalid/source\n'
		printf '`https://github.com` is a link of the public.\n'
		printf 'The driver is at https://nvidia.com.\n'
	} >> "${PLANT}/sysutils/nvidia-firmware/pkg-descr"
	out=$(cd "${PLANT}" && NF_LINT_NO_SPACE_CASE=1 \
	    NF_LINT_NO_TMPDIR_CASE=1 NF_LINT_NO_PLANT_CASE=1 \
	    sh tests/docs-test.sh 2>&1)
	rc=$?
	missing=
	[ ${rc} -ne 0 ] || missing="${missing} the lint did not fail"
	for word in CloudBSDFleet 'internal repository' InternalPkg; do
		printf '%s\n' "${out}" | grep -qF "${word}" || \
		    missing="${missing} the word ${word}"
	done
	for host in https-host.invalid git-host.invalid svn-host.invalid; do
		printf '%s\n' "${out}" | grep -qF ": ${host}" || \
		    missing="${missing} the host ${host}"
	done
	printf '%s\n' "${out}" | grep -q 'PUBLIC_HOSTS' || \
	    missing="${missing} the hint that names PUBLIC_HOSTS"
	printf '%s\n' "${out}" | grep -qF 'github.com`' && \
	    missing="${missing} the link in a backtick was refused"
	printf '%s\n' "${out}" | grep -qF 'nvidia.com.' && \
	    missing="${missing} the link at the end of a sentence was refused"
	if [ -n "${missing}" ]; then
	    fail "the planted copy: the lint did not find what it has to find" \
	    "missing:${missing}" "${out}"
	else
	    ok "the lint finds every word and every address it refuses, and" \
	    "leaves the addresses of the public alone"
	fi
fi

# --------------------------------------------------------------- test 19
# The licence of the firmware is discoverable: the README has to name the
# file of the agreement that the package installs.  The name of the file
# is the licence of the port and the directory is the one of the licence
# framework, so the README cannot name a file that the package does not
# install.  The port has to name the file of the agreement as well, and
# the file has to be there: a licence that the port names and does not
# ship is not a licence the reader can read.
new_case
lic=$(awk -F= '/^LICENSE=/ { gsub(/[ \t]/, "", $2); print $2 }' \
    "${PORT}/Makefile")
licfile=$(awk -F= '/^LICENSE_FILE=/ { gsub(/[ \t]/, "", $2); print $2 }' \
    "${PORT}/Makefile")
port=$(awk -F= '/^PORTNAME=/ { gsub(/[ \t]/, "", $2); print $2 }' \
    "${PORT}/Makefile")
case "${licfile}" in
'') rel= ;;
*) rel=$(printf '%s' "${licfile}" | sed -e 's|^[$]{FILESDIR}/|files/|') ;;
esac
missing=
if [ -z "${lic}" ]; then
	missing="${missing} the port names no licence"
fi
if [ -z "${licfile}" ]; then
	missing="${missing} the port names no file of the licence"
elif [ ! -f "${PORT}/${rel}" ]; then
	missing="${missing} the file of the licence is not in the tree"
fi
if [ -z "${port}" ]; then
	missing="${missing} the port names no port"
else
	grep -qF "share/licenses/${port}-" "${ROOT}/README.md" || \
	    missing="${missing} the directory of the licence"
	for one in ${lic}; do
		grep -qF "/${one}" "${ROOT}/README.md" || \
		    missing="${missing} the file ${one}"
	done
fi
if [ -n "${missing}" ]; then
	fail "the licence is discoverable: the README names no file of the" \
	    "licence that the package installs" "missing:${missing}"
else
    ok "the README names the file of the licence that the package installs"
fi

# --------------------------------------------------------------- test 20
# The removal tells the reader to unload the module before the package
# goes: the reader that copies the commands in the order they are written
# has to unload first.  The command that takes the same files away has
# the same prerequisites, and the README has to say both of them: the
# rights of root, and the module that has to be unloaded.  The reader
# also has to be able to find out whether the module is loaded at all.
new_case
unload=$(grep -n 'kldunload nvidia' "${ROOT}/README.md" | head -1 | cut -d: -f1)
delete=$(grep -n 'pkg delete nvidia-firmware' "${ROOT}/README.md" | head -1 \
    | cut -d: -f1)
missing=
if [ -z "${unload}" ] || [ -z "${delete}" ]; then
	missing="${missing} the README names no unload and no removal"
elif [ "${unload}" -ge "${delete}" ]; then
	missing="${missing} the README takes the package away before it says to" \
	    "unload the module (unload: line ${unload}, remove: line ${delete})"
fi
grep -qF 'kldstat -m nvidia' "${ROOT}/README.md" || \
    missing="${missing} the README does not say how to find out whether the module is loaded"
grep -qi 'busy' "${ROOT}/README.md" || \
    missing="${missing} the README does not say what to do when the module is busy"
grep -A4 'nvidia-firmware clean' "${ROOT}/README.md" 2>/dev/null | \
    grep -qi 'root' || \
    missing="${missing} the README does not say that the command that takes the files away needs the rights of root"
if [ -n "${missing}" ]; then
	fail "the removal: the README does not tell the reader what to do" \
	    "missing:${missing}"
else
	ok "the README unloads the module before the package goes away, and" \
	    "says what the command that takes the files away needs"
fi

printf '1..%d\n' "${CASE}"
printf '# passed %d, failed %d\n' "${PASS}" "${FAIL}"
[ "${FAIL}" -eq 0 ] || exit 1
exit 0
