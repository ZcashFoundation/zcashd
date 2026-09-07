#!/usr/bin/env python3
# Copyright (c) 2017 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

#
# Test reorg limit
#

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    check_node,
    connect_nodes_bi,
    start_node,
    sync_blocks,
)

import tempfile
from time import sleep

# Must match MAX_REORG_LENGTH in src/main.h.
MAX_REORG_LENGTH = 1000

def check_stopped(i, timeout=240):
    stopped = False
    for x in range(1, timeout):
        ret = check_node(i)
        if ret is None:
            sleep(1)
        else:
            stopped = True
            break
    return stopped

class ReorgLimitTest(BitcoinTestFramework):

    def setup_nodes(self):
        self.log_stderr = tempfile.SpooledTemporaryFile(max_size=2**16)

        nodes = []
        nodes.append(start_node(0, self.options.tmpdir, stderr=self.log_stderr))
        nodes.append(start_node(1, self.options.tmpdir))
        nodes.append(start_node(2, self.options.tmpdir))
        nodes.append(start_node(3, self.options.tmpdir))

        return nodes

    def run_test(self):
        assert(self.nodes[0].getblockcount() == 200)
        assert(self.nodes[2].getblockcount() == 200)

        self.split_network()

        print("Test the maximum-allowed reorg:")
        print("Mine %d blocks on Node 0" % MAX_REORG_LENGTH)
        self.nodes[0].generate(MAX_REORG_LENGTH)
        assert(self.nodes[0].getblockcount() == 200 + MAX_REORG_LENGTH)
        assert(self.nodes[2].getblockcount() == 200)

        print("Mine competing %d blocks on Node 2" % (MAX_REORG_LENGTH + 1))
        self.nodes[2].generate(MAX_REORG_LENGTH + 1)
        assert(self.nodes[0].getblockcount() == 200 + MAX_REORG_LENGTH)
        assert(self.nodes[2].getblockcount() == 201 + MAX_REORG_LENGTH)

        print("Connect nodes to force a reorg")
        connect_nodes_bi(self.nodes, 0, 2)
        self.is_network_split = False
        # A reorg of ~MAX_REORG_LENGTH blocks takes well over the default timeout.
        sync_blocks(self.nodes, timeout=600)

        print("Check Node 0 is still running and on the correct chain")
        assert(self.nodes[0].getblockcount() == 201 + MAX_REORG_LENGTH)

        self.split_network()

        print("Test the minimum-rejected reorg:")
        print("Mine %d blocks on Node 0" % (MAX_REORG_LENGTH + 1))
        self.nodes[0].generate(MAX_REORG_LENGTH + 1)
        assert(self.nodes[0].getblockcount() == 202 + 2 * MAX_REORG_LENGTH)
        assert(self.nodes[2].getblockcount() == 201 + MAX_REORG_LENGTH)

        print("Mine competing %d blocks on Node 2" % (MAX_REORG_LENGTH + 2))
        self.nodes[2].generate(MAX_REORG_LENGTH + 2)
        assert(self.nodes[0].getblockcount() == 202 + 2 * MAX_REORG_LENGTH)
        assert(self.nodes[2].getblockcount() == 203 + 2 * MAX_REORG_LENGTH)

        try:
            print("Sync nodes to force a reorg")
            connect_nodes_bi(self.nodes, 0, 2)
            self.is_network_split = False
            # sync_blocks uses RPC calls to wait for nodes to be synced, so don't
            # call it here, because it will have a non-specific connection error
            # when Node 0 stops. Instead, we explicitly check for the process itself
            # to stop.

            print("Check Node 0 is no longer running")
            assert(check_stopped(0))

            # Check that node 0 stopped for the expected reason.
            self.log_stderr.seek(0)
            stderr = self.log_stderr.read().decode('utf-8')
            expected_msg = "A block chain reorganization has been detected that would roll back %d blocks!" % (MAX_REORG_LENGTH + 1)
            if expected_msg not in stderr:
                raise AssertionError("Expected error \"" + expected_msg + "\" not found in:\n" + stderr)
        finally:
            self.log_stderr.close()
            # Dummy stop to enable the test to tear down
            self.nodes[0].stop = lambda: True

if __name__ == '__main__':
    ReorgLimitTest().main()
