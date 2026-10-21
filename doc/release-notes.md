(note: this is a temporary file, to be added-to by anybody, and moved to
release-notes at release time)

Notable changes
===============

NU7 consensus support
---------------------

This release adds consensus support for the NU7 network upgrade (the ZIP 259
set, consensus branch ID `0x77190ad9`). NU7 activated on Testnet at height
4,465,026 on 2026-10-04, and zebra-compat-v1.2.0 and earlier sidecars cannot
follow Testnet past that height. Mainnet NU7 is scheduled at height 3,543,405,
matching Zebra's Mainnet schedule. Upgrade before that height to keep following
Mainnet.

Run this release behind Zebra 7.0.0 or later. From NU7 activation, Zebra and
the sidecar each disconnect a peer that advertises an older protocol version,
so both must be upgraded.

From NU7:

- **25-second blocks (ZIP 218).** The target spacing drops from 75 to 25
  seconds and the difficulty averaging window grows from 17 to 102 blocks. The
  halving interval triples and the block subsidy is divided by 3, so issuance
  per unit of time is unchanged, and the funding streams and lockbox follow the
  new schedule.
- **Per-block shielded limits (ZIP 218).** A block holds at most 330 Orchard
  actions, 330 Ironwood actions and 300 Sapling spends and outputs, within a
  combined budget of 330, and no Sprout JoinSplits.
- **Fee burning (ZIP 235).** 60% of each block's transaction fees are burned:
  the coinbase may claim only `F - floor(6F/10)` of the block's fees `F`.
- **Network Sustainability Mechanism (ZIP 237).** Burned fees are added to the
  NSM balance, which is paid back out to miners gradually from the reissuance
  height. `getblockchaininfo` reports it as `nsmValueBalanceZat`.
- **No v4 transactions (ZIP 2003).** v4 transactions, coinbase included, are
  invalid.

The protocol version is now 170190, the Mainnet NU7 version. The Rust crates
are Zakura Common 2.2.0, Zakura's forks of the Zcash protocol and cryptography
crates. A CI job checks zcashd's NU7 consensus values against Zakura's.

Action required for Sprout users BEFORE NU7 activation
------------------------------------------------------

- **Sprout funds become permanently unspendable at NU7.** ZIP 2003 makes v4
  transactions, the only ones that can spend Sprout notes, invalid. Move any
  Sprout funds to a Sapling or transparent address well before the activation
  height, with `z_sendmany` or the Sprout-to-Sapling migration
  (`z_setmigration`).
- **Do not rely on the migration's last rounds.** The migration stops creating
  transactions a few blocks before NU7, and skips any round whose transactions
  could still be unmined when NU7 activates.

Wallet changes from NU7
-----------------------

- Wallet transactions are v5, even with `-preferredtxversion=4`.
- The default expiry delta becomes 120 blocks, the same time as 40 blocks at
  the old spacing. `-txexpirydelta` still overrides it.
- Sprout spends and the Sprout-to-Sapling migration are refused with an error.
- A transaction with more than 300 Sapling spends and outputs is refused,
  because no block could hold it.

Deeper reorgs: wallet and pruning impact
----------------------------------------

Since zebra-compat-v1.2.0, zcashd accepts reorgs of up to 1000 blocks
(previously 99), matching Zebra's `MAX_BLOCK_REORG_HEIGHT`, so the sidecar
follows every reorg Zebra does. This has costs for wallets and pruned nodes:

- **Larger wallets.** The wallet keeps 1001 cached witnesses for each unspent
  Sprout or Sapling note (previously 100), about 10 times the memory and disk
  per note, and its Orchard note commitment tree keeps 1001 checkpoints. Since
  anyone who knows a Sapling address can send it notes, a wallet sent many small
  "dust" notes grows accordingly: at least about 600 MiB per ZEC the sender
  spends on fees.
- **One-way wallet upgrade.** zebra-compat-v1.1.0 and earlier abort on a
  wallet written by zebra-compat-v1.2.0 or later, once one of its notes has
  more than 100 cached witnesses. This corrects the zebra-compat-v1.2.0 release
  notes, which said that downgrading still worked. Back up `wallet.dat` before
  upgrading from zebra-compat-v1.1.0 or earlier.
- **Reorg tolerance builds up after upgrading.** A wallet upgraded from
  zebra-compat-v1.1.0 or earlier starts with only the last 100 blocks of
  history and reaches the full 1000 over the next 900 or so blocks. A reorg as
  deep as its history or deeper, in that window, stops the node; restart with
  `-rescan`.
- **Pruned nodes keep more blocks.** A pruned node keeps the last 1001 blocks
  (previously 288). With large blocks this can exceed the 550 MiB minimum
  `-prune` target, so allow at least 2 GiB. A recently upgraded pruned node has
  already pruned older blocks, so it cannot follow a reorg deeper than the
  blocks it has kept until it holds 1001 again.

Mempool around network upgrades
-------------------------------

