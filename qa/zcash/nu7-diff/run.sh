#!/usr/bin/env bash
# Writes zcashd's NU7 differential dump and compares it with Zakura's committed dump.
#
# Usage: qa/zcash/nu7-diff/run.sh [--fail-on-diff]   (from the repository root, after a build)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${NU7_DIFF_OUT:-$(mktemp -d)/zcashd.jsonl}"
NU7_DIFF_OUT="$OUT" ./src/zcash-gtest --gtest_also_run_disabled_tests --gtest_filter=NU7Diff.DISABLED_Dump
python3 "$HERE/compare.py" "$@" "$OUT" "$HERE/zakura.jsonl.gz"
