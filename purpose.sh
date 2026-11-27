#!/bin/sh
#
# Check the purpose of the repository and print it.
#
# usage: purpose.sh
#
# The purpose is a single word out of a fixed list: the commit hook
# refuses any other word, and this script refuses it at run time as well.
#
# The purpose is the one value that the Makefile writes into the text of
# a command, because the contract reads the name of the variable out of
# the text of the recipe.  A value of the caller that holds a quote or a
# semicolon would therefore be read as shell syntax.  The value is
# handed to the shell through the environment instead, this script reads
# it from there and checks it against the list, and only a purpose that
# is a word of the list is printed.  A caller that sets a purpose of its
# own gets the build to stop before the value reaches a command.
#
# The name of the variable in the environment is the name of the variable
# in the Makefile: the value that is checked here is the very value that
# the recipes print, and a caller cannot have one value checked and
# another one printed.
#
# The purpose is read under two names, and both are checked: REPO_PURPOSE,
# the name that the recipes use, and NF_REPO_PURPOSE, the copy of it that
# the Makefile hands over under a name of its own.  The two makes spell
# the export differently: GNU make exports the value of the variable as
# it is when the recipe runs, and bmake exports the value that the
# export directive holds.  The copy is written as a reference to the
# value, so that both makes read it again when the recipe runs, and one
# of the two copies is therefore always the value that the recipes print.
# A caller that sets one of the two names on the command line cannot
# make this script see a word that the recipes do not print.
#
# The value that is refused is not printed: it is a value of the caller
# and it may hold anything, and the message of the error names the
# variable and the words that are accepted instead.

set -eu

words="application, library, tool, service, docs, config, data, skills, infra, fork"

purpose=${REPO_PURPOSE:-}
copy=${NF_REPO_PURPOSE:-}

# Is the value one word of the list?
is_word() {
	case "$1" in
	application | library | tool | service | docs | config | data | \
	skills | infra | fork)
		return 0
		;;
	esac
	return 1
}

if [ -z "${purpose}" ]; then
	echo "purpose.sh: the purpose of the repository is not set:" \
	    "set REPO_PURPOSE to one of: ${words}" >&2
	exit 1
fi

if [ -z "${copy}" ]; then
	echo "purpose.sh: the copy of the purpose is not set:" \
	    "set NF_REPO_PURPOSE to one of: ${words}" >&2
	exit 1
fi

if ! is_word "${purpose}" || ! is_word "${copy}"; then
	echo "purpose.sh: the purpose of the repository is not a word of the list:" \
	    "set REPO_PURPOSE to one of: ${words}" >&2
	exit 1
fi

printf '%s\n' "${purpose}"
exit 0
