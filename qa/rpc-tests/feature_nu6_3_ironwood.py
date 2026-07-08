#!/usr/bin/env python3
# Copyright (c) 2026 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    NU5_BRANCH_ID,
    NU6_3_BRANCH_ID,
    assert_equal,
    assert_raises_message,
    get_coinbase_address,
    nuparams,
    start_node,
    start_nodes,
    stop_node,
    wait_and_assert_operationid_status,
)
from test_framework.authproxy import JSONRPCException
from test_framework.zip317 import conventional_fee

from decimal import Decimal


IRONWOOD_TREE_EMPTY_ROOT = "ae2935f1dfd8a24aed7c70df7de3a668eb7a49b1319880dde2bbd9031ae5d82f"
# Must match IRONWOOD_WALLET_UNSUPPORTED in src/wallet/wallet_tx_builder.cpp. The
# wording is permanent unsupport by design; it must never say "not yet available".
IRONWOOD_WALLET_UNSUPPORTED = (
    "zcashd does not support the Ironwood pool, and Orchard payments (including spends of "
    "existing Orchard notes) are unsupported from NU6.3. Use transparent or Sapling funds "
    "with zcashd, or a Z3-stack wallet for shielded payments."
)
# NU5 activates before NU6.3 so the test can create Orchard notes pre-NU6.3 and
# then prove they are frozen by the wallet guardrails post-activation. The gap
# must be comfortably larger than the tx-expiring-soon threshold: wallet txs
# built in the window have their expiry clamped to NU6_3_ACTIVATION - 1, and
# the mempool rejects txs expiring within 3 blocks.
NU5_ACTIVATION = 201
NU6_3_ACTIVATION = 220
COMMON_ARGS = [
    nuparams(NU5_BRANCH_ID, NU5_ACTIVATION),
    nuparams(NU6_3_BRANCH_ID, NU6_3_ACTIVATION),
    '-experimentalfeatures',
    '-lightwalletd',
    # The unshielding guardrail check below needs a plain transparent recipient.
    '-allowdeprecated=getnewaddress',
]
NODE_ARGS = COMMON_ARGS + ['-reindex']
RESTART_NODE_ARGS = COMMON_ARGS


