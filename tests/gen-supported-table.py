#!/usr/bin/env python3
"""Print the table of the firmware that the package supports.

The table is made of two files of the port: the pin table, which says
which file belongs to which driver version and how big it is and what
its hash is, and the table of the chip families, which says which file
each family needs for each driver version.

Every file of the pin table is one row of the table.  The families the
file serves are the families whose files are that file, and the lines of
products of a family are the lines of the rules that name the family.

usage: gen-supported-table.py [versions.list] [families]
       gen-supported-table.py --codes [versions.list] [families]

The --codes option prints the names of the families that the table names,
one per line: the README holds a legend of them, and the check of the
legend needs the names.
"""

import os
import shlex
import sys


def read_pin_table(path):
    """Read the pin table; return the rows in the order of the table."""
    rows = []
    with open(path) as fp:
        for lineno, line in enumerate(fp, 1):
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            fields = line.split()
            if len(fields) != 4:
                raise SystemExit('%s: line %d: a line is a version, a '
                                 'file, a size and a hash' % (path, lineno))
            rows.append(fields)
    return rows


def read_families(path):
    """Read the table of the families.

    Returns two things: the files every family needs for every driver
    version, and the lines of products every family is in.
    """
    images = {}
    series = {}
    for line in open(path):
        fields = shlex.split(line, comments=True)
        if not fields:
            continue
        if fields[0] == 'images' and len(fields) >= 4:
            version, family = fields[1], fields[2]
            for name in fields[3:]:
                images.setdefault((version, name), []).append(family)
        elif fields[0] == 'product' and len(fields) >= 4:
            family = fields[3]
            if fields[2] not in series.setdefault(family, []):
                series[family].append(fields[2])
    return images, series


def families_of(images, series, version, name):
    """The families a file serves, with the lines of products of each."""
    out = []
    for family in images.get((version, name), []):
        lines = series.get(family, [])
        if lines:
            out.append('%s (%s)' % (family, ', '.join(lines)))
        else:
            out.append(family)
    return ', '.join(out)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    port = os.path.join(root, 'sysutils', 'nvidia-firmware')
    args = sys.argv[1:]
    codes_only = False
    if args and args[0] == '--codes':
        codes_only = True
        args = args[1:]
    table = args[0] if len(args) > 0 else os.path.join(
        port, 'files', 'versions.list')
    families = args[1] if len(args) > 1 else os.path.join(
        port, 'src', 'nvidia-firmware.families')

    rows = read_pin_table(table)
    images, series = read_families(families)

    if codes_only:
        names = set()
        for version, name, _size, _sha in rows:
            for family in images.get((version, name), []):
                names.add(family)
        for family in sorted(names):
            print(family)
        return 0

    print('| Driver release | File | Size (bytes) | sha256 | GPU families |')
    print('|---|---|---|---|---|')
    for version, name, size, sha in rows:
        serves = families_of(images, series, version, name)
        print('| %s | %s | %s | %s | %s |' %
              (version, name, size, sha, serves))
    return 0


if __name__ == '__main__':
    sys.exit(main())
