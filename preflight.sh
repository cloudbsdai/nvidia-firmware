#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 REVYTECH, Inc.
#
# preflight.sh -- what the build of this tree needs before it starts.
#
# Usage: sh preflight.sh [--json] [--purpose NAME] [--missing-packages]
#                        [--ports-dir DIR] [--port-dir DIR] [--dist-dir DIR]
#
# The items that are checked are the ones this port needs: the make that
# reads a port Makefile (bmake), GNU make (the lint of the root Makefile
# runs under it), the compiler, the manual page linter the test suite
# uses, the package manager, the ports tree that holds the framework, and
# the port itself.  The firmware tarballs are optional: the build fetches
# them when they are not in the dist directory.
#
# Exit 0 when every required item is present, 1 when one of them is
# missing, 2 on a usage error.  --json writes the machine-readable
# contract: a metadata header, an indexed status array, the items, and the
# purpose of the repository.  --missing-packages prints the packages of
# the missing items, one word each, and nothing else.
set -u

usage() {
	cat <<EOF
usage: sh preflight.sh [--json] [--purpose NAME] [--missing-packages]
                       [--ports-dir DIR] [--port-dir DIR] [--dist-dir DIR]
reports what the build of this tree needs before it starts
  --json               write the machine-readable contract
  --purpose NAME       the purpose of the repository, in the contract
  --missing-packages   print the packages of the missing items
EOF
}

json=0
packages_only=0
purpose=
ports_dir=${PORTSDIR:-/usr/ports}
port_dir=${PORTDIR:-sysutils/nvidia-firmware}
dist_dir=${DISTDIR:-}

while [ $# -gt 0 ]; do
	case $1 in
	--json) json=1 ;;
	--missing-packages) packages_only=1 ;;
	--purpose)
		if [ $# -lt 2 ] || [ -z "$2" ]; then
			printf '%s\n' "preflight.sh: --purpose needs a name" >&2
			exit 2
		fi
		purpose=$2
		shift
		;;
	--ports-dir)
		if [ $# -lt 2 ] || [ -z "$2" ]; then
			printf '%s\n' "preflight.sh: --ports-dir needs a directory" >&2
			exit 2
		fi
		ports_dir=$2
		shift
		;;
	--port-dir)
		if [ $# -lt 2 ] || [ -z "$2" ]; then
			printf '%s\n' "preflight.sh: --port-dir needs a directory" >&2
			exit 2
		fi
		port_dir=$2
		shift
		;;
	--dist-dir)
		if [ $# -lt 2 ]; then
			printf '%s\n' "preflight.sh: --dist-dir needs a directory" >&2
			exit 2
		fi
		dist_dir=$2
		shift
		;;
	-h|--help)
		usage
		exit 0
		;;
	*)
		printf '%s\n' "preflight.sh: unknown option $1" >&2
		usage >&2
		exit 2
		;;
	esac
	shift
done

# The purpose has to be one the contract knows.
if [ -n "$purpose" ]; then
	case $purpose in
	application|library|tool|service|docs|config|data|skills|infra|fork) ;;
	*)
		printf '%s\n' "preflight.sh: unknown purpose $purpose" >&2
		exit 2
		;;
	esac
fi