class IronwoodRpcSurfaceTest(BitcoinTestFramework):
    def __init__(self):
        super().__init__()
        self.num_nodes = 1

    def setup_nodes(self):
        # The cached regtest chain starts at height 200.
        return start_nodes(self.num_nodes, self.options.tmpdir, extra_args=[NODE_ARGS] * self.num_nodes)

    def setup_network(self, split=False):
        self.nodes = self.setup_nodes()
        self.is_network_split = False
        self.sync_all()

    def assert_has_ironwood_pool(self, value_pools):
        ironwood = next(pool for pool in value_pools if pool['id'] == 'ironwood')
        assert 'monitored' in ironwood
        return ironwood

    def run_test(self):
        node = self.nodes[0]
        assert_equal(node.getblockcount(), 200)

        pre_activation_block = node.getblock('200')
        assert 'finalironwoodroot' not in pre_activation_block
        assert 'ironwood' not in pre_activation_block['trees']
        self.assert_has_ironwood_pool(pre_activation_block['valuePools'])
        self.assert_has_ironwood_pool(node.getblockchaininfo()['valuePools'])

        pre_activation_treestate = node.z_gettreestate('200')
        assert 'ironwood' not in pre_activation_treestate

        # Activate NU5 and give the wallet an Orchard note while that is still
        # possible, so the post-NU6.3 freeze of existing Orchard funds can be
        # exercised below.
        node.generate(1)
        self.sync_all()
        assert_equal(node.getblockcount(), NU5_ACTIVATION)

        acct = node.z_getnewaccount()['account']
        orchard_ua = node.z_getaddressforaccount(acct, ['orchard'])['address']
        fee = conventional_fee(3)
        orchard_funds = Decimal('10') - fee
        opid = node.z_sendmany(
            get_coinbase_address(node),
            [{"address": orchard_ua, "amount": orchard_funds}],
            1,
            fee,
            'AllowRevealedSenders')
        wait_and_assert_operationid_status(node, opid)
        node.generate(1)
        self.sync_all()

        balances = node.z_getbalanceforaccount(acct)['pools']
        assert_equal(balances['orchard']['valueZat'], int(orchard_funds * 100000000))

        node.generate(NU6_3_ACTIVATION - node.getblockcount() - 1)
        assert_equal(node.getblockcount(), NU6_3_ACTIVATION - 1)
        assert 'finalironwoodroot' not in node.getblock(str(NU6_3_ACTIVATION - 1))

        node.generate(1)
        assert_equal(node.getblockcount(), NU6_3_ACTIVATION)

        activation_block = node.getblock(str(NU6_3_ACTIVATION))
        assert_equal(activation_block['finalironwoodroot'], IRONWOOD_TREE_EMPTY_ROOT)
        assert_equal(activation_block['trees']['ironwood']['size'], 0)
        self.assert_has_ironwood_pool(activation_block['valuePools'])
        ironwood_pool = self.assert_has_ironwood_pool(node.getblockchaininfo()['valuePools'])
        assert_equal(ironwood_pool['chainValueZat'], 0)

        activation_treestate = node.z_gettreestate(str(NU6_3_ACTIVATION))
        assert_equal(activation_treestate['ironwood']['commitments']['finalRoot'], IRONWOOD_TREE_EMPTY_ROOT)
        assert_equal(activation_treestate['ironwood']['commitments']['finalState'], "000000")
        assert 'skipHash' not in activation_treestate['ironwood']

        subtrees = node.z_getsubtreesbyindex('ironwood', 0)
        assert_equal(subtrees['pool'], 'ironwood')
        assert_equal(subtrees['start_index'], 0)
        assert_equal(len(subtrees['subtrees']), 0)

        # Exercise disconnect/reconnect across the activation boundary.
        activation_hash = node.getblockhash(NU6_3_ACTIVATION)
        post_activation_hashes = node.generate(2)
        tip_hash = post_activation_hashes[-1]
        tip_height = node.getblockcount()
        assert_equal(node.getblock(tip_hash)['finalironwoodroot'], IRONWOOD_TREE_EMPTY_ROOT)

        # Restart with post-NU6.3 chainstate on disk to cover the Ironwood data-version gate.
        stop_node(node, 0)
        self.nodes[0] = start_node(0, self.options.tmpdir, RESTART_NODE_ARGS)
        node = self.nodes[0]
        assert_equal(node.getbestblockhash(), tip_hash)
        assert_equal(node.getblock(tip_hash)['finalironwoodroot'], IRONWOOD_TREE_EMPTY_ROOT)

        node.invalidateblock(activation_hash)
        assert_equal(node.getblockcount(), NU6_3_ACTIVATION - 1)
        assert 'finalironwoodroot' not in node.getblock(str(NU6_3_ACTIVATION - 1))
        assert 'ironwood' not in node.z_gettreestate(str(NU6_3_ACTIVATION - 1))

        node.reconsiderblock(activation_hash)
        assert_equal(node.getbestblockhash(), tip_hash)
        assert_equal(node.getblockcount(), tip_height)
        assert_equal(node.getblock(tip_hash)['finalironwoodroot'], IRONWOOD_TREE_EMPTY_ROOT)

        # Wallet guardrails (permanent behavior): any post-NU6.3 wallet operation
        # involving Orchard fails at preparation time with the documented
        # permanent-unsupported error, never as a proving failure or an
        # unbalanced bundle.

        # Sending TO an Orchard receiver fails (z_sendmany prepares inside the
        # async operation, so the error surfaces via the operation status).
        # Spend a whole coinbase UTXO (amount + fee = 10) so the transaction has
        # no transparent change: the Ironwood guardrail runs after input
        # selection, and transparent change would fail selection first with an
        # unrelated privacy-policy error.
        opid = node.z_sendmany(
            get_coinbase_address(node),
            [{"address": orchard_ua, "amount": Decimal('10') - fee}],
            1,
            fee,
            'AllowRevealedSenders')
        wait_and_assert_operationid_status(node, opid, 'failed', IRONWOOD_WALLET_UNSUPPORTED)

        # Spending FROM existing Orchard funds fails, even to unshield them.
        # This is the permanent fund freeze called out in the release notes.
        taddr = node.getnewaddress()
        opid = node.z_sendmany(
            orchard_ua,
            [{"address": taddr, "amount": Decimal('1')}],
            1,
            None,
            'AllowRevealedRecipients')
        wait_and_assert_operationid_status(node, opid, 'failed', IRONWOOD_WALLET_UNSUPPORTED)
        balances = node.z_getbalanceforaccount(acct)['pools']
        assert_equal(balances['orchard']['valueZat'], int(orchard_funds * 100000000))

        # z_shieldcoinbase and z_mergetoaddress prepare synchronously in the RPC
        # handler, so the same guardrail surfaces as an immediate RPC error.
        assert_raises_message(
            JSONRPCException,
            IRONWOOD_WALLET_UNSUPPORTED,
            node.z_shieldcoinbase,
            '*',
            orchard_ua)
        # z_mergetoaddress does not merge coinbase UTXOs, and all of the wallet's
        # transparent funds so far are coinbase, so create a regular UTXO first
        # (regtest permits transparent coinbase spends); otherwise the RPC fails
        # with "Could not find any funds to merge" before reaching the guardrail.
        node.sendtoaddress(taddr, Decimal('1'))
        node.generate(1)
        assert_raises_message(
            JSONRPCException,
            IRONWOOD_WALLET_UNSUPPORTED,
            node.z_mergetoaddress,
            ['ANY_TADDR'],
            orchard_ua,
            None,
            None,
            None,
            None,
            # Merging transparent funds to a shielded address reveals senders and
            # amounts; the policy must permit that or resolution fails before the
            # Ironwood guardrail is reached.
            'AllowRevealedSenders')


if __name__ == '__main__':
    IronwoodRpcSurfaceTest().main()
