# NU7 differential check

zcashd and Zakura must agree on every NU7 consensus value, or the sidecar would reject
blocks that Zakura accepts. This directory checks that by comparing both nodes' values
of each quantity in [`SPEC.md`](SPEC.md), computed with each node's production code.

- `run.sh` writes zcashd's dump with the `NU7Diff.DISABLED_Dump` gtest and compares it
  with Zakura's. CI runs it with `--fail-on-diff` in the `nu7-differential` job.
- `compare.py` compares two dumps. A quantity that one side reports as `MISSING` (Zakura
  has no public function for `tx_version_allowed`) is listed but is not a difference.
- `zakura.jsonl.gz` is Zakura's dump, sorted and gzipped.

## Provenance of `zakura.jsonl.gz`

Written by Zakura's ignored test `crates/zakura-consensus/tests/nu7_diff_dump.rs`, from
commit a4021b9ba (branch `adam/nu7-diff-dump-harness`) on top of Zakura `main` at 3ef9f45f2:

```sh
NU7_DIFF_OUT=zakura.jsonl cargo test -p zakura-consensus --test nu7_diff_dump -- --ignored
sort zakura.jsonl | gzip -9n > zakura.jsonl.gz
```

Those four scenarios contain 478,906 rows and remain unchanged. The `mainnet-real`
scenario is generated separately from Zakura commit
`ae0f629bbee85cfe80d89e65d86fb2e931832efe`, whose production Mainnet parameters schedule
NU7 at 3,543,405. `zakura-mainnet.patch` adds that scenario to the existing harness and
runs its public-network difficulty chains around activation. It changes no production
consensus code.

The combined fixture contains 613,745 rows: the original 478,906 rows plus 134,839
Mainnet rows covering 81 series. Its SHA-256 is
`6ce49c3f4f6c62daf0868efb06df0da6c2d2d8d39de953d470eec2f760f87a95`.

To reproduce the Mainnet rows in a clean checkout of that exact Zakura commit:

```sh
git apply /path/to/zcashd/qa/zcash/nu7-diff/zakura-mainnet.patch
NU7_DIFF_ONLY=mainnet-real NU7_DIFF_OUT=/tmp/zakura-mainnet.jsonl \
  cargo test --release --locked -p zakura-consensus --test nu7_diff_dump -- --ignored
```

Then combine the independently generated Mainnet rows with the original fixture, from
the zcashd checkout:

```sh
git show 32d749f170afc26929267f3b100f829a3737e9eb:qa/zcash/nu7-diff/zakura.jsonl.gz \
  | gzip -dc > /tmp/zakura-original.jsonl
cat /tmp/zakura-original.jsonl /tmp/zakura-mainnet.jsonl \
  | LC_ALL=C sort | gzip -9n > qa/zcash/nu7-diff/zakura.jsonl.gz
```

Regenerate the affected scenarios whenever Zakura's NU7 rules or the spec change,
and update their provenance here. The Mainnet reference must come from Zakura's
production functions, independently of zcashd's dump; a missing scenario or series
is a comparison failure.
