#!/usr/bin/env python3
"""Check the family table of nvidia-firmware against the chip lists of NVIDIA.

The table says which chip family a PCI device id belongs to.  The chip
lists of the driver releases say the name of every chip.  A chip whose
name tells the architecture has to land in the family the table gives for
that architecture.

usage: family-check.py <families> <supported-gpus.json> ...
"""

import json
import os
import shlex
import sys


def read_table(path):
    """Read the family table; return the ranges and the exact ids."""
    ranges = {}
    exact = {}
    family = None
    for line in open(path):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        fields = line.split()
        if fields[0] == 'family':
            family = fields[1]
        elif fields[0] == 'range':
            ranges.setdefault(family, []).append(
                (int(fields[1], 0), int(fields[2], 0)))
        elif fields[0] == 'exact':
            exact[fields[1].lower()] = family
    return ranges, exact


def read_products(path):
    """Read the product rules of the table.

    A rule is a name of a product, the series it belongs to and the
    family the chip is in.  The rules are looked at in the order they are
    written: the first rule whose name is in the name of the chip is the
    one that counts.
    """
    rules = []
    for line in open(path):
        fields = shlex.split(line, comments=True)
        if not fields or fields[0] != 'product':
            continue
        if len(fields) < 4:
            raise SystemExit('the product rule is not complete: %s' % line)
        rules.append((fields[1], fields[2], fields[3]))
    return rules


def lookup(ranges, exact, devid):
    """The family of a device id, or None."""
    key = '0x%04x' % devid
    if key in exact:
        return exact[key]
    for family, blocks in ranges.items():
        for low, high in blocks:
            if low <= devid <= high:
                return family
    return None


def expected(rules, name):
    """The family a chip is expected to be in, from its name."""
    for pattern, series, family in rules:
        if pattern in name:
            return family
    return None


def main():
    if len(sys.argv) < 3:
        print('usage: family-check.py <families> <supported-gpus.json> ...',
              file=sys.stderr)
        return 2
    ranges, exact = read_table(sys.argv[1])
    rules = read_products(sys.argv[1])
    checked = 0
    bad = []
    unmatched = []
    unmatched_ids = set()
    total = 0
    for path in sys.argv[2:]:
        for chip in json.load(open(path))['chips']:
            total += 1
            devid = int(chip['devid'], 16)
            family = lookup(ranges, exact, devid)
            if family is None:
                unmatched.append((chip['devid'], chip['name']))
                unmatched_ids.add(chip['devid'])
            want = expected(rules, chip['name'])
            if want is None:
                continue
            checked += 1
            if family != want:
                bad.append((chip['devid'], chip['name'], family, want))
    print('chips %d checked %d mismatched %d unmatched %d unique %d' %
          (total, checked, len(bad), len(unmatched), len(unmatched_ids)))
    for row in bad:
        print('MISMATCH %s %s table=%s expected=%s' % row)
    out = os.environ.get('NF_TEST_UNMATCHED', sys.argv[1] + '.unmatched')
    with open(out, 'w') as fp:
        for devid, name in unmatched:
            fp.write('%s %s\n' % (devid, name))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
