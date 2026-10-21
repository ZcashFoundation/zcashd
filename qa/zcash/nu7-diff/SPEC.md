# NU7 differential dump spec

Every implementation writes the SAME quantities, for the SAME scenarios, in the SAME format, using ONLY
that implementation's own production functions (never re-implement a formula in the harness; if an
implementation has no function for a quantity, emit a `MISSING` record, see below).

Each implementation writes one JSONL file (`zcashd.jsonl`, `zakura.jsonl`), one JSON object per line:

```
{"scenario": "...", "quantity": "...", "key": "...", "height": <u32 or null>, "value": "<string>"}
```

- `value` is always a string (decimal integers for amounts in zatoshi, hex for branch ids, addresses as
  their string encoding, `true`/`false`, or `ERR:<short error>` if the production function returned an
  error, or `MISSING:<why>` if the implementation has no such function).
- `key` disambiguates sub-values (receiver name, fee input, balance input, etc.). Use `""` if none.
- Sort order does not matter; the comparer sorts.

The zcashd dump is `NU7Diff.DISABLED_Dump` in `src/gtest/test_nu7_diff_dump.cpp`. The Zakura dump is
the ignored test `crates/zakura-consensus/tests/nu7_diff_dump.rs` in the Zakura repository; its output
is committed here as `zakura.jsonl.gz` (see `README.md` for its provenance).

## Scenarios

Build each network with the implementation's normal configured-network builder
(e.g. `testnet::Parameters::build()...to_network()` or the equivalent), starting from that
implementation's public defaults for that network (activation heights, funding streams, lockbox, halving params),
and only changing the NU7 activation height. Do not change anything else. If the builder demands other
parameters (e.g. NU7 funding streams must be supplied explicitly), use the implementation's own public
Testnet NU7 defaults if it has them, and record what you had to do in `NOTES.<impl>.md`.

| scenario id  | network                                  | NU7 activation height |
|--------------|-------------------------------------------|-----------------------|
| `testnet-A1` | configured Testnet, public defaults       | 4_200_000             |
| `testnet-A2` | configured Testnet, public defaults       | 4_187_001             |
| `testnet-real` | the implementation's default public Testnet, unchanged | its scheduled NU7 height, 4_465_026 |
| `mainnet-real` | the implementation's default public Mainnet, unchanged | its scheduled NU7 height, 3_543_405 |
| `regtest-R`  | Regtest, the implementation's default Regtest config, with every upgrade through NU6.3 at height 1 (or the implementation's default Regtest heights if it forces them) and NU7 at 300 | 300 |

If an implementation cannot build a scenario, emit one record
`{"scenario": S, "quantity": "SCENARIO", "key": "", "height": null, "value": "ERR:<why>"}` and move on.

## Quantities

### A. Run-length (change-point) quantities

For each of these, iterate EVERY height `h` in the scan range and emit a record ONLY at the first height of
the range and at every height where the value differs from the value at `h-1`
("change points"). Scan ranges:
- `testnet-A1`, `testnet-A2`, `testnet-real`, `mainnet-real`: `h` in `[A - 2_000, A + 12_000_000]`
- `regtest-R`: `h` in `[1, 2_000_000]`

Quantities (all "for the block at height h"):
- `nu`: current network upgrade name at h (use the implementation's Display/Debug of the enum, normalized to
  e.g. `Nu6_3`, `Nu7`).
- `branch_id`: consensus branch id at h, hex lowercase with `0x` prefix.
- `target_spacing`: target block spacing in seconds for the block at h.
- `averaging_window`: the PoW averaging window length (number of blocks) used to compute the difficulty
  target of the block at h.