# The directory this script is in is the root of the repository, so the
# port is found whatever the directory make was started from.
root=`dirname "$0"`
case $port_dir in
/*) port_path=$port_dir ;;
*) port_path=$root/$port_dir ;;
esac

# The package manager of FreeBSD lives in /usr/local/sbin, which is not in
# the PATH of a plain shell; the tools are looked for in both.
search_path=$PATH
for extra in /usr/local/sbin /usr/sbin; do
	case :$search_path: in
	*:"$extra":*) ;;
	*) search_path=$search_path:$extra ;;
	esac
done

find_cmd() {
	# find_cmd NAME prints the path of the command, or nothing.
	#
	# The PATH is split on its colons with the parameter expansion of the
	# shell, so the IFS of the shell is never changed.  An IFS that is set
	# for the whole shell also decides how every other word of the script
	# is split, and the scanners refuse it (semgrep: ifs-tampering).  The
	# split is the one the shell does with IFS set to the colon: a
	# directory with a space in it stays one directory, an empty component
	# is the current directory, and a colon at the end does not add a
	# component.
	_rest=$search_path
	while [ -n "$_rest" ]; do
		_dir=${_rest%%:*}
		if [ "$_rest" = "$_dir" ]; then
			_rest=
		else
			_rest=${_rest#*:}
		fi
		[ -n "$_dir" ] || _dir=.
		if [ -x "$_dir/$1" ]; then
			printf '%s\n' "$_dir/$1"
			return 0
		fi
	done
	return 1
}

json_escape() {
	printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

idx=0
missing=0
human=
status_json=
items_json=
packages=

# check_command NAME PACKAGE DESCRIPTION REQUIRED
check_command() {
	_name=$1
	_pkg=$2
	_desc=$3
	_req=$4
	_path=`find_cmd "$_name"`
	if [ -n "$_path" ]; then
		human="$human
ok    $_name ($_path): $_desc"
		status_json="$status_json
{\"index\": $idx, \"name\": \"$_name\", \"state\": \"ok\", \"available\": true}"
		items_json="$items_json
{\"index\": $idx, \"name\": \"$_name\", \"kind\": \"command\", \"state\": \"ok\", \"available\": true, \"required\": $_req, \"path\": \"$_path\", \"package\": \"$_pkg\", \"description\": \"$_desc\"}"
	else
		if [ "$_req" = true ]; then
			missing=$((missing + 1))
			_label=miss
		else
			_label=note
		fi
		human="$human
$_label  $_name: $_desc"
		status_json="$status_json
{\"index\": $idx, \"name\": \"$_name\", \"state\": \"missing\", \"available\": false}"
		items_json="$items_json
{\"index\": $idx, \"name\": \"$_name\", \"kind\": \"command\", \"state\": \"missing\", \"available\": false, \"required\": $_req, \"package\": \"$_pkg\", \"description\": \"$_desc\"}"
		if [ -n "$_pkg" ]; then
			packages="$packages $_pkg"
		fi
	fi
	idx=$((idx + 1))
}

# check_path NAME PATH DESCRIPTION REQUIRED
check_path() {
	_name=$1
	_path=$2
	_desc=$3
	_req=$4
	if [ -e "$_path" ]; then
		human="$human
ok    $_name ($_path): $_desc"
		status_json="$status_json
{\"index\": $idx, \"name\": \"$_name\", \"state\": \"ok\", \"available\": true}"
		items_json="$items_json
{\"index\": $idx, \"name\": \"$_name\", \"kind\": \"path\", \"state\": \"ok\", \"available\": true, \"required\": $_req, \"path\": \"$_path\", \"package\": \"\", \"description\": \"$_desc\"}"
	else
		if [ "$_req" = true ]; then
			missing=$((missing + 1))
			_label=miss
		else
			_label=note
		fi
		human="$human
$_label  $_name ($_path): $_desc"
		status_json="$status_json
{\"index\": $idx, \"name\": \"$_name\", \"state\": \"missing\", \"available\": false}"
		items_json="$items_json
{\"index\": $idx, \"name\": \"$_name\", \"kind\": \"path\", \"state\": \"missing\", \"available\": false, \"required\": $_req, \"path\": \"$_path\", \"package\": \"\", \"description\": \"$_desc\"}"
	fi
	idx=$((idx + 1))
}

# The tools.
check_command bmake bmake "the make that reads a port Makefile" true
check_command gmake gmake "GNU make, the lint of the root Makefile runs under it" false
check_command cc "" "the C compiler that builds the tool" true
check_command mandoc "" "the linter of the manual page, the test suite uses it" true
check_command python3 python3 "the program that makes the table of the firmware" true
check_command pkg "" "the package manager of the ports tree" true

# The places.
check_path ports-tree "$ports_dir/Mk/bsd.port.mk" "the ports tree that holds the framework" true
check_path port "$port_path/Makefile" "the port this tree wraps" true

# The firmware tarballs.  They are fetched by the build when they are not
# there, so they are not required.
if [ -n "$dist_dir" ] && [ -f "$port_path/files/versions.list" ]; then
	for version in `awk '/^[0-9]/ { print $1 }' "$port_path/files/versions.list" | sort -u`; do
		check_path "firmware-$version" "$dist_dir/NVIDIA-FreeBSD-x86_64-$version.tar.xz" \
		    "the firmware tarball of $version (fetched by the build when it is absent)" false
	done
fi

if [ "$packages_only" = 1 ]; then
	if [ -n "$packages" ]; then
		printf '%s\n' "$packages"
	fi
	if [ "$missing" -gt 0 ]; then
		exit 1
	fi
	exit 0
fi

os=`uname -s 2>/dev/null || printf 'unknown'`
arch=`uname -m 2>/dev/null || printf 'unknown'`
cwd=`pwd -P 2>/dev/null || pwd`

if [ "$json" = 1 ]; then
	printf '{\n'
	printf '  "schema_version": "1.0",\n'
	printf '  "tool": "preflight",\n'
	if [ -n "$purpose" ]; then
		printf '  "purpose": "%s",\n' "`json_escape \"$purpose\"`"
	fi
	printf '  "metadata": {\n'
	printf '    "os": "%s",\n' "`json_escape \"$os\"`"
	printf '    "arch": "%s",\n' "`json_escape \"$arch\"`"
	printf '    "cwd": "%s",\n' "`json_escape \"$cwd\"`"
	printf '    "ports_dir": "%s",\n' "`json_escape \"$ports_dir\"`"
	printf '    "port_dir": "%s",\n' "`json_escape \"$port_dir\"`"
	printf '    "dist_dir": "%s",\n' "`json_escape \"$dist_dir\"`"
	printf '    "checked": %s\n' "$idx"
	printf '  },\n'
	printf '  "status": ['
	first=1
	printf '%s\n' "$status_json" | sed '/^$/d' | while IFS= read -r line; do
		[ "$first" = 1 ] || printf ','
		first=0
		printf '\n    %s' "$line"
	done
	printf '\n  ],\n'
	printf '  "items": ['
	first=1
	printf '%s\n' "$items_json" | sed '/^$/d' | while IFS= read -r line; do
		[ "$first" = 1 ] || printf ','
		first=0
		printf '\n    %s' "$line"
	done
	printf '\n  ]\n'
	printf '}\n'
else
	if [ -n "$purpose" ]; then
		printf 'preflight: purpose=%s os=%s arch=%s\n' "$purpose" "$os" "$arch"
	else
		printf 'preflight: os=%s arch=%s\n' "$os" "$arch"
	fi
	printf '%s\n' "$human" | sed '/^$/d'
	if [ "$missing" -eq 0 ]; then
		printf 'preflight: every required item is present (%s item(s) checked)\n' "$idx"
	else
		printf 'preflight: %s required item(s) are missing (%s item(s) checked)\n' "$missing" "$idx"
	fi
fi

if [ "$missing" -gt 0 ]; then
	exit 1
fi
exit 0
