#!/bin/sh
#
# Write the rule of devd of the package.
#
# usage: make-devd-rule.sh <prefix> <file> [template]
#
# The rule calls the tool that the package installs, and the path of the
# tool depends on the prefix the package was built with.  The template of
# the rule is files/nvidia_firmware.conf, and every @PREFIX@ in it is the
# prefix of the package.
#
# The prefix is written into the rule of devd, which is a file of its own
# grammar: a prefix that holds a quote, a semicolon or a newline would end
# the string of the call and would put a rule of its own into the file.
# devd(8) runs the calls of a rule as root, so a prefix that is written
# into the rule as it is can add a call of its own.  The prefix is
# therefore checked before it is written: it has to be a path that is made
# of the characters a path is made of, and nothing else.  A prefix that is
# refused is a build that stops, not a rule that is broken.

set -eu

if [ $# -lt 2 ]; then
	echo "usage: make-devd-rule.sh <prefix> <file> [template]" >&2
	exit 1
fi

prefix=$1
out=$2
template=${3:-}

if [ -z "${template}" ]; then
	echo "make-devd-rule.sh: the template of the rule is not named" >&2
	exit 1
fi

if [ ! -f "${template}" ]; then
	echo "make-devd-rule.sh: cannot read ${template}" >&2
	exit 1
fi

# The prefix has to be a path: it starts at the root, it holds no
# character that is not a character of a path, and none of its parts is
# empty, a dot or two dots.
check_prefix() {
	case "$1" in
	/*) ;;
	*)
		echo "make-devd-rule.sh: the prefix is not an absolute path: $1" >&2
		return 1
		;;
	esac
	case "$1" in
	*[!A-Za-z0-9._/-]*)
		echo "make-devd-rule.sh: the prefix holds a character that a" \
		    "path does not hold: $1" >&2
		return 1
		;;
	esac
	rest=${1#/}
	while [ -n "${rest}" ]; do
		part=${rest%%/*}
		case "${part}" in
		"" | "." | "..")
			echo "make-devd-rule.sh: the prefix is not a plain" \
			    "path: $1" >&2
			return 1
			;;
		esac
		case "${rest}" in
		*/*) rest=${rest#*/} ;;
		*) rest="" ;;
		esac
	done
	if [ "$1" = "/" ]; then
		echo "make-devd-rule.sh: the prefix is the root: $1" >&2
		return 1
	fi
	return 0
}

check_prefix "${prefix}"

# Every @PREFIX@ of a line is the prefix: the loop takes the first one
# that is left and starts again, so a line that holds the placeholder
# twice is written out with the prefix in both places.
while IFS= read -r line || [ -n "${line}" ]; do
	while [ "${line#*@PREFIX@}" != "${line}" ]; do
		head=${line%%@PREFIX@*}
		tail=${line#*@PREFIX@}
		line="${head}${prefix}${tail}"
	done
	printf '%s\n' "${line}"
done < "${template}" > "${out}"

exit 0
