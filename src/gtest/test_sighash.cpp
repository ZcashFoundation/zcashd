#include <gtest/gtest.h>

#include "random.h"
#include "script/interpreter.h"
#include "script/standard.h"

static std::pair<CMutableTransaction, std::vector<CTxOut>> DummyV5Transaction() {
    auto key = CKey::TestOnlyRandomKey(true);
    auto scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());

    // Create a fake pair of coins to spend.
    std::vector<CTxOut> allPrevOutputs;
    allPrevOutputs.resize(2);
    allPrevOutputs[0].nValue = 1000;
    allPrevOutputs[0].scriptPubKey = scriptPubKey;
    allPrevOutputs[1].nValue = 2000;
    allPrevOutputs[1].scriptPubKey = scriptPubKey;

    // Create a fake 2-in 2-out transaction.
    CMutableTransaction mtx;
    mtx.fOverwintered = true;
    mtx.nVersionGroupId = ZIP225_VERSION_GROUP_ID;
    mtx.nVersion = ZIP225_TX_VERSION;
    mtx.nConsensusBranchId = NetworkUpgradeInfo[Consensus::UPGRADE_NU5].nBranchId;
    mtx.vin.resize(allPrevOutputs.size());
    mtx.vin[0].prevout.hash = GetRandHash();
    mtx.vin[0].prevout.n = 0;
    mtx.vin[0].prevout.hash = GetRandHash();
    mtx.vin[0].prevout.n = 7;
    mtx.vout.resize(2);
    mtx.vout[0].nValue = 1500;
    mtx.vout[0].scriptPubKey = scriptPubKey;
    mtx.vout[1].nValue = 1500;
    mtx.vout[1].scriptPubKey = scriptPubKey;

    return std::make_pair(mtx, allPrevOutputs);
}

// Same shape as DummyV5Transaction(), but a v6 (ZIP 248 / NU6.3 Ironwood)
// transaction. v6 transactions must use the ZIP 244 signature digest, so
// SignatureHash() must dispatch them through the ZIP 244 path exactly as it does
// for v5. If it instead falls through to the legacy Sapling/Overwinter sighash,
// zcashd computes a different digest than Zebra and forks at the first v6
// transaction after NU6.3.
static std::pair<CMutableTransaction, std::vector<CTxOut>> DummyV6Transaction() {
    auto parts = DummyV5Transaction();
    parts.first.nVersionGroupId = ZIP248_VERSION_GROUP_ID;
    parts.first.nVersion = ZIP248_TX_VERSION;
    parts.first.nConsensusBranchId = NetworkUpgradeInfo[Consensus::UPGRADE_NU6_3].nBranchId;
    return parts;
}

// Regression test for the v6 sighash dispatch. The ZIP 244 code path is the only
// one that rejects undefined hash types (and SIGHASH_SINGLE without a
// corresponding output). The legacy Sapling/Overwinter path does not throw on
// them. So requiring a v6 transaction to reject unknown hash types proves it is
// being routed to the ZIP 244 digest and not the legacy one.
TEST(SigHashTest, Zip244UsedForV6Transactions) {
    auto parts = DummyV6Transaction();
    auto mtx = parts.first;
    auto allPrevOutputs = parts.second;

    unsigned int nIn = 1;
    PrecomputedTransactionData txdata(mtx, allPrevOutputs);
    CScript scriptCode;
    CAmount amount;
    uint32_t consensusBranchId;

    // Known ZIP 244 hash types must succeed (this also exercises the v6 digest
    // FFI end-to-end, confirming preTx is built and the digest handles v6).
    std::vector<uint8_t> knownSighashTypes {
        SIGHASH_ALL,
        SIGHASH_SINGLE,
        SIGHASH_NONE,
        SIGHASH_ANYONECANPAY | SIGHASH_ALL,
        SIGHASH_ANYONECANPAY | SIGHASH_SINGLE,
        SIGHASH_ANYONECANPAY | SIGHASH_NONE,
    };
    for (auto nHashType : knownSighashTypes) {
        EXPECT_NO_THROW(SignatureHash(
            scriptCode, mtx, nIn, nHashType, amount, consensusBranchId, txdata));
    }

    // Undefined-in-ZIP-244 hash types must throw. Under the pre-fix dispatch bug
    // (v6 falling through to the legacy sighash) these would NOT throw.
    std::vector<uint8_t> unknownSighashTypes {
        0, // Known in BIP 341, unknown in ZIP 244.
        SIGHASH_SINGLE + 1,
        0x7f,
        0xff,
    };
    for (auto nHashType : unknownSighashTypes) {
        EXPECT_THROW(
            SignatureHash(scriptCode, mtx, nIn, nHashType, amount, consensusBranchId, txdata),
            std::logic_error);
    }
}

