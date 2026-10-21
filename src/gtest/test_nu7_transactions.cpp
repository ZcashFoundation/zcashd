#include <gtest/gtest.h>

#include "chainparams.h"
#include "consensus/validation.h"
#include "gtest/utils.h"
#include "main.h"
#include "miner.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "util/test.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

// ZIP 2003 (no v4 transactions from NU7) and ZIP 218 (per-block shielded limits and the
// post-NU7 default expiry).

/** An empty v4 transaction, or an empty coinbase transaction if `coinbase` is true. */
static CTransaction EmptyV4Transaction(bool coinbase)
{
    CMutableTransaction mtx;
    mtx.fOverwintered = true;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
    mtx.nVersion = SAPLING_TX_VERSION;
    if (coinbase) {
        mtx.vin.resize(1);
        mtx.vin[0].prevout.SetNull();
    }
    return CTransaction(mtx);
}

/** Returns the DoS score of an invalid state. */
static int DoSScore(const CValidationState& state)
{
    int dos = 0;
    EXPECT_TRUE(state.IsInvalid(dos));
    return dos;
}

TEST(NU7Transactions, V4IsInvalidFromNU7) {
    RegtestActivateNU7(false, 10);

    for (bool coinbase : {false, true}) {
        const CTransaction tx = EmptyV4Transaction(coinbase);

        CValidationState before;
        EXPECT_TRUE(ContextualCheckTransaction(tx, before, Params(), 9, true)) << coinbase;

        CValidationState inBlock;
        EXPECT_FALSE(ContextualCheckTransaction(tx, inBlock, Params(), 10, true)) << coinbase;
        EXPECT_EQ(inBlock.GetRejectReason(), "bad-tx-v4-after-nu7");
        EXPECT_EQ(DoSScore(inBlock), 100);

        CValidationState inMempool;
        EXPECT_FALSE(ContextualCheckTransaction(tx, inMempool, Params(), 10, false)) << coinbase;
        EXPECT_EQ(inMempool.GetRejectReason(), "bad-tx-v4-after-nu7");
        EXPECT_EQ(DoSScore(inMempool), 10);
    }

    // An empty v5 transaction committing to NU7 is valid from NU7.
    CMutableTransaction v5;
    v5.fOverwintered = true;
    v5.nVersionGroupId = ZIP225_VERSION_GROUP_ID;
    v5.nVersion = ZIP225_TX_VERSION;
    v5.nConsensusBranchId = NetworkUpgradeInfo[Consensus::UPGRADE_NU7].nBranchId;
    CValidationState state;
    EXPECT_TRUE(ContextualCheckTransaction(CTransaction(v5), state, Params(), 10, true));

    RegtestDeactivateNU7();
}

TEST(NU7Transactions, NewTransactionsFromNU7) {
    const auto& params = RegtestActivateNU7(false, 10);
    const uint32_t nu6point3 = NetworkUpgradeInfo[Consensus::UPGRADE_NU6_3].nBranchId;
    const uint32_t nu7 = NetworkUpgradeInfo[Consensus::UPGRADE_NU7].nBranchId;

    // requireV4 still selects v4 before NU7, but is ignored from NU7.
    EXPECT_EQ(CurrentTxVersionInfo(params, 9, true).nVersion, SAPLING_TX_VERSION);
    EXPECT_EQ(CurrentTxVersionInfo(params, 10, true).nVersion, ZIP225_TX_VERSION);
    EXPECT_EQ(CurrentTxVersionInfo(params, 10, true).nVersionGroupId, ZIP225_VERSION_GROUP_ID);
    EXPECT_EQ(CreateNewContextualCMutableTransaction(params, 8, true).nVersion, SAPLING_TX_VERSION);
    EXPECT_EQ(CreateNewContextualCMutableTransaction(params, 10, true).nVersion, ZIP225_TX_VERSION);

    // Before NU7 the expiry is clamped to the last pre-NU7 block; from NU7 the default
    // delta is 120 blocks (40 * 3), the same time at the 25 second spacing.
    for (const auto& [height, branchId, expiry] : std::vector<std::tuple<int, uint32_t, uint32_t>>{
             {8, nu6point3, 9}, {9, nu6point3, 9}, {10, nu7, 130}, {11, nu7, 131}}) {
        const auto mtx = CreateNewContextualCMutableTransaction(params, height, false);
        EXPECT_EQ(mtx.nVersion, ZIP225_TX_VERSION) << height;
        EXPECT_EQ(mtx.nConsensusBranchId, branchId) << height;
        EXPECT_EQ(mtx.nExpiryHeight, expiry) << height;
    }
    EXPECT_EQ(DEFAULT_POST_NU7_TX_EXPIRY_DELTA, 120);

    RegtestDeactivateNU7();
}

