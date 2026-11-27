#!/bin/sh
#
# The Makefiles of this tree must not put a value of the caller into the
# text of a shell command.
#
# A path that holds a space, a quote or a semicolon is data.  Data that
# is written into the text of a shell command is read as code: the shell
# runs what the value holds.  Every value is therefore handed to the
# shell through the environment, and the recipe reads it from there.
#
# The purpose is the one exception.  The contract (the commit hook reads
# the text of the recipes and never runs them) wants the value of
# REPO_PURPOSE in the text of the help and the purpose recipes, so that a
# hard-coded word cannot creep in and a machine reads the value the tree
# declares.  The purpose is a single word out of a fixed list.  Every
# other value of the caller, every path in particular, stays out of the
# text of a command.
#
# The test has two parts:
#
#   - a check of the text of the Makefiles: no line of a recipe holds a
#     make variable, so no value of the caller can end up in the text of
#     a command; the purpose is the one name that is allowed there;
#   - a run of the Makefile with a value that holds a space, a quote and
#     a semicolon: the value has to arrive whole, and nothing may run
#     that the value holds.
#
# Everything happens in a directory of its own below ${TMPDIR}; when
# TMPDIR is not set, the directory is made below /var/tmp.  Nothing
# outside of it is touched, and it is taken away when the test is over.
# The directory is made for every run: two runs of the test do not share
# one, and neither of them takes the workspace of the other away.

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PORT=${ROOT}/sysutils/nvidia-firmware
SRC=${PORT}/src

