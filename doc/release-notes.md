(note: this is a temporary file, to be added-to by anybody, and moved to
release-notes at release time)

Notable changes
===============

NU6.3 (Ironwood) consensus support; no wallet support, ever
-----------------------------------------------------------

This release adds full consensus and node support for the NU6.3 network
upgrade, which introduces the Ironwood shielded pool (testnet activation
height 4134000; the mainnet activation height is not yet scheduled). zcashd
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

