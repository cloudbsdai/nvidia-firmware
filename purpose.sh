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

set -eu

purpose=${NF_REPO_PURPOSE:-}

if [ -z "${purpose}" ]; then
	echo "purpose.sh: the purpose of the repository is not set" >&2
	exit 1
fi

case "${purpose}" in
application | library | tool | service | docs | config | data | skills | \
infra | fork)
	printf '%s\n' "${purpose}"
	exit 0
	;;
esac

echo "purpose.sh: the purpose of the repository is not a word of the list:" \
    "${purpose}" >&2
exit 1
