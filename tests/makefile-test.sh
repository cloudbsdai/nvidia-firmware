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
declared=$(sed -n 's/^REPO_PURPOSE[ \t]*=[ \t]*//p' "${ROOT}/Makefile" | head -1)
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
new_case
payload='x"; touch '"${MARKER}"'; #'
out=$(cd "${ROOT}" && env -u MAKEFLAGS -u MAKELEVEL "${MAKE}" purpose \
    REPO_PURPOSE="${payload}" 2>&1)
rc=$?
if [ -f "${MARKER}" ]; then
	fail "purpose: the purpose of the caller was run" "${out}"
elif [ ${rc} -eq 0 ]; then
	fail "purpose: a purpose that is not a word of the list was accepted" \
	    "${out}"
else
	ok "a purpose that is not a word of the list stops the build"
fi

# ---------------------------------------------------------------- test 8
# The same for the help: the first line of the help holds the purpose.
new_case
out=$(cd "${ROOT}" && env -u MAKEFLAGS -u MAKELEVEL "${MAKE}" help \
    REPO_PURPOSE="${payload}" 2>&1)
rc=$?
if [ -f "${MARKER}" ]; then
	fail "help: the purpose of the caller was run" "${out}"
elif [ ${rc} -eq 0 ]; then
	fail "help: a purpose that is not a word of the list was accepted" \
	    "${out}"
else
	ok "the help refuses a purpose that is not a word of the list"
fi

printf '1..%d\n' "${CASE}"
printf '# passed %d, failed %d\n' "${PASS}" "${FAIL}"
[ "${FAIL}" -eq 0 ] || exit 1
exit 0