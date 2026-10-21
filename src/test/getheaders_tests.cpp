// Copyright (c) 2026 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "arith_uint256.h"
#include "chainparams.h"
#include "crypto/equihash.h"
#include "hash.h"
#include "main.h"
#include "net.h"
#include "pow.h"
#include "script/script.h"
#include "serialize.h"
#include "streams.h"
#include "util/time.h"
#include "version.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

#include <memory>

namespace {

CAddress TestAddress(uint32_t i)
{
    struct in_addr s;
    s.s_addr = i;
    return CAddress(CService(CNetAddr(s), Params().GetDefaultPort()));
}

struct MockTimeGuard {
    MockTimeGuard()
    {
        int64_t nStartTime = GetTime();
        FixedClock::SetGlobal();
        FixedClock::Instance()->Set(std::chrono::seconds(nStartTime));
    }

    ~MockTimeGuard()
    {
        SystemClock::SetGlobal();
    }

    void AdvanceHeadersTimeout()
    {
        FixedClock::Instance()->Set(std::chrono::seconds(GetTime() + HEADERS_RESPONSE_TIMEOUT + 1));
    }

    void AdvanceSeconds(int64_t seconds)
    {
        FixedClock::Instance()->Set(std::chrono::seconds(GetTime() + seconds));
    }
};

struct InitialBlockDownloadGuard {
    InitialBlockDownloadGuard() : previous(TestSetIBD(true)) {}
    ~InitialBlockDownloadGuard() { TestSetIBD(previous); }

private:
    bool previous;
};

struct GetHeadersPayload {
    CBlockLocator locator;
    uint256 hashStop;
};

std::string OutboundCommand(const CSerializeData& msg)
{
    CDataStream ss(msg, SER_NETWORK, PROTOCOL_VERSION);
    CMessageHeader hdr(Params().MessageStart());
    ss >> hdr;
    return hdr.GetCommand();
}

std::vector<GetHeadersPayload> GetHeadersMessages(CNode& node)
{
    std::vector<GetHeadersPayload> messages;
    LOCK(node.cs_vSend);
    for (const CSerializeData& msg : node.vSendMsg) {
        CDataStream ss(msg, SER_NETWORK, PROTOCOL_VERSION);
        CMessageHeader hdr(Params().MessageStart());
        ss >> hdr;
        if (hdr.GetCommand() != "getheaders") {
            continue;
        }
        GetHeadersPayload payload;
        ss >> payload.locator >> payload.hashStop;
        messages.push_back(payload);
    }
    return messages;
}

/** Returns the inventory of each getdata message queued for `node`, in send order. */
std::vector<std::vector<CInv>> GetDataMessages(CNode& node)
{
    std::vector<std::vector<CInv>> messages;
    LOCK(node.cs_vSend);
    for (const CSerializeData& msg : node.vSendMsg) {
        CDataStream ss(msg, SER_NETWORK, PROTOCOL_VERSION);
        CMessageHeader hdr(Params().MessageStart());
        ss >> hdr;
        if (hdr.GetCommand() != "getdata") {
            continue;
        }
        std::vector<CInv> invs;
        ss >> invs;
        messages.push_back(invs);
    }
    return messages;
}

void CheckSameGetHeaders(const GetHeadersPayload& a, const GetHeadersPayload& b)
{
    BOOST_CHECK(a.locator == b.locator);
    BOOST_CHECK_EQUAL(a.hashStop.ToString(), b.hashStop.ToString());
}

void ReceiveRawMessage(CNode& node, const char* command, CDataStream& payload)
{
    CDataStream msg(SER_NETWORK, PROTOCOL_VERSION);
    CMessageHeader hdr(Params().MessageStart(), command, payload.size());
    uint256 hash = Hash(payload.begin(), payload.end());
    memcpy(hdr.pchChecksum, hash.begin(), CMessageHeader::CHECKSUM_SIZE);
    msg << hdr;
    msg.insert(msg.end(), payload.begin(), payload.end());

    LOCK(node.cs_vRecvMsg);
    BOOST_REQUIRE(node.ReceiveMsgBytes((const char*)&msg[0], msg.size()));
}

void ReceiveHeaders(CNode& node, const std::vector<CBlockHeader>& headers)
{
    CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
    WriteCompactSize(payload, headers.size());
    for (const CBlockHeader& header : headers) {
        payload << header;
        WriteCompactSize(payload, 0);
    }
    ReceiveRawMessage(node, "headers", payload);
}

void ReceiveOversizedHeaders(CNode& node)
{
    CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
    WriteCompactSize(payload, MAX_HEADERS_RESULTS + 1);
    ReceiveRawMessage(node, "headers", payload);
}

void ReceiveInv(CNode& node, const std::vector<CInv>& invs)
{
    CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
    payload << invs;
    ReceiveRawMessage(node, "inv", payload);
}

std::unique_ptr<CNode> MakePeer()
{
    auto peer = std::make_unique<CNode>(INVALID_SOCKET, TestAddress(0xa0b0c001), "", false);
    peer->nVersion = PROTOCOL_VERSION;
    return peer;
}

void StartInitialGetHeaders(CNode& peer)
{
    const Consensus::Params& params = Params().GetConsensus();
    BOOST_REQUIRE(SendMessages(params, &peer));
    BOOST_REQUIRE_EQUAL(GetHeadersMessages(peer).size(), 1);
}

#ifdef ENABLE_MINING
/** A bare regtest chain, whose cheap Equihash parameters let tests solve their own headers. */
struct RegtestingSetup : public TestingSetup {
    RegtestingSetup() : TestingSetup(CBaseChainParams::REGTEST) {}
};

/**
 * Returns `count` solved headers extending the active tip, without their blocks.
 *
 * Every header uses the minimum difficulty, which regtest requires until its averaging
 * window is full, so the headers must not reach past that window.
 */
std::vector<CBlockHeader> SolvedHeaders(int count)
{
    const Consensus::Params& params = Params().GetConsensus();
    BOOST_REQUIRE(chainActive.Height() + count <= params.nPowAveragingWindow);

    std::vector<CBlockHeader> headers;
    CBlockHeader prev = chainActive.Tip()->GetBlockHeader();
    for (int i = 0; i < count; i++) {
        CBlockHeader header;
        header.nVersion = CBlockHeader::CURRENT_VERSION;
        header.hashPrevBlock = prev.GetHash();
        header.hashMerkleRoot = InsecureRand256();
        header.nTime = prev.nTime + 1;
        header.nBits = UintToArith256(params.powLimit).GetCompact();

        eh_HashState baseState = EhInitialiseState(params.nEquihashN, params.nEquihashK);
        CEquihashInput input{header};
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << input;
        baseState.Update((unsigned char*)&ss[0], ss.size());

        bool found = false;
        while (!found) {
            header.nNonce = ArithToUint256(UintToArith256(header.nNonce) + 1);
            eh_HashState state(baseState);
            state.Update(header.nNonce.begin(), header.nNonce.size());
            found = EhBasicSolveUncancellable(params.nEquihashN, params.nEquihashK, state,
                [&](std::vector<unsigned char> solution) {
                    header.nSolution = solution;
                    return CheckProofOfWork(header.GetHash(), header.nBits, params);
                });
        }
        headers.push_back(header);
        prev = header;
    }
    return headers;
}
#endif // ENABLE_MINING

} // namespace