TEST(SigHashTest, Zip244AcceptsKnownHashTypes) {
    auto parts = DummyV5Transaction();
    auto mtx = parts.first;
    auto allPrevOutputs = parts.second;

    unsigned int nIn = 1;
    PrecomputedTransactionData txdata(mtx, allPrevOutputs);
    // These aren't used for ZIP 244 sighashes.
    CScript scriptCode;
    CAmount amount;
    uint32_t consensusBranchId;

    // Nothing should be thrown for known sighash types.
    std::vector<uint8_t> knownSighashTypes {
        SIGHASH_ALL,
        SIGHASH_SINGLE,
        SIGHASH_NONE,
        SIGHASH_ANYONECANPAY | SIGHASH_ALL,
        SIGHASH_ANYONECANPAY | SIGHASH_SINGLE,
        SIGHASH_ANYONECANPAY | SIGHASH_NONE,
    };
    for (auto nHashType : knownSighashTypes) {
        EXPECT_NO_THROW(SignatureHash(
            scriptCode, mtx, nIn, nHashType, amount, consensusBranchId, txdata));
    }
}

TEST(SigHashTest, Zip244RejectsUnknownHashTypes) {
    auto parts = DummyV5Transaction();
    auto mtx = parts.first;
    auto allPrevOutputs = parts.second;

    unsigned int nIn = 1;
    PrecomputedTransactionData txdata(mtx, allPrevOutputs);
    // These aren't used for ZIP 244 sighashes.
    CScript scriptCode;
    CAmount amount;
    uint32_t consensusBranchId;

    // An error should be thrown for unknown sighash types.
    std::vector<uint8_t> unknownSighashTypes {
        0, // Known in BIP 341, unknown in ZIP 244.
        SIGHASH_SINGLE + 1,
        0x7f,
        0xff,
    };
    for (auto nHashType : unknownSighashTypes) {
        EXPECT_THROW(
            SignatureHash(scriptCode, mtx, nIn, nHashType, amount, consensusBranchId, txdata),
            std::logic_error);
    }
}

TEST(SigHashTest, Zip244RejectsSingleWithoutCorrespondingOutput) {
    auto parts = DummyV5Transaction();
    auto mtx = parts.first;
    auto allPrevOutputs = parts.second;

    // Modify the transaction to have only 1 output.
    mtx.vout.resize(1);

    unsigned int nIn = 1;
    PrecomputedTransactionData txdata(mtx, allPrevOutputs);
    // These aren't used for ZIP 244 sighashes.
    CScript scriptCode;
    CAmount amount;
    uint32_t consensusBranchId;

    // Nothing should be thrown for non-single sighash types.
    std::vector<uint8_t> nonSighashSingleTypes {
        SIGHASH_ALL,
        SIGHASH_NONE,
        SIGHASH_ANYONECANPAY | SIGHASH_ALL,
        SIGHASH_ANYONECANPAY | SIGHASH_NONE,
    };
    for (auto nHashType : nonSighashSingleTypes) {
        EXPECT_NO_THROW(SignatureHash(
            scriptCode, mtx, nIn, nHashType, amount, consensusBranchId, txdata));
    }

    // SIGHASH_SINGLE types should throw an error.
    std::vector<uint8_t> sighashSingleTypes {
        SIGHASH_SINGLE,
        SIGHASH_ANYONECANPAY | SIGHASH_SINGLE,
    };
    for (auto nHashType : sighashSingleTypes) {
        EXPECT_THROW(
            SignatureHash(scriptCode, mtx, nIn, nHashType, amount, consensusBranchId, txdata),
            std::logic_error);
    }
}
