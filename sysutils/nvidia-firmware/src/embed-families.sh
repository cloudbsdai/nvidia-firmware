#!/bin/sh
#
# Turn the family table into a C header.
#
# usage: embed-families.sh <family table> <header>
#
# The tool needs the family table even when the firmware is not installed,
# so the table is compiled into the program as well as installed in the
# share directory.  The table itself stays the one source of truth: this
# script only makes a copy of it for the compiler.

set -eu

src=$1
out=$2

if [ -z "${src}" ] || [ -z "${out}" ]; then
	echo "usage: embed-families.sh <family table> <header>" >&2
	exit 1
fi

{
	echo "/*"
	echo " * Generated from $(basename "${src}") by embed-families.sh."
	echo " * Do not edit; change the family table instead."
	echo " */"
	echo ""
	echo "static const char nf_families_text[] ="
} > "${out}.tmp"

# The two placeholders keep the escaping of the text simple: a backslash
# and a double quote are the only characters a C string needs them for.
awk '
{
	gsub(/\\/, "@@NFBS@@", $0)
	gsub(/"/, "@@NFDQ@@", $0)
	printf "\"%s\\n\"\n", $0
}
' "${src}" | sed -e 's/@@NFBS@@/\\\\/g' -e 's/@@NFDQ@@/\\"/g' >> "${out}.tmp"

echo ";" >> "${out}.tmp"

mv "${out}.tmp" "${out}"