BOOST_FIXTURE_TEST_SUITE(getheaders_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(initial_request_retries_and_disconnects)
{
    MockTimeGuard time;
    auto peer = MakePeer();
    StartInitialGetHeaders(*peer);

    const GetHeadersPayload first = GetHeadersMessages(*peer).front();

    for (int retry = 1; retry <= MAX_HEADERS_SYNC_RETRIES; retry++) {
        time.AdvanceHeadersTimeout();
        BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));
        BOOST_CHECK(!peer->fDisconnect);

        std::vector<GetHeadersPayload> messages = GetHeadersMessages(*peer);
        BOOST_REQUIRE_EQUAL(messages.size(), retry + 1);
        CheckSameGetHeaders(first, messages.back());
    }

    time.AdvanceHeadersTimeout();
    BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));
    BOOST_CHECK(peer->fDisconnect);
}

BOOST_AUTO_TEST_CASE(empty_headers_disarms_timeout)
{
    MockTimeGuard time;
    auto peer = MakePeer();
    StartInitialGetHeaders(*peer);

    ReceiveHeaders(*peer, {});
    BOOST_REQUIRE(ProcessMessages(Params(), peer.get()));

    time.AdvanceHeadersTimeout();
    BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));
    BOOST_CHECK(!peer->fDisconnect);
    BOOST_CHECK_EQUAL(GetHeadersMessages(*peer).size(), 1);
}