- `halving_index`: number of halvings that have occurred at h (the implementation's `num_halvings` or equivalent).
- `block_subsidy`: total block subsidy at h in zatoshi (before any split).
- `funding_stream_value`, key = receiver name (normalize to the ZIP 214 receiver names, e.g. `ZcashFoundation`,
  `MajorGrants`/`FPF`/`ZCG`, `Deferred`/`Lockbox`, etc. — use the implementation's enum name as-is and note the
  mapping in NOTES): the funding stream value for that receiver at h (emit `0` when inactive so change points
  are visible).
- `funding_stream_address`, key = receiver name: the address the coinbase must pay at h (string encoding),
  or `none`.
- `lockbox_value`: the amount directed to the lockbox / deferred pool at h.
- `miner_subsidy`: subsidy minus all funding streams and lockbox at h (before fees and before NSM reissuance).
- `testnet_min_difficulty_gap_secs`: for Testnet scenarios only, the minimum gap (in seconds) between the
  candidate block time and the previous block time above which the block at h may use minimum difficulty.
- `nsm_reissuance`, key = parent NSM balance B in zatoshi as decimal string, for
  B in {`0`, `1`, `100000000`, `1000000000000`, `21000000000000`, `500000000000000`, `1050000000000000`}:
  the ZIP 237 NSM reissuance amount the block at h must/may pay given parent NSM balance B. If the function
  needs other inputs (e.g. issued supply), note what you passed in NOTES and pass consistent values:
  issued supply = 2_100_000_000_000_000 - B.

To keep runtime acceptable, it is fine to evaluate the expensive quantities (`funding_stream_address`,
`nsm_reissuance`) only at heights where one of the cheap quantities changed OR where `h % 1000 == 0`, PLUS
every height in `[A-10, A+10]`, PLUS a full scan of the ZIP 207 address-period boundaries computed by the
implementation itself; state in NOTES exactly which heights you covered. Use `--release` if the debug build
is too slow (budget: each full scan should finish in < 15 minutes).

### B. Structural boundaries (emit once, `height: null`)

- `halving_heights`: comma-separated heights of the first 6 halvings as computed by the implementation
  (key `""`).
- `funding_stream_ranges`, key = receiver: `start..end` height range(s) for each funding stream, as the
  implementation defines them for this scenario.
- `nsm_reissuance_start_height`: NSM_REISSUANCE_HEIGHT / DEPLOYMENT_BLOCK_HEIGHT for this scenario.
- `address_period_boundaries`, key = receiver: first 10 heights at/after A where the address index changes.

### C. Fee split (ZIP 235), per height in {A-1, A, A+1}

- `fee_burn`, key = total block fees F (decimal) for F in
  {0,1,2,3,4,5,6,7,8,9,10,11,99,100,101,999,12345,100000001,1099511627783}: the amount removed from
  circulation (to NSM) by ZIP 235.
- `miner_fee_share`, same keys: the fee amount the miner may claim.

### D. Difficulty (ZIP 218 + Testnet min-difficulty), `testnet-A1`, `testnet-real`, `mainnet-real` and `regtest-R`

Build a deterministic synthetic header chain and compute the expected difficulty threshold with the
implementation's production difficulty code (e.g. `AdjustedDifficulty::new_from_header_time(...)` +
`expected_difficulty_threshold()` or equivalent):
- Start from `bits = 0x1f07ffff` for public Testnet and Mainnet and from the network's PoW limit in compact form for
  Regtest (note the value), for heights `A-200 .. A+300`. For `regtest-R` use A = 300 but start at height 1
  (i.e. heights `1 .. 600`), with times starting at 1_760_000_000.
- Block times: `t(A-200) = 1_760_000_000`; for each subsequent height h, interval
  `d(h) = [37, 75, 90, 60, 150, 20, 25, 25, 10, 500, 75, 30][h % 12]` seconds before A (heights < A), and
  `[12, 25, 30, 20, 50, 25, 8, 40, 25, 480, 25, 460][h % 12]` at/after A.
- `bits(h)` for the synthetic chain = the implementation's OWN computed expected threshold for h (i.e. feed
  your outputs forward), except bits(A-200..A-200+window) = starting bits. Both harnesses fix the 29 heights
  start..start+28 (the pre-NU7 averaging window of 17 plus the 11-block median span) and compute from start+29.
- Emit `expected_bits` (compact hex) for every h in `[A-150, A+300]`, and `is_min_difficulty_block` (true/false)
  for the Testnet scenario.

Also emit the raw per-height `median_time_past` if the implementation exposes it.

### E. Branch id / tx version validity, per height in {A-1, A}

- `tx_version_allowed`, key in {`v4`, `v5`, `v6`, `v4-coinbase`, `v5-coinbase`, `v6-coinbase`}: whether the
  implementation's version-vs-upgrade check accepts that version at that height (only if there is a pure
  function; otherwise `MISSING`).

### D2. Difficulty away from the PoW limit, `testnet-A1`, `testnet-real` and `mainnet-real`

Section D mostly saturates at the Testnet PoW limit. D2 builds a second chain the same way, but:
- starting bits `0x1d00ffff`;
- block intervals that average the target spacing, `[60, 90, 75, 70, 80, 75, 65, 85, 75, 72, 78, 75][h % 12]`
  before A and `[20, 30, 25, 23, 27, 25, 22, 28, 25, 24, 26, 25][h % 12]` at/after A;
- except for a slow and then a fast stretch on each side of A, to reach the adjustment bounds:
  225 s for h in `[A-100, A-60)`, 25 s for h in `[A-60, A-30)`, 75 s for h in `[A+100, A+150)`, and
  8 s for h in `[A+150, A+200)`.

No interval exceeds the minimum-difficulty gap. Emit `expected_bits_d2` and `median_time_past_d2` for
every h in `[A-150, A+300]`.
