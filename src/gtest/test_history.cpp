#include <gtest/gtest.h>

#include "main.h"
#include "rust/history.h"
#include "util/test.h"
#include "zcash/History.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

namespace {

uint256 PatternedUint256(uint8_t seed) {
    std::array<uint8_t, 32> bytes;
    for (size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<uint8_t>(seed + i);
    }
    return uint256::FromRawBytes(bytes);
}

void AppendUint32LE(std::vector<unsigned char>& bytes, uint32_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 0xff));
    bytes.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
    bytes.push_back(static_cast<unsigned char>((value >> 16) & 0xff));
    bytes.push_back(static_cast<unsigned char>((value >> 24) & 0xff));
}

void AppendRawBytes(std::vector<unsigned char>& bytes, const uint256& value) {
    const auto raw = value.ToRawBytes();
    bytes.insert(bytes.end(), raw.begin(), raw.end());
}

void AppendSmallCompactSize(std::vector<unsigned char>& bytes, uint64_t value) {
    assert(value < 253);
    bytes.push_back(static_cast<unsigned char>(value));
}

} // namespace

HistoryNode getLeafN(uint64_t block_num) {
    HistoryNode node = libzcash::NewV1Leaf(
        uint256(),
        block_num*10,
        block_num*13,
        uint256(),
        uint256(),
        block_num,
        3
    );
    return node;
}

TEST(History, NewV3LeafSerializesIronwoodFields) {
    const auto commitment = PatternedUint256(0x10);
    const auto saplingRoot = PatternedUint256(0x20);
    const auto orchardRoot = PatternedUint256(0x30);
    const auto ironwoodRoot = PatternedUint256(0x40);
    const auto totalWork = PatternedUint256(0x50);
    const uint32_t time = 2;
    const uint32_t target = 3;
    const uint64_t height = 4;
    const uint64_t saplingTxCount = 5;
    const uint64_t orchardTxCount = 6;
    const uint64_t ironwoodTxCount = 7;

    const auto node = libzcash::NewV3Leaf(
        commitment,
        time,
        target,
        saplingRoot,
        orchardRoot,
        ironwoodRoot,
        totalWork,
        height,
        saplingTxCount,
        orchardTxCount,
        ironwoodTxCount);

    std::vector<unsigned char> expected;
    AppendRawBytes(expected, commitment);
    AppendUint32LE(expected, time);
    AppendUint32LE(expected, time);
    AppendUint32LE(expected, target);
    AppendUint32LE(expected, target);
    AppendRawBytes(expected, saplingRoot);
    AppendRawBytes(expected, saplingRoot);
    AppendRawBytes(expected, totalWork);
    AppendSmallCompactSize(expected, height);
    AppendSmallCompactSize(expected, height);
    AppendSmallCompactSize(expected, saplingTxCount);
    AppendRawBytes(expected, orchardRoot);
    AppendRawBytes(expected, orchardRoot);
    AppendSmallCompactSize(expected, orchardTxCount);
    AppendRawBytes(expected, ironwoodRoot);
    AppendRawBytes(expected, ironwoodRoot);
    AppendSmallCompactSize(expected, ironwoodTxCount);

    ASSERT_LE(expected.size(), node.size());
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), node.begin()));
    EXPECT_TRUE(std::all_of(
        node.begin() + expected.size(),
        node.end(),
        [](unsigned char byte) { return byte == 0; }));
}

TEST(History, NewV3LeafParsesAsRustV3) {
    const auto consensusBranchId = NetworkUpgradeInfo[Consensus::UPGRADE_NU6_3].nBranchId;
    const auto node = libzcash::NewV3Leaf(
        PatternedUint256(0x10),
        2,
        3,
        PatternedUint256(0x20),
        PatternedUint256(0x30),
        PatternedUint256(0x40),
        PatternedUint256(0x50),
        4,
        5,
        6,
        7);

    std::array<uint8_t, 32> hash;
    ASSERT_NO_THROW(hash = mmr::hash_node(consensusBranchId, node));
    EXPECT_FALSE(uint256::FromRawBytes(hash).IsNull());
}

TEST(History, Smoky) {
    // Fake an empty view
    CCoinsViewDummy fakeDB;
    CCoinsViewCache view(&fakeDB);

    uint32_t epochId = 0;

    // Test initial value
    EXPECT_EQ(view.GetHistoryLength(epochId), 0);

    view.PushHistoryNode(epochId, getLeafN(1));

    EXPECT_EQ(view.GetHistoryLength(epochId), 1);

    view.PushHistoryNode(epochId, getLeafN(2));

    EXPECT_EQ(view.GetHistoryLength(epochId), 3);

    view.PushHistoryNode(epochId, getLeafN(3));

    EXPECT_EQ(view.GetHistoryLength(epochId), 4);

    view.PushHistoryNode(epochId, getLeafN(4));

    uint256 h4Root = view.GetHistoryRoot(epochId);

    EXPECT_EQ(view.GetHistoryLength(epochId), 7);

    view.PushHistoryNode(epochId, getLeafN(5));
    EXPECT_EQ(view.GetHistoryLength(epochId), 8);

    view.PopHistoryNode(epochId);

    EXPECT_EQ(view.GetHistoryLength(epochId), 7);
    EXPECT_EQ(h4Root, view.GetHistoryRoot(epochId));
}


TEST(History, EpochBoundaries) {
    // Fake an empty view
    CCoinsViewDummy fakeDB;
    CCoinsViewCache view(&fakeDB);

    // Test with the Heartwood and Canopy epochs
    uint32_t epoch1 = 0xf5b9230b;
    uint32_t epoch2 = 0xe9ff75a6;

    view.PushHistoryNode(epoch1, getLeafN(1));

    EXPECT_EQ(view.GetHistoryLength(epoch1), 1);

    view.PushHistoryNode(epoch1, getLeafN(2));

    EXPECT_EQ(view.GetHistoryLength(epoch1), 3);

    view.PushHistoryNode(epoch1, getLeafN(3));

    EXPECT_EQ(view.GetHistoryLength(epoch1), 4);

    view.PushHistoryNode(epoch1, getLeafN(4));

    uint256 h4Root = view.GetHistoryRoot(epoch1);

    EXPECT_EQ(view.GetHistoryLength(epoch1), 7);

    view.PushHistoryNode(epoch1, getLeafN(5));
    EXPECT_EQ(view.GetHistoryLength(epoch1), 8);


    // Move to Canopy epoch
    view.PushHistoryNode(epoch2, getLeafN(6));
    EXPECT_EQ(view.GetHistoryLength(epoch1), 8);
    EXPECT_EQ(view.GetHistoryLength(epoch2), 1);

    view.PushHistoryNode(epoch2, getLeafN(7));
    EXPECT_EQ(view.GetHistoryLength(epoch1), 8);
    EXPECT_EQ(view.GetHistoryLength(epoch2), 3);

    view.PushHistoryNode(epoch2, getLeafN(8));
    EXPECT_EQ(view.GetHistoryLength(epoch1), 8);
    EXPECT_EQ(view.GetHistoryLength(epoch2), 4);

    // Rolling epoch back to 1
    view.PopHistoryNode(epoch2);
    EXPECT_EQ(view.GetHistoryLength(epoch2), 3);

    view.PopHistoryNode(epoch2);
    EXPECT_EQ(view.GetHistoryLength(epoch2), 1);
    EXPECT_EQ(view.GetHistoryLength(epoch1), 8);

    // And even rolling epoch 1 back a bit
    view.PopHistoryNode(epoch1);
    EXPECT_EQ(view.GetHistoryLength(epoch1), 7);

    // And also rolling epoch 2 back to 0
    view.PopHistoryNode(epoch2);
    EXPECT_EQ(view.GetHistoryLength(epoch2), 0);

    // Trying to truncate an empty tree is a no-op
    view.PopHistoryNode(epoch2);
    EXPECT_EQ(view.GetHistoryLength(epoch2), 0);

}

TEST(History, GarbageMemoryHash) {
    const auto consensusBranchId = NetworkUpgradeInfo[Consensus::UPGRADE_HEARTWOOD].nBranchId;

    CCoinsViewDummy fakeDB;
    CCoinsViewCache view(&fakeDB);

    // Hash two history nodes
    HistoryNode node0 = getLeafN(1);
    HistoryNode node1 = getLeafN(2);

    view.PushHistoryNode(consensusBranchId, node0);
    view.PushHistoryNode(consensusBranchId, node1);

    uint256 historyRoot = view.GetHistoryRoot(consensusBranchId);

    // Change garbage memory and re-hash nodes
    CCoinsViewDummy fakeDBGarbage;
    CCoinsViewCache viewGarbage(&fakeDBGarbage);

    HistoryNode node0Garbage = getLeafN(1);
    HistoryNode node1Garbage = getLeafN(2);

    node0Garbage[NODE_SERIALIZED_LENGTH - 1] = node0[NODE_SERIALIZED_LENGTH - 1] ^ 1;
    node1Garbage[NODE_SERIALIZED_LENGTH - 1] = node1[NODE_SERIALIZED_LENGTH - 1] ^ 1;

    viewGarbage.PushHistoryNode(consensusBranchId, node0Garbage);
    viewGarbage.PushHistoryNode(consensusBranchId, node1Garbage);

    uint256 historyRootGarbage = viewGarbage.GetHistoryRoot(consensusBranchId);

    // Check history root and garbage history root are equal
    EXPECT_EQ(historyRoot, historyRootGarbage);
}
