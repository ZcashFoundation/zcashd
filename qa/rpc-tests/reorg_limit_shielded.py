#!/usr/bin/env python3
# Copyright (c) 2026 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

#
# Test that a reorg deeper than the old 99-block limit is survivable by a wallet
# that holds Sapling and Orchard notes.
#
# Before MAX_REORG_LENGTH was raised to 1000, a reorg of this depth shut the node
# down. Raising only the node threshold would have converted that clean shutdown
# into an assertion failure, because the Sapling/Sprout witness cache
# (WITNESS_CACHE_SIZE) and the Orchard note commitment tree checkpoints
# (MAX_CHECKPOINTS) were both sized for 100 blocks:
#
#  - `CWallet::DecrementNoteWitnesses` asserts `nWitnessCacheSize > 0`, and
#  - it wraps `OrchardWallet::Rewind` (which fails with InsufficientCheckpoints
#    once the checkpoints run out) in an `assert()`.
#
# So this test only passes if all three limits were raised together.
#

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    BLOSSOM_BRANCH_ID,
    HEARTWOOD_BRANCH_ID,
    CANOPY_BRANCH_ID,
    NU5_BRANCH_ID,
    assert_equal,
    assert_true,
    check_node,
    get_coinbase_address,
    nuparams,
    start_nodes,
    sync_blocks,
    wait_and_assert_operationid_status,
)
from test_framework.zip317 import ZIP_317_FEE

from decimal import Decimal

# The depth of the reorg to exercise. This must be greater than the old
# MAX_REORG_LENGTH of 99 (and greater than the old WITNESS_CACHE_SIZE and
# MAX_CHECKPOINTS of 100), and no greater than the current MAX_REORG_LENGTH of
# 1000. It is kept well below 1000 so that the test stays fast; `reorg_limit.py`
# covers the boundary itself.
REORG_DEPTH = 150

# Height at which NU5 activates, so that Orchard notes are available.
NU5_HEIGHT = 10


def pool_balance(node, account, pool):
    pools = node.z_getbalanceforaccount(account, 1)['pools']
    return pools[pool]['valueZat'] if pool in pools else 0


class ReorgLimitShieldedTest(BitcoinTestFramework):
    def __init__(self):
        super().__init__()
        self.num_nodes = 4
        self.cache_behavior = 'clean'

    def setup_nodes(self):
        return start_nodes(self.num_nodes, self.options.tmpdir, extra_args=[[
            nuparams(BLOSSOM_BRANCH_ID, 1),
            nuparams(HEARTWOOD_BRANCH_ID, 5),
            nuparams(CANOPY_BRANCH_ID, 5),
            nuparams(NU5_BRANCH_ID, NU5_HEIGHT),
            '-nurejectoldversions=false',
        ]] * self.num_nodes)

    def run_test(self):
        node = self.nodes[0]

        # Activate NU5 and mature enough coinbase to shield.
        print("Mining 120 blocks to activate NU5 and mature coinbase")
        node.generate(120)
        self.sync_all()

        # A Sapling-only and an Orchard-only unified address, so that the reorg
        # has to walk back both the Sapling witness cache and the Orchard note
        # commitment tree checkpoints.
        sapling_acct = node.z_getnewaccount()['account']
        sapling_addr = node.z_getaddressforaccount(sapling_acct, ['sapling'])
        assert_equal(set(sapling_addr['receiver_types']), set(['sapling']))
        sapling_ua = sapling_addr['address']

        orchard_acct = node.z_getnewaccount()['account']
        orchard_addr = node.z_getaddressforaccount(orchard_acct, ['orchard'])
        assert_equal(set(orchard_addr['receiver_types']), set(['orchard']))
        orchard_ua = orchard_addr['address']

        coinbase_addr = get_coinbase_address(node)
        for dest in [sapling_ua, orchard_ua]:
            res = node.z_shieldcoinbase(
                coinbase_addr, dest, ZIP_317_FEE, 1, None, 'AllowRevealedSenders')
            wait_and_assert_operationid_status(node, res['opid'])
            node.generate(1)
            self.sync_all()

        sapling_balance = pool_balance(node, sapling_acct, 'sapling')
        orchard_balance = pool_balance(node, orchard_acct, 'orchard')
        assert_true(sapling_balance > 0, "Sapling note was not created")
        assert_true(orchard_balance > 0, "Orchard note was not created")

        common_height = node.getblockcount()
        common_tip = node.getbestblockhash()

        print("Splitting the network at height %d" % common_height)
        self.split_network()

        # Node 0 builds the chain that will be discarded; node 2 builds the
        # chain that wins. Both branches are entirely above the block that mined
        # the shielded notes, so the notes themselves survive the reorg and only
        # their witnesses have to be rolled back.
        print("Mining %d blocks on node 0" % REORG_DEPTH)
        node.generate(REORG_DEPTH)
        print("Mining competing %d blocks on node 2" % (REORG_DEPTH + 1))
        self.nodes[2].generate(REORG_DEPTH + 1)
        self.sync_all()

        assert_equal(node.getblockcount(), common_height + REORG_DEPTH)
        assert_equal(self.nodes[2].getblockcount(), common_height + REORG_DEPTH + 1)
        assert_true(node.getbestblockhash() != self.nodes[2].getbestblockhash(),
                    "Split chains have not diverged!")

        print("Re-joining the network to force a %d-block reorg on node 0" % REORG_DEPTH)
        self.join_network()
        # A reorg this deep takes longer than the default sync timeout.
        sync_blocks(self.nodes, timeout=600)

        print("Checking node 0 is still running and on the winning chain")
        assert_equal(check_node(0), None)
        assert_equal(node.getblockcount(), common_height + REORG_DEPTH + 1)
        assert_equal(node.getbestblockhash(), self.nodes[2].getbestblockhash())

        # The notes were mined before the fork point, so they must still be
        # present and their witnesses must have been rolled back correctly.
        print("Checking shielded balances survived the reorg")
        assert_equal(pool_balance(node, sapling_acct, 'sapling'), sapling_balance)
        assert_equal(pool_balance(node, orchard_acct, 'orchard'), orchard_balance)

        # Spending proves the rolled-back witnesses are still valid against the
        # post-reorg anchor, which is what the witness cache and the Orchard
        # checkpoints exist to guarantee.
        print("Checking the surviving notes are still spendable")
        recipient_acct = node.z_getnewaccount()['account']
        recipient = node.z_getaddressforaccount(recipient_acct, ['sapling'])['address']
        for (source, balance) in [(sapling_ua, sapling_balance), (orchard_ua, orchard_balance)]:
            # Leave a generous margin for the ZIP 317 fee.
            amount = Decimal(balance - 100000) / Decimal('1e8')
            opid = node.z_sendmany(
                source, [{'address': recipient, 'amount': amount}], 1, ZIP_317_FEE, 'AllowRevealedAmounts')
            wait_and_assert_operationid_status(node, opid)
            node.generate(1)
            self.sync_all()

        assert_true(pool_balance(node, recipient_acct, 'sapling') > 0,
                    "Post-reorg spend did not arrive")
        print("Node 0 survived a %d-block reorg with shielded notes intact" % REORG_DEPTH)
        assert_true(common_tip != node.getbestblockhash())


if __name__ == '__main__':
    ReorgLimitShieldedTest().main()
