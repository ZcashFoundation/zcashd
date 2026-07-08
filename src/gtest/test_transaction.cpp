#include <gtest/gtest.h>

#include "gtest/utils.h"
#include "consensus/upgrades.h"
#include "primitives/transaction.h"
#include "streams.h"
#include "transaction_builder.h"
#include "version.h"
#include "zcash/Note.hpp"
#include "zcash/Address.hpp"

#include <array>

#include <rust/ed25519.h>

// Round-trips an empty v6 (ZIP 248) transaction through serialization. Constructing
// the CTransaction exercises UpdateHash, whose librustzcash reparse rejects any
// non-canonical v6 encoding — including a missing or malformed Ironwood slot — so
// this doubles as a check that the C++ serializer emits the canonical v6 format.
TEST(Transaction, V6EmptyBundlesRoundTrip) {
    CMutableTransaction mtx;
    mtx.fOverwintered = true;
    mtx.nVersionGroupId = ZIP248_VERSION_GROUP_ID;
    mtx.nVersion = ZIP248_TX_VERSION;
    mtx.nConsensusBranchId = NetworkUpgradeInfo[Consensus::UPGRADE_NU6_3].nBranchId;

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << mtx;
    const std::vector<unsigned char> bytes(ss.begin(), ss.end());

    CTransaction tx(deserialize, ss);
    EXPECT_EQ(tx.GetHash(), mtx.GetHash());
    EXPECT_EQ(tx.GetAuthDigest(), mtx.GetAuthDigest());
    EXPECT_FALSE(tx.GetOrchardBundle().IsPresent());
    EXPECT_FALSE(tx.GetIronwoodBundle().IsPresent());
    EXPECT_EQ(tx.GetConsensusBranchId(), mtx.nConsensusBranchId);

    CDataStream ss2(SER_NETWORK, PROTOCOL_VERSION);
    ss2 << tx;
    const std::vector<unsigned char> bytes2(ss2.begin(), ss2.end());
    EXPECT_EQ(bytes, bytes2);
}

TEST(Transaction, JSDescriptionRandomized) {
    // construct a merkle tree
    SproutMerkleTree merkleTree;

    libzcash::SproutSpendingKey k = libzcash::SproutSpendingKey::random();
    libzcash::SproutPaymentAddress addr = k.address();

    libzcash::SproutNote note(addr.a_pk, 100, uint256(), uint256());

    // commitment from coin
    uint256 commitment = note.cm();

    // insert commitment into the merkle tree
    merkleTree.append(commitment);

    // compute the merkle root we will be working with
    uint256 rt = merkleTree.root();

    auto witness = merkleTree.witness();

    // create JSDescription
    ed25519::VerificationKey joinSplitPubKey;
    std::array<libzcash::JSInput, ZC_NUM_JS_INPUTS> inputs = {
        libzcash::JSInput(witness, note, k),
        libzcash::JSInput() // dummy input of zero value
    };
    std::array<libzcash::JSOutput, ZC_NUM_JS_OUTPUTS> outputs = {
        libzcash::JSOutput(addr, 50),
        libzcash::JSOutput(addr, 50)
    };
    std::array<size_t, ZC_NUM_JS_INPUTS> inputMap;
    std::array<size_t, ZC_NUM_JS_OUTPUTS> outputMap;

    {
        auto jsdesc = JSDescriptionInfo(
            joinSplitPubKey, rt,
            inputs, outputs,
            0, 0
        ).BuildRandomized(
            inputMap, outputMap,
            false);

        std::set<size_t> inputSet(inputMap.begin(), inputMap.end());
        std::set<size_t> expectedInputSet {0, 1};
        EXPECT_EQ(expectedInputSet, inputSet);

        std::set<size_t> outputSet(outputMap.begin(), outputMap.end());
        std::set<size_t> expectedOutputSet {0, 1};
        EXPECT_EQ(expectedOutputSet, outputSet);
    }

    {
        auto jsdesc = JSDescriptionInfo(
            joinSplitPubKey, rt,
            inputs, outputs,
            0, 0
        ).BuildRandomized(
            inputMap, outputMap,
            false, nullptr, GenZero);

        std::array<size_t, ZC_NUM_JS_INPUTS> expectedInputMap {1, 0};
        std::array<size_t, ZC_NUM_JS_OUTPUTS> expectedOutputMap {1, 0};
        EXPECT_EQ(expectedInputMap, inputMap);
        EXPECT_EQ(expectedOutputMap, outputMap);
    }

    {
        auto jsdesc = JSDescriptionInfo(
            joinSplitPubKey, rt,
            inputs, outputs,
            0, 0
        ).BuildRandomized(
            inputMap, outputMap,
            false, nullptr, GenMax);

        std::array<size_t, ZC_NUM_JS_INPUTS> expectedInputMap {0, 1};
        std::array<size_t, ZC_NUM_JS_OUTPUTS> expectedOutputMap {0, 1};
        EXPECT_EQ(expectedInputMap, inputMap);
        EXPECT_EQ(expectedOutputMap, outputMap);
    }
}
