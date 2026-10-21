#!/usr/bin/env python3
"""Tests for compare.py: in particular, a dump that leaves values out must not pass."""
import os
import tempfile
import unittest

from compare import compare, load


def dump(*records):
    """A dump with the given (scenario, quantity, key, height, value) records, as load() returns it."""
    recs = {}
    for scenario, quantity, key, height, value in records:
        recs.setdefault((scenario, quantity, key), {})[height] = value
    return recs


class CompareTest(unittest.TestCase):
    def test_identical_dumps_match(self):
        a = dump(("s", "expected_bits", "", 1, "0x1"), ("s", "branch_id", "", 0, "0xa"))
        self.assertEqual(compare(a, a), ([], []))

    def test_a_series_only_one_side_has_differs(self):
        diffs, _ = compare({}, dump(("s", "expected_bits", "", 1, "0x1")))
        self.assertEqual(len(diffs), 1)

    def test_a_height_only_one_side_has_differs(self):
        a = dump(("s", "expected_bits", "", 1, "0x1"))
        b = dump(("s", "expected_bits", "", 1, "0x1"), ("s", "expected_bits", "", 2, "0x2"))
        diffs, _ = compare(a, b)
        self.assertEqual(len(diffs), 1)

    def test_change_points_must_start_at_the_same_height(self):
        diffs, _ = compare(dump(("s", "branch_id", "", 5, "0xa")), dump(("s", "branch_id", "", 0, "0xa")))
        self.assertEqual(len(diffs), 1)

    def test_duplicate_records_are_refused(self):
        record = '{"scenario": "s", "quantity": "expected_bits", "key": "", "height": 1, "value": "0x1"}\n'
        with tempfile.NamedTemporaryFile("w", suffix=".jsonl", delete=False) as f:
            f.write(record + record.replace("0x1", "0x2"))
        try:
            with self.assertRaises(SystemExit):
                load(f.name)
        finally:
            os.unlink(f.name)

    def test_missing_against_a_height_without_a_record_differs(self):
        a = dump(("s", "tx_version_allowed", "v4", 1, "MISSING:x"), ("s", "tx_version_allowed", "v4", 2, "MISSING:x"))
        b = dump(("s", "tx_version_allowed", "v4", 1, "true"))
        diffs, notes = compare(a, b)
        self.assertEqual((len(diffs), len(notes)), (1, 1))

    def test_no_value_stands_for_a_missing_record(self):
        a = dump(("s", "expected_bits", "", 1, "0x1"), ("s", "expected_bits", "", 2, "ABSENT"))
        b = dump(("s", "expected_bits", "", 1, "0x1"))
        diffs, _ = compare(a, b)
        self.assertEqual(len(diffs), 1)

    def test_err_differs_even_when_both_sides_report_it(self):
        failed = dump(("s", "SCENARIO", "", None, "ERR:could not build"))
        diffs, _ = compare(failed, failed)
        self.assertEqual(len(diffs), 1)

    def test_missing_differs_for_other_quantities(self):
        a = dump(("s", "expected_bits", "", 1, "MISSING:no such function"))
        b = dump(("s", "expected_bits", "", 1, "0x1"))
        diffs, notes = compare(a, b)
        self.assertEqual((len(diffs), len(notes)), (1, 0))

    def test_missing_is_noted_not_counted(self):
        a = dump(("s", "tx_version_allowed", "v4", 1, "MISSING:no such function"))
        b = dump(("s", "tx_version_allowed", "v4", 1, "true"))
        diffs, notes = compare(a, b)
        self.assertEqual((len(diffs), len(notes)), (0, 1))


if __name__ == "__main__":
    unittest.main()