BOOST_AUTO_TEST_CASE(invalid_and_unrelated_headers_do_not_disarm)
{
    MockTimeGuard time;
    {
        auto peer = MakePeer();
        StartInitialGetHeaders(*peer);

        ReceiveOversizedHeaders(*peer);
        BOOST_REQUIRE(ProcessMessages(Params(), peer.get()));

        time.AdvanceHeadersTimeout();
        BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));
        BOOST_CHECK_EQUAL(GetHeadersMessages(*peer).size(), 2);
    }

    auto unrelatedPeer = MakePeer();
    StartInitialGetHeaders(*unrelatedPeer);
    ReceiveHeaders(*unrelatedPeer, {chainActive.Genesis()->GetBlockHeader()});
    BOOST_REQUIRE(ProcessMessages(Params(), unrelatedPeer.get()));

    time.AdvanceHeadersTimeout();
    BOOST_REQUIRE(SendMessages(Params().GetConsensus(), unrelatedPeer.get()));
    BOOST_CHECK_EQUAL(GetHeadersMessages(*unrelatedPeer).size(), 2);
}

BOOST_AUTO_TEST_CASE(inv_request_is_tracked_and_replayed)
{
    MockTimeGuard time;
    auto peer = MakePeer();
    StartInitialGetHeaders(*peer);
    ReceiveHeaders(*peer, {});
    BOOST_REQUIRE(ProcessMessages(Params(), peer.get()));

    uint256 hashStop = InsecureRand256();
    ReceiveInv(*peer, {CInv(MSG_BLOCK, hashStop)});
    BOOST_REQUIRE(ProcessMessages(Params(), peer.get()));

    std::vector<GetHeadersPayload> messages = GetHeadersMessages(*peer);
    BOOST_REQUIRE_EQUAL(messages.size(), 2);
    BOOST_CHECK_EQUAL(messages.back().hashStop.ToString(), hashStop.ToString());

    time.AdvanceHeadersTimeout();
    BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));
    messages = GetHeadersMessages(*peer);
    BOOST_REQUIRE_EQUAL(messages.size(), 3);
    CheckSameGetHeaders(messages[1], messages[2]);
}

BOOST_AUTO_TEST_CASE(ibd_poll_fires_after_headers_inactivity)
{
    MockTimeGuard time;
    InitialBlockDownloadGuard ibd;
    auto peer = MakePeer();
    StartInitialGetHeaders(*peer);
    ReceiveHeaders(*peer, {});
    BOOST_REQUIRE(ProcessMessages(Params(), peer.get()));

    time.AdvanceSeconds(HEADERS_IBD_POLL_INTERVAL + 1);
    BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));

    std::vector<GetHeadersPayload> messages = GetHeadersMessages(*peer);
    BOOST_REQUIRE_EQUAL(messages.size(), 2);
    BOOST_CHECK(messages.back().hashStop.IsNull());
}

BOOST_AUTO_TEST_CASE(ibd_poll_does_not_overlap_pending_request)
{
    MockTimeGuard time;
    InitialBlockDownloadGuard ibd;
    auto peer = MakePeer();
    StartInitialGetHeaders(*peer);
    const GetHeadersPayload initial = GetHeadersMessages(*peer).front();

    time.AdvanceSeconds(HEADERS_IBD_POLL_INTERVAL + 1);
    BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));

    std::vector<GetHeadersPayload> messages = GetHeadersMessages(*peer);
    BOOST_REQUIRE_EQUAL(messages.size(), 2);
    CheckSameGetHeaders(initial, messages.back());
}

BOOST_AUTO_TEST_CASE(headers_activity_refreshes_ibd_poll_timer)
{
    MockTimeGuard time;
    InitialBlockDownloadGuard ibd;
    auto peer = MakePeer();
    StartInitialGetHeaders(*peer);
    ReceiveHeaders(*peer, {});
    BOOST_REQUIRE(ProcessMessages(Params(), peer.get()));

    time.AdvanceSeconds(HEADERS_IBD_POLL_INTERVAL - 1);
    BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));
    BOOST_CHECK_EQUAL(GetHeadersMessages(*peer).size(), 1);

    time.AdvanceSeconds(2);
    BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));
    BOOST_CHECK_EQUAL(GetHeadersMessages(*peer).size(), 2);
}

BOOST_AUTO_TEST_CASE(answered_ibd_poll_rearms_timer)
{
    MockTimeGuard time;
    InitialBlockDownloadGuard ibd;
    auto peer = MakePeer();
    StartInitialGetHeaders(*peer);
    ReceiveHeaders(*peer, {});
    BOOST_REQUIRE(ProcessMessages(Params(), peer.get()));

    time.AdvanceSeconds(HEADERS_IBD_POLL_INTERVAL + 1);
    BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));
    BOOST_REQUIRE_EQUAL(GetHeadersMessages(*peer).size(), 2);

    ReceiveHeaders(*peer, {});
    BOOST_REQUIRE(ProcessMessages(Params(), peer.get()));
    time.AdvanceSeconds(HEADERS_IBD_POLL_INTERVAL - 1);
    BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));
    BOOST_CHECK_EQUAL(GetHeadersMessages(*peer).size(), 2);

    time.AdvanceSeconds(2);
    BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));
    BOOST_CHECK_EQUAL(GetHeadersMessages(*peer).size(), 3);
}

BOOST_AUTO_TEST_CASE(unanswered_ibd_poll_retries_and_disconnects)
{
    MockTimeGuard time;
    InitialBlockDownloadGuard ibd;
    auto peer = MakePeer();
    StartInitialGetHeaders(*peer);
    ReceiveHeaders(*peer, {});
    BOOST_REQUIRE(ProcessMessages(Params(), peer.get()));

    time.AdvanceSeconds(HEADERS_IBD_POLL_INTERVAL + 1);
    BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));
    const GetHeadersPayload poll = GetHeadersMessages(*peer).back();

    for (int retry = 1; retry <= MAX_HEADERS_SYNC_RETRIES; retry++) {
        time.AdvanceHeadersTimeout();
        BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));
        BOOST_CHECK(!peer->fDisconnect);
        CheckSameGetHeaders(poll, GetHeadersMessages(*peer).back());
    }

    time.AdvanceHeadersTimeout();
    BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));
    BOOST_CHECK(peer->fDisconnect);
}

#ifdef ENABLE_MINING
BOOST_FIXTURE_TEST_CASE(non_contiguous_headers_do_not_disarm, TestChain100Setup)
{
    MockTimeGuard time;
    auto peer = MakePeer();
    StartInitialGetHeaders(*peer);
    ReceiveHeaders(*peer, {});
    BOOST_REQUIRE(ProcessMessages(Params(), peer.get()));

    uint256 hashStop = InsecureRand256();
    ReceiveInv(*peer, {CInv(MSG_BLOCK, hashStop)});
    BOOST_REQUIRE(ProcessMessages(Params(), peer.get()));

    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    std::vector<CMutableTransaction> noTxns;
    CBlock first = CreateAndProcessBlock(noTxns, scriptPubKey);
    CreateAndProcessBlock(noTxns, scriptPubKey);
    CBlock third = CreateAndProcessBlock(noTxns, scriptPubKey);

    ReceiveHeaders(*peer, {first.GetBlockHeader(), third.GetBlockHeader()});
    BOOST_REQUIRE(ProcessMessages(Params(), peer.get()));

    time.AdvanceHeadersTimeout();
    BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));
    BOOST_CHECK_EQUAL(GetHeadersMessages(*peer).size(), 3);
}

BOOST_FIXTURE_TEST_CASE(continuation_request_is_tracked_and_replayed, TestChain100Setup)
{
    MockTimeGuard time;
    auto peer = MakePeer();
    StartInitialGetHeaders(*peer);
    ReceiveHeaders(*peer, {});
    BOOST_REQUIRE(ProcessMessages(Params(), peer.get()));

    uint256 hashStop = InsecureRand256();
    ReceiveInv(*peer, {CInv(MSG_BLOCK, hashStop)});
    BOOST_REQUIRE(ProcessMessages(Params(), peer.get()));

    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    std::vector<CMutableTransaction> noTxns;
    std::vector<CBlockHeader> headers;
    headers.reserve(MAX_HEADERS_RESULTS);
    // TestChain100Setup leaves regtest out of IBD, so these self-mined headers still exercise
    // the full-batch continuation path even though they are already in mapBlockIndex.
    for (unsigned int i = 0; i < MAX_HEADERS_RESULTS; i++) {
        headers.push_back(CreateAndProcessBlock(noTxns, scriptPubKey).GetBlockHeader());
    }

    ReceiveHeaders(*peer, headers);
    BOOST_REQUIRE(ProcessMessages(Params(), peer.get()));

    std::vector<GetHeadersPayload> messages = GetHeadersMessages(*peer);
    BOOST_REQUIRE_EQUAL(messages.size(), 3);
    BOOST_CHECK(messages.back().hashStop.IsNull());
    BOOST_REQUIRE(!messages.back().locator.IsNull());
    BOOST_CHECK_EQUAL(messages.back().locator.vHave.front().ToString(), headers.back().GetHash().ToString());

    const GetHeadersPayload continuation = messages.back();
    time.AdvanceHeadersTimeout();
    BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));
    messages = GetHeadersMessages(*peer);
    BOOST_REQUIRE_EQUAL(messages.size(), 4);
    CheckSameGetHeaders(continuation, messages.back());
}