TEST(NU7Transactions, ShieldedLimits) {
    auto counts = [](uint64_t orchard, uint64_t ironwood, uint64_t sapling, uint64_t joinsplits) {
        ShieldedActionCounts result;
        result.orchardActions = orchard;
        result.ironwoodActions = ironwood;
        result.saplingIOs = sapling;
        result.sproutJoinSplits = joinsplits;
        return result;
    };

    // The per-pool limits and the global budget, as in Zakura's block limit tests.
    for (const auto& [c, expected] : std::vector<std::pair<ShieldedActionCounts, std::optional<std::string>>>{
             {counts(0, 0, 0, 0), std::nullopt},
             {counts(330, 0, 0, 0), std::nullopt},
             {counts(331, 0, 0, 0), "bad-blk-orchard-actions"},
             {counts(0, 330, 0, 0), std::nullopt},
             {counts(0, 331, 0, 0), "bad-blk-ironwood-actions"},
             {counts(0, 0, 300, 0), std::nullopt},
             {counts(0, 0, 301, 0), "bad-blk-sapling-ios"},
             {counts(0, 0, 0, 1), "bad-blk-joinsplits"},
             {counts(329, 0, 1, 0), std::nullopt},
             {counts(330, 0, 1, 0), "bad-blk-shielded-cost"},
             {counts(165, 165, 0, 0), std::nullopt},
             {counts(165, 166, 0, 0), "bad-blk-shielded-cost"},
             {counts(30, 0, 300, 0), std::nullopt},
             {counts(31, 0, 300, 0), "bad-blk-shielded-cost"}}) {
        EXPECT_EQ(c.ExceededLimit(), expected)
            << c.orchardActions << " " << c.ironwoodActions << " " << c.saplingIOs << " " << c.sproutJoinSplits;
    }

    // Counts add up across a block's transactions, and JoinSplits cost twice.
    ShieldedActionCounts total = counts(100, 0, 0, 0);
    total += counts(0, 100, 100, 1);
    EXPECT_EQ(total.orchardActions, 100);
    EXPECT_EQ(total.ironwoodActions, 100);
    EXPECT_EQ(total.saplingIOs, 100);
    EXPECT_EQ(total.sproutJoinSplits, 1);
    EXPECT_EQ(total.Cost(), 302);

    // A transaction without shielded parts counts as zero.
    CMutableTransaction mtx;
    const ShieldedActionCounts empty{CTransaction(mtx)};
    EXPECT_EQ(empty.Cost(), 0);
}

TEST(NU7Transactions, CoinbaseSaplingOutputs) {
    LoadProofParameters();
    RegtestActivateNU7(false, 10);
    const CChainParams& chainparams = Params();

    boost::shared_ptr<CReserveScript> script(new CReserveScript());
    script->reserveScript = CScript() << OP_TRUE;
    const auto saplingAddress = GetTestMasterSaplingSpendingKey().ToXFVK().DefaultAddress();

    // A block template reserves exactly the Sapling outputs its coinbase will have.
    for (const auto& [minerAddress, expected] : std::vector<std::pair<MinerAddress, uint64_t>>{
             {script, 0}, {saplingAddress, 1}}) {
        const CTransaction coinbase(CreateCoinbaseTransaction(chainparams, 0, 0, minerAddress, 10));
        EXPECT_EQ(coinbase.GetSaplingOutputsCount(), expected);
        EXPECT_EQ(CoinbaseSaplingOutputs(chainparams, minerAddress, 10), expected);
    }

    RegtestDeactivateNU7();
}