Zebra penalizes a peer that relays a transaction it rejects (it bans the peer
once its misbehavior score reaches 100), and Zebra may be a few blocks ahead of
the sidecar. The mempool therefore also checks each transaction at the highest
height Zebra could check it at (3 blocks past the next block, or past the block
after Zebra's best header), and drops any it holds that are no longer valid
there.

So in the last few blocks before an upgrade activates, the mempool refuses new
transactions with `tx-invalid-at-relay-height`, and drops the ones it holds.
From activation, it accepts transactions for the new upgrade. From NU7 it also
refuses any transaction that exceeds a ZIP 218 per-block limit.

Mining
------

Block templates follow the ZIP 218 per-block limits, the ZIP 235 fee split and
ZIP 237 reissuance, and `getblocksubsidy` reports the NU7 amounts. Once
reissuance is active, `getblocktemplate` no longer builds the next block's
coinbase in advance, since it depends on that block's NSM balance.

Other fixes
-----------

- v6 transactions are standard from NU6.3. Previously the mempool rejected
  every v6 transaction relayed to it as `nu5-version`.
- Fixed a sync stall: when Zebra stopped answering a `getdata` at its 1 MB
  limit, the sidecar waited for the download timeout on each remaining block,
  and each wait was longer than the last.

NU6.3 (Ironwood) consensus support; no wallet support, ever
-----------------------------------------------------------

This release adds full consensus and node support for the NU6.3 network
upgrade, which introduces the Ironwood shielded pool (testnet activation
height 4134000; mainnet activation height 3428143). zcashd
validates and follows the chain across NU6.3 activation, including blocks
containing v6 transactions with Ironwood bundles, and exposes node-level
observability for the new pool: an "ironwood" value pool in `getblockchaininfo`
and `getblock`, `finalironwoodroot` in `getblock`, an ironwood section in
`z_gettreestate` and `z_getsubtreesbyindex`, and an "ironwood" bundle in
verbose `getrawtransaction`/`decoderawtransaction` output for v6 transactions.

zcashd will NEVER support the Ironwood pool in its wallet, and will never
construct v6 transactions. This is a permanent decision, not a deferral.
Shielded wallet functionality beyond Sapling is out of scope for zcashd;
users who need it should migrate to a Z3-stack wallet. zcashd wallets remain
fully supported for transparent and Sapling funds. Block production for
post-NU6.3 shielded coinbase is handled by Zebra; from NU6.3 activation,
zcashd's internal miner refuses to start with an Orchard `-mineraddress`
(use a transparent or Sapling address, or mine with Zebra).

Action required for Orchard users BEFORE NU6.3 activation
---------------------------------------------------------

From NU6.3 activation, the Orchard protocol itself no longer permits payments
to third-party addresses, and the zcashd wallet rejects ALL transactions that
would spend or receive Orchard funds, including unshielding. This means:

- **Orchard funds held in a zcashd wallet become unspendable through zcashd
  once NU6.3 activates.** Move any Orchard funds (for example with `z_sendmany`
  to a transparent or Sapling address, or migrate to a Z3-stack wallet) BEFORE
  the activation height, on both testnet and mainnet.

- **Stop publishing unified addresses that contain an Orchard receiver.**
  After activation, an Ironwood-capable wallet paying a zcashd-generated
  unified address will send funds to the Orchard receiver's corresponding
  Ironwood address. zcashd cannot detect, display, or spend Ironwood funds;
  such payments will appear to vanish. Generate replacement addresses without
  an Orchard receiver (e.g. `z_getaddressforaccount` with `["sapling"]` or
  `["p2pkh"]` receiver types), or migrate to a Z3-stack wallet.

Wallet operations that would involve Orchard or Ironwood at NU6.3 heights
(`z_sendmany`, `z_shieldcoinbase`, `z_mergetoaddress`) fail at preparation
time with a clear error explaining these alternatives.

P2P sidecar hard-lock
---------------------

This compatibility build is intended to run as a wallet/RPC sidecar behind
Zakura. It now refuses to start unless exactly one `-connect=<zakura-address>`
peer is configured, never opens a P2P listener, and rejects `-addnode`,
`-seednode`, `-bind`, and `-whitebind`. The `addnode` RPC is not registered and
returns `Method not found`.

This makes the sidecar's P2P isolation a binary guarantee. Zcashd can only dial
the single Zakura peer supplied at startup, and no public peer can connect
inbound to zcashd.

After publishing this sidecar release, update Zakura's pinned compatibility
manifest and installer checksums to this build.

Compatibility support policy
----------------------------

Unlike upstream zcashd, this compatibility build has no scheduled End of Life.
It no longer contains an End-of-Support height, emits height-based support
warnings, requires the upstream deprecation acknowledgment at startup, or
returns End-of-Service data from `getdeprecationinfo`.

Valargroup reserves the right to announce a future End of Life through a Zakura
compatibility release. Integrators are encouraged to migrate to
[Zakura](https://github.com/zakura-core/zakura) and monitor its
[CHANGELOG](https://github.com/zakura-core/zakura/blob/main/CHANGELOG.md).