BOOST_FIXTURE_TEST_CASE(full_new_headers_without_pending_still_continues, TestChain100Setup)
{
    auto peer = MakePeer();
    StartInitialGetHeaders(*peer);
    ReceiveHeaders(*peer, {});
    BOOST_REQUIRE(ProcessMessages(Params(), peer.get()));

    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    std::vector<CMutableTransaction> noTxns;
    std::vector<CBlockHeader> headers;
    headers.reserve(MAX_HEADERS_RESULTS);
    for (unsigned int i = 0; i < MAX_HEADERS_RESULTS; i++) {
        headers.push_back(CreateAndProcessBlock(noTxns, scriptPubKey).GetBlockHeader());
    }

    ReceiveHeaders(*peer, headers);
    BOOST_REQUIRE(ProcessMessages(Params(), peer.get()));

    std::vector<GetHeadersPayload> messages = GetHeadersMessages(*peer);
    BOOST_REQUIRE_EQUAL(messages.size(), 2);
    BOOST_CHECK(messages.back().hashStop.IsNull());
    BOOST_REQUIRE(!messages.back().locator.IsNull());
    BOOST_CHECK_EQUAL(messages.back().locator.vHave.front().ToString(), headers.back().GetHash().ToString());
}

BOOST_FIXTURE_TEST_CASE(each_block_is_requested_in_its_own_getdata, RegtestingSetup)
{
    auto peer = MakePeer();
    std::vector<CBlockHeader> headers = SolvedHeaders(MAX_BLOCKS_IN_TRANSIT_PER_PEER);
    ReceiveHeaders(*peer, headers);
    BOOST_REQUIRE(ProcessMessages(Params(), peer.get()));
    BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));

    std::vector<std::vector<CInv>> messages = GetDataMessages(*peer);
    BOOST_REQUIRE_EQUAL(messages.size(), headers.size());
    for (size_t i = 0; i < headers.size(); i++) {
        BOOST_REQUIRE_EQUAL(messages[i].size(), 1);
        BOOST_CHECK_EQUAL(messages[i][0].type, MSG_BLOCK);
        BOOST_CHECK_EQUAL(messages[i][0].hash.ToString(), headers[i].GetHash().ToString());
    }
}

BOOST_FIXTURE_TEST_CASE(dropped_peer_does_not_delay_block_timeouts, RegtestingSetup)
{
    MockTimeGuard time;
    std::vector<CBlockHeader> headers = SolvedHeaders(MAX_BLOCKS_IN_TRANSIT_PER_PEER);
    {
        auto dropped = MakePeer();
        ReceiveHeaders(*dropped, headers);
        BOOST_REQUIRE(ProcessMessages(Params(), dropped.get()));
        BOOST_REQUIRE(SendMessages(Params().GetConsensus(), dropped.get()));
        CNodeStateStats droppedStats;
        BOOST_REQUIRE(GetNodeStateStats(dropped->GetId(), droppedStats));
        BOOST_REQUIRE_EQUAL(droppedStats.vHeightInFlight.size(), headers.size());
    } // Destroying the peer finalizes it with all of its blocks still in flight.

    auto peer = MakePeer();
    ReceiveHeaders(*peer, headers);
    BOOST_REQUIRE(ProcessMessages(Params(), peer.get()));
    BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));
    CNodeStateStats peerStats;
    BOOST_REQUIRE(GetNodeStateStats(peer->GetId(), peerStats));
    BOOST_REQUIRE_EQUAL(peerStats.vHeightInFlight.size(), headers.size());

    // With nothing else in flight, `GetBlockTimeout` gives the first block two target spacings.
    const int64_t timeout = 2 * Params().GetConsensus().PoWTargetSpacing(1);
    time.AdvanceSeconds(timeout - 1);
    BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));
    BOOST_CHECK(!peer->fDisconnect);

    time.AdvanceSeconds(2);
    BOOST_REQUIRE(SendMessages(Params().GetConsensus(), peer.get()));
    BOOST_CHECK(peer->fDisconnect);
}
#endif

BOOST_AUTO_TEST_SUITE_END()
