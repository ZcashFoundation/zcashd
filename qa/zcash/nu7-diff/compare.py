#!/usr/bin/env python3
"""Compare the zcashd and Zakura NU7 differential dumps (see SPEC.md).

Usage: compare.py [--fail-on-diff] ZCASHD_JSONL ZAKURA_JSONL[.gz]

Prints every differing series. Change-point quantities are compared at every height either
side changes, and must start at the same height; other quantities at every height either side
emitted. A series or height only one side emitted is a difference, so a dump cannot pass by
leaving values out. A value that one side reports as MISSING (no such function) is listed but
not counted as a difference, for the quantities in MISSING_ALLOWED and as long as the other side
has a record there too. An ERR value (a production function or scenario that failed) is a
difference even when both sides report it.
"""
import bisect
import gzip
import json
import sys
from collections import defaultdict

# Change-point quantities: the value at h is the value of the last record at or below h.
RLE = {
    "nu", "branch_id", "target_spacing", "averaging_window", "halving_index",
    "block_subsidy", "funding_stream_value", "lockbox_value", "miner_subsidy",
    "testnet_min_difficulty_gap_secs",
}


def load(path):
    """Returns {(scenario, quantity, key): {height: value}} from a JSONL file, refusing duplicate records."""
    opener = gzip.open if path.endswith(".gz") else open
    recs = defaultdict(dict)
    with opener(path, "rt") as f:
        for n, line in enumerate(f, 1):
            line = line.strip()
            if line:
                r = json.loads(line)
                k = (r["scenario"], r["quantity"], r.get("key", ""))
                height = r.get("height")
                if height in recs[k]:
                    sys.exit(f"{path}:{n}: duplicate record for {k} at height {height}")
                recs[k][height] = str(r["value"])
    return recs


# The quantities an implementation may report as MISSING because it has no pure production
# function for them. MISSING for any other quantity is a difference, so the harness cannot stop
# comparing a quantity by emitting MISSING for it.
MISSING_ALLOWED = {"tx_version_allowed"}


def is_err(value):
    """Whether a record's value reports a failure (see SPEC.md); None means no record."""
    return value is not None and value.startswith("ERR:")


def value_at(points, heights, h):
    """The change-point value in effect at height h."""
    i = bisect.bisect_right(heights, h) - 1
    return points[heights[i]] if i >= 0 else None


def compare(a, b):
    """Returns (differences, notes) between the zcashd dump a and the Zakura dump b."""
    diffs, notes = [], []
    for k in sorted(set(a) ^ set(b)):
        diffs.append(f"ONLY in {'zcashd' if k in a else 'zakura'}: {k}")
    for k in sorted(set(a) & set(b)):
        scenario, quantity, key = k
        va, vb = a[k], b[k]
        if quantity in RLE and None not in va and None not in vb:
            ha, hb = sorted(va), sorted(vb)
            if ha[0] != hb[0]:
                diffs.append(f"DIFF {scenario} {quantity} [{key}]: zcashd starts at h={ha[0]}, zakura at h={hb[0]}")
            lo = max(ha[0], hb[0])
            pairs = [(h, value_at(va, ha, h), value_at(vb, hb, h)) for h in sorted(set(ha) | set(hb)) if h >= lo]
        else:
            heights = sorted(set(va) | set(vb), key=lambda h: (h is None, h))
            # None, which no record's (string) value can be, marks a height a side has no record for.
            pairs = [(h, va.get(h), vb.get(h)) for h in heights]
        bad = [(h, x, y) for h, x, y in pairs if x != y or is_err(x) or is_err(y)]
        missing = [p for p in bad if quantity in MISSING_ALLOWED and None not in p[1:]
                   and not (is_err(p[1]) or is_err(p[2]))
                   and (p[1].startswith("MISSING") or p[2].startswith("MISSING"))]
        bad = [p for p in bad if p not in missing]
        if missing:
            notes.append(f"MISSING on one side: {scenario} {quantity} [{key}] at {len(missing)} point(s)")
        if bad:
            first = "; ".join(
                f"h={h}: zcashd={'(no record)' if x is None else x} zakura={'(no record)' if y is None else y}"
                for h, x, y in bad[:6])
            diffs.append(f"DIFF {scenario} {quantity} [{key}] at {len(bad)}/{len(pairs)} point(s); first: {first}")
    return diffs, notes


def main():
    args = sys.argv[1:]
    fail_on_diff = "--fail-on-diff" in args
    args = [x for x in args if x != "--fail-on-diff"]
    if len(args) != 2:
        sys.exit(__doc__)
    a, b = load(args[0]), load(args[1])
    diffs, notes = compare(a, b)
    for line in notes + diffs:
        print(line)
    series = len(set(a) & set(b))
    print(f"{series} common series, {len(diffs)} differing, {len(notes)} notes")
    if fail_on_diff and (diffs or not series):
        sys.exit(1)


if __name__ == "__main__":
    main()