WORK=$(mktemp -d "${TMPDIR:-/var/tmp}/nvidia-firmware-makefile-test.XXXXXX") || {
	printf 'Bail out! cannot make a workspace for the test\n' >&2
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

# The make of this host: GNU make or bmake, whatever there is.
MAKE=
for candidate in gmake bmake make; do
	if command -v "${candidate}" >/dev/null 2>&1; then
		MAKE=${candidate}
		break
	fi
done
if [ -z "${MAKE}" ]; then
	printf 'Bail out! no make was found on this host\n'
	exit 1
fi

# The makes of this host, one after the other.  The cases that hold the
# purpose run under every one of them: the two makes spell the export of
# the purpose differently, and a case that runs under one make alone
# would not see the other one.
MAKES=
for candidate in gmake bmake; do
	if command -v "${candidate}" >/dev/null 2>&1; then
		MAKES="${MAKES} ${candidate}"
	fi
done
if [ -z "${MAKES}" ] && command -v make >/dev/null 2>&1; then
	MAKES=" make"
fi

rm -rf "${WORK}"
mkdir -p "${WORK}"

# ---------------------------------------------------------------- test 1
# No line of a recipe of either Makefile holds a make variable, with one
# exception: the purpose, which the contract reads by name out of the
# text of the recipe (test 2).  A line of a recipe is a line that begins
# with a tab.
recipe_holds_variable() {
	awk '
		# A line of a recipe is a line that begins with a tab and
		# that follows a rule.  A tab line that follows an
		# assignment is the continuation of that assignment.
		/^\t/ {
			if (inrecipe == 0)
				next
			line = $0
			sub(/^\t/, "", line)
			# The purpose is printed by name: the contract reads
			# it out of the text of the recipe.  Every other
			# value of the caller stays out.
			gsub(/\$\(REPO_PURPOSE\)/, "", line)
			if (line ~ /\$\(/ || line ~ /\$\{/)
				print FILENAME ":" FNR ": " line
			next
		}
		{
			inrecipe = 0
			if ($0 ~ /^[A-Za-z0-9_.\/$-]+[ \t]*:/ &&
			    $0 !~ /^[ \t]*#/)
				inrecipe = 1
		}
	' "$1"
}

new_case
offenders=
for file in "${ROOT}/Makefile" "${PORT}/Makefile"; do
	[ -f "${file}" ] || continue
	offenders="${offenders}$(recipe_holds_variable "${file}")"
done
if [ -n "${offenders}" ]; then
	fail "recipe: a recipe of a Makefile holds a make variable" \
	    "${offenders}"
else
	ok "no recipe of either Makefile holds a make variable"
fi

# ---------------------------------------------------------------- test 2
# The purpose is printed by name.  The contract (the commit hook reads the
# text of the recipes and never runs them) wants the value of REPO_PURPOSE
# in the text of the help and the purpose recipes: a hard-coded word
# cannot creep in, and a machine reads the value the tree declares.  It is
# the one value of the caller that is written into the text of a recipe;
# every other value travels through the environment (test 1).
#
# The recipe of a named target: the target line and the lines of its
# recipe, up to the next target.
recipe_of() {
	awk -v tgt="$1" '
	BEGIN { keep = 0 }
	/^[A-Za-z_][A-Za-z0-9_.-]*[ \t]*:([^=]|$)/ {
		lhs = $0
		sub(/:.*$/, "", lhs)
		keep = 0
		n = split(lhs, a, /[ \t]+/)
		for (i = 1; i <= n; i++)
			if (a[i] == tgt) keep = 1
	}
	keep { print }
	' "${ROOT}/Makefile"
}

# The marker is the name of a file that must not come to life.
MARKER=${WORK}/marker

new_case
help_rec=$(recipe_of help)
purpose_rec=$(recipe_of purpose)
declared=$(sed -n 's/^\(export[ \t][ \t]*\)\{0,1\}REPO_PURPOSE[ \t]*=[ \t]*//p' "${ROOT}/Makefile" | head -1)
if ! printf '%s' "${help_rec}" | grep -qF 'purpose: $(REPO_PURPOSE)'; then
	fail "purpose: the help recipe does not print the purpose by name" \
	    "${help_rec}"
elif ! printf '%s' "${purpose_rec}" | grep -qF '$(REPO_PURPOSE)'; then
	fail "purpose: the purpose recipe does not print the purpose by name" \
	    "${purpose_rec}"
elif [ -z "${declared}" ]; then
	fail "purpose: the tree declares no purpose" "${declared}"
else
	out=$(cd "${ROOT}" && env -u MAKEFLAGS -u MAKELEVEL "${MAKE}" purpose 2>&1)
	if [ "${out}" != "${declared}" ]; then
		fail "purpose: the purpose printed is not the one the tree declares" \
		    "got: ${out}" "want: ${declared}"
	else
		ok "the purpose is printed by name and is the one the tree declares"
	fi
fi

# ---------------------------------------------------------------- test 3
# The same for a path: the value of PREFIX is printed as it is and is
# not read as shell source.
new_case
out=$(cd "${ROOT}" && env -u MAKEFLAGS -u MAKELEVEL "${MAKE}" help \
    PREFIX="${WORK}/a b\"c" 2>&1)
if [ -f "${MARKER}" ]; then
	fail "help: the payload of the caller was run" "${out}"
elif ! printf '%s' "${out}" | grep -q "${WORK}/a b\"c"; then
	fail "help: the path did not arrive whole" "${out}"
else
	ok "a path with a space and a quote arrives whole"
fi

# ---------------------------------------------------------------- test 4
# The staging directory is one argument and one goal.  A staging
# directory with a space used to become two words, and the second one
# was a goal of its own that the port would have run.
#
# The bmake of the test is a program of its own: it writes down the
# goals it was given and the environment it was given, and it does
# nothing else.  The id(1) of the test says that the caller is root, so
# that the install does not stop at the question of the root password.
FAKEBIN=${WORK}/bin
mkdir -p "${FAKEBIN}"
# The compiler of the test lives in a directory of its own: the fake bmake
# of the test above must not be found when the build of the tool runs.
CCBIN=${WORK}/ccbin
cat > "${FAKEBIN}/bmake" <<-'STUB'
	#!/bin/sh
	if [ "$1" = "-V" ] && [ "$2" = "MAKE_VERSION" ]; then
		printf '%s\n' "1.0"
		exit 0
	fi
	{
		printf '%s\n' "goal"
		for arg in "$@"; do
			printf 'arg:%s\n' "$arg"
		done
		printf 'env:STAGEDIR=%s\n' "${STAGEDIR-}"
	} >> "${NF_MAKE_TEST_LOG}"
	exit 0
	STUB
cat > "${FAKEBIN}/id" <<-'STUB'
	#!/bin/sh
	case "${1-}" in
	-u) printf '%s\n' "0" ;;
	*) printf '%s\n' "uid=0(root)" ;;
	esac
	exit 0
	STUB
chmod +x "${FAKEBIN}/bmake" "${FAKEBIN}/id"

LOG=${WORK}/bmake.log
: > "${LOG}"
STAGE="${WORK}/stage dir"
new_case
out=$(cd "${ROOT}" && env -u MAKEFLAGS -u MAKELEVEL PATH="${FAKEBIN}:${PATH}" \
    NF_MAKE_TEST_LOG="${LOG}" \
    "${MAKE}" install STAGEDIR="${STAGE}" YES=1 2>&1)
goals=$(sed -n 's/^arg://p' "${LOG}")
stage=$(sed -n 's/^env:STAGEDIR=//p' "${LOG}")
if [ -f "${MARKER}" ]; then
	fail "install: the payload of the caller was run" "${out}"
elif [ "${goals}" != "install" ]; then
	fail "install: the goals of the port are not only install" \
	    "goals: ${goals}" "${out}"
elif [ "${stage}" != "${STAGE}" ]; then
	fail "install: the staging directory did not arrive whole" \
	    "got: ${stage}" "want: ${STAGE}"
else
	ok "the staging directory is one argument and the only goal is install"
fi

# ---------------------------------------------------------------- test 5
# The port Makefile reads the staging directory from the environment as
# well: the text of its recipe does not hold the value of the caller.
new_case
if [ ! -f "${PORT}/Makefile" ] || ! command -v bmake >/dev/null 2>&1; then
	printf '# the port Makefile or bmake is not there; the test is skipped\n'
elif ! bmake -n -C "${PORT}" do-install >/dev/null 2>&1; then
	printf '# the port Makefile cannot be read by bmake; the test is skipped\n'
else
	out=$(cd "${PORT}" && bmake -n do-install \
	    STAGEDIR="${WORK}/stage dir\"x" 2>&1)
	if printf '%s' "${out}" | grep -q 'stage dir"x'; then
		fail "port: the value of the caller is in the text of the recipe" \
		    "${out}"
	else
		ok "the port Makefile keeps the value of the caller out of its recipe"
	fi
fi

# ---------------------------------------------------------------- test 6
# The test runs without TMPDIR.  The variable is not set by a shell and
# the targets of the root Makefile do not set it: a test that needs it
# fails on an ordinary machine, and the targets that run it fail with it.
new_case
if [ -n "${NF_MAKEFILE_TEST_NO_TMPDIR_CASE:-}" ]; then
	printf '# the case of the missing TMPDIR is skipped in a copy\n'
else
	out=$(cd "${ROOT}" && env -u TMPDIR \
	    NF_MAKEFILE_TEST_NO_TMPDIR_CASE=1 sh tests/makefile-test.sh 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "no TMPDIR: the test did not run without TMPDIR" \
		    "exit ${rc}" "${out}"
	else
		ok "the test runs without TMPDIR"
	fi
fi

# ---------------------------------------------------------------- test 7
# The purpose is a word out of a fixed list and it is checked before it
# is written into the text of a command: a caller that sets a purpose of
# its own must not get the text of the recipe to be run.
#
# The case runs under every make of the host: the two makes spell the
# export of the purpose differently, and a case that runs under one make
# alone would not see the other one.
payload='x"; touch '"${MARKER}"'; #'
for m in ${MAKES}; do
	new_case
	out=$(cd "${ROOT}" && env -u MAKEFLAGS -u MAKELEVEL "${m}" purpose \
	    REPO_PURPOSE="${payload}" 2>&1)
	rc=$?
	if [ -f "${MARKER}" ]; then
		fail "purpose: the purpose of the caller was run (${m})" "${out}"
		rm -f "${MARKER}"
	elif [ ${rc} -eq 0 ]; then
		fail "purpose: a purpose that is not a word of the list was accepted (${m})" \
		    "${out}"
	else
		ok "a purpose that is not a word of the list stops the build (${m})"
	fi
done

# ---------------------------------------------------------------- test 8
# The same for the help: the first line of the help holds the purpose.
for m in ${MAKES}; do
	new_case
	out=$(cd "${ROOT}" && env -u MAKEFLAGS -u MAKELEVEL "${m}" help \
	    REPO_PURPOSE="${payload}" 2>&1)
	rc=$?
	if [ -f "${MARKER}" ]; then
		fail "help: the purpose of the caller was run (${m})" "${out}"
		rm -f "${MARKER}"
	elif [ ${rc} -eq 0 ]; then
		fail "help: a purpose that is not a word of the list was accepted (${m})" \
		    "${out}"
	else
		ok "the help refuses a purpose that is not a word of the list (${m})"
	fi
done

# ---------------------------------------------------------------- test 9
# The value that is checked has to be the value that the recipe prints.
# The Makefile hands the purpose over under two names: the name that the
# recipe uses and a copy of it under a name of its own.  A caller that
# sets the copy to a word of the list and the value that the recipe
# prints to something else has one value checked and another one
# printed: the check has to refuse that as well, and nothing may run.
for m in ${MAKES}; do
	new_case
	out=$(cd "${ROOT}" && env -u MAKEFLAGS -u MAKELEVEL "${m}" purpose \
	    REPO_PURPOSE="${payload}" NF_REPO_PURPOSE=tool 2>&1)
	rc=$?
	if [ -f "${MARKER}" ]; then
		fail "purpose: the copy of the purpose was forged (${m})" "${out}"
		rm -f "${MARKER}"
	elif [ ${rc} -eq 0 ]; then
		fail "purpose: the forged copy of the purpose was accepted (${m})" \
		    "${out}"
	else
		ok "the check refuses a purpose that the copy does not name (${m})"
	fi
done

# --------------------------------------------------------------- test 10
# A purpose that is a word of the list is used as it is: the build does
# not stop, and the word that is printed is the word that was asked for.
for m in ${MAKES}; do
	new_case
	out=$(cd "${ROOT}" && env -u MAKEFLAGS -u MAKELEVEL "${m}" purpose \
	    REPO_PURPOSE=library 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "purpose: a purpose of the list was refused (${m})" "${out}"
	elif [ "${out}" != "library" ]; then
		fail "purpose: the purpose that is printed is not the one that was asked for (${m})" \
		    "${out}"
	else
		ok "a purpose of the list is printed as it is (${m})"
	fi
done

# --------------------------------------------------------------- test 11
# The version of the program is a value of the caller and it is not
# written into the text of a command of the compiler: the generator
# reads it from the environment, refuses a version that is not a date
# version, and writes no header for it.
new_case
GEN=${SRC}/embed-version.sh
HDR=${WORK}/nf_version.h
rm -f "${HDR}"
out=$(NF_VERSION="x'; touch ${MARKER}; #" sh "${GEN}" "${HDR}" 2>&1)
rc=$?
if [ -f "${MARKER}" ]; then
	fail "version: the version of the caller was run" "${out}"
	rm -f "${MARKER}"
elif [ ${rc} -eq 0 ]; then
	fail "version: a version that is not a date version was accepted" "${out}"
elif [ -f "${HDR}" ]; then
	fail "version: a header was written for a version that is not one" \
	    "$(cat "${HDR}")"
else
	ok "the generator refuses a version that is not a date version"
fi

# --------------------------------------------------------------- test 12
# A version that is a date version is written into the header, and the
# header holds the version that was asked for.
new_case
rm -f "${HDR}"
out=$(NF_VERSION=2026.11.26 sh "${GEN}" "${HDR}" 2>&1)
rc=$?
if [ ${rc} -ne 0 ]; then
	fail "version: a date version was refused" "exit ${rc}" "${out}"
elif ! grep -q '#define NF_VERSION "2026.11.26"' "${HDR}"; then
	fail "version: the header does not hold the version" \
	    "$(cat "${HDR}")"
else
	ok "the generator writes the date version into the header"
fi

# --------------------------------------------------------------- test 13
# The build of the tool: the version of the caller never reaches the text
# of a command of the compiler.  The build runs in a copy of the sources
# with a compiler of the test in place of the real one, so that the
# version of the caller would be run by the compiler if it were written
# into the text of the command.
new_case
skip=
if ! command -v bmake >/dev/null 2>&1; then
	skip="bmake is not there"
else
	rm -rf "${WORK}/src"
	mkdir -p "${WORK}/src"
	cp -R "${SRC}/." "${WORK}/src/"
	rm -f "${WORK}/src"/*.o "${WORK}/src"/*.pieo "${WORK}/src"/.depend* \
	    "${WORK}/src"/nvidia-firmware "${WORK}/src"/nvidia-firmware-test \
	    "${WORK}/src"/nvidia-firmware.8.gz "${WORK}/src"/nf_paths.h \
	    "${WORK}/src"/nf_families_data.h "${WORK}/src"/nf_version.h
	# The compiler of the test: it writes down that it was called and
	# it leaves an empty file where the compiler would leave the
	# object.
	mkdir -p "${CCBIN}"
	cat > "${CCBIN}/cc" <<-'STUB'
	#!/bin/sh
	printf '%s\n' "$*" >> "${NF_MAKE_TEST_LOG}"
	out=
	prev=
	for a in "$@"; do
		if [ "${prev}" = "-o" ]; then
			out=${a}
		fi
		prev=${a}
	done
	if [ -n "${out}" ]; then
		: > "${out}"
	fi
	exit 0
	STUB
	chmod +x "${CCBIN}/cc"
	LOG2=${WORK}/cc.log
	: > "${LOG2}"
	if ! (cd "${WORK}/src" && env -u MAKEFLAGS -u MAKELEVEL \
	    bmake -n all >/dev/null 2>&1); then
		skip="the Makefile of the tool cannot be read by bmake"
	fi
fi
if [ -n "${skip}" ]; then
	printf '# %s; the build of the tool is not tested\n' "${skip}"
else
	out=$(cd "${WORK}/src" && env -u MAKEFLAGS -u MAKELEVEL \
	    PATH="${CCBIN}:${PATH}" NF_MAKE_TEST_LOG="${LOG2}" \
	    COMPILER_TYPE=clang MK_DEBUG_FILES=no \
	    bmake "NF_VERSION=x'; touch ${MARKER}; #" all 2>&1)
	rc=$?
	if [ -f "${MARKER}" ]; then
		fail "version: the version of the caller was run by the build" \
		    "${out}"
		rm -f "${MARKER}"
	elif [ ${rc} -eq 0 ]; then
		fail "version: the build took a version that is not a date version" \
		    "${out}"
	else
		ok "the build refuses a version that is not a date version"
	fi
	# The same build with a version that is a date version: the
	# compiler is called and the header holds the version.
	new_case
	out=$(cd "${WORK}/src" && env -u MAKEFLAGS -u MAKELEVEL \
	    PATH="${CCBIN}:${PATH}" NF_MAKE_TEST_LOG="${LOG2}" \
	    COMPILER_TYPE=clang MK_DEBUG_FILES=no \
	    bmake NF_VERSION=2026.11.26 all 2>&1)
	rc=$?
	if [ ${rc} -ne 0 ]; then
		fail "version: the build of the tool failed" "exit ${rc}" "${out}"
	elif [ ! -s "${LOG2}" ]; then
		fail "version: the compiler was not called" "${out}"
	elif ! grep -q '#define NF_VERSION "2026.11.26"' \
	    "${WORK}/src/nf_version.h" 2>/dev/null; then
		fail "version: the header of the build does not hold the version" \
		    "${out}"
	else
		ok "the build writes the version of the package into the header"
	fi
fi
printf '1..%d\n' "${CASE}"
printf '# passed %d, failed %d\n' "${PASS}" "${FAIL}"
[ "${FAIL}" -eq 0 ] || exit 1
exit 0