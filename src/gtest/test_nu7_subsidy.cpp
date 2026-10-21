#include <gtest/gtest.h>

#include "chainparams.h"
#include "consensus/funding.h"
#include "consensus/upgrades.h"
#include "key_io.h"
#include "script/standard.h"

#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// ZIP 218 halving/subsidy and ZIP 207 Rev 2 / ZIP 214 Rev 3 / ZIP 2008 funding stream
// vectors. Values come from Zakura's tests (zakura-chain parameters/network/tests) and
// its differential dumps, which Zakura's NU7 implementation was verified against.

/** Regtest consensus parameters with every upgrade through NU6.3 at height 1 and NU7 at nu7Height. */
static Consensus::Params Nu7RegtestParams(int nu7Height)
{
    Consensus::Params params = Params(CBaseChainParams::REGTEST).GetConsensus();
    for (int i = Consensus::UPGRADE_OVERWINTER; i <= Consensus::UPGRADE_NU6_3; i++) {
        params.vUpgrades[i].nActivationHeight = 1;
    }
    params.vUpgrades[Consensus::UPGRADE_NU7].nActivationHeight = nu7Height;
    return params;
}

/** The value of the stream `idx` at nHeight, or 0 if it is inactive. */
static CAmount StreamValue(const Consensus::Params& params, Consensus::FundingStreamIndex idx, int nHeight)
{
    const auto& fs = params.vFundingStreams[idx];
    if (!fs || nHeight < fs->GetStartHeight() || nHeight >= fs->GetEndHeight()) {
        return 0;
    }
    return Consensus::FundingStreamInfo[idx].Value(params.GetBlockSubsidy(nHeight));
}

TEST(NU7Subsidy, PublicNetworkHalvingsMoveWithNU7) {
    // Without NU7, the third halving stays at its pre-NU7 height.
    const auto withoutNU7 = CreateChainParamsWithNU7ForTesting(
        CBaseChainParams::MAIN, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    EXPECT_EQ(withoutNU7->GetConsensus().HeightForHalving(3), 4406400);

    // With NU7 at A, the third halving is at A + 3 * (H3 - A), and the fourth one
    // PostNU7HalvingInterval (5,040,000) blocks later.
    for (const auto& [chain, nu7, h3, h4] : std::vector<std::tuple<std::string, int, int, int>>{
             {CBaseChainParams::MAIN, 3543000, 6133200, 11173200},
             {CBaseChainParams::TESTNET, 4386000, 4656000, 9696000},
             {CBaseChainParams::TESTNET, 4200000, 5028000, 10068000}}) {
        const auto chainparams = CreateChainParamsWithNU7ForTesting(chain, nu7);
        const auto& params = chainparams->GetConsensus();
        EXPECT_EQ(params.HeightForHalving(3), h3) << chain << " NU7 " << nu7;
        EXPECT_EQ(params.HeightForHalving(4), h4) << chain << " NU7 " << nu7;

        // Scheduled subsidy: floor(12.5 ZEC / (2 * 3 * 2^Halving)) from NU7.
        EXPECT_EQ(params.GetBlockSubsidy(nu7 - 1), 156250000) << chain;
        EXPECT_EQ(params.GetBlockSubsidy(nu7), 52083333) << chain;
        EXPECT_EQ(params.GetBlockSubsidy(h3 - 1), 52083333) << chain;
        EXPECT_EQ(params.GetBlockSubsidy(h3), 26041666) << chain;
        EXPECT_EQ(params.GetBlockSubsidy(h4), 13020833) << chain;
        EXPECT_EQ(params.PoWTargetSpacing(nu7 - 1), 75) << chain;
        EXPECT_EQ(params.PoWTargetSpacing(nu7), 25) << chain;
    }

    // Testnet NU7 at 4,200,000: every halving height the differential dump recorded.
    const auto testnet = CreateChainParamsWithNU7ForTesting(CBaseChainParams::TESTNET, 4200000);
    const std::vector<int> expected{1116000, 2796000, 5028000, 10068000, 15108000, 20148000};
    for (size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(testnet->GetConsensus().HeightForHalving(i + 1), expected[i]) << "halving " << i + 1;
    }
}

TEST(NU7Subsidy, MainnetSchedule) {
    // These are Zakura's built-in Mainnet schedule and its derived consensus values.
    const auto& params = Params(CBaseChainParams::MAIN).GetConsensus();
    constexpr int nu7 = 3543405;
    EXPECT_EQ(params.vUpgrades[Consensus::UPGRADE_NU7].nActivationHeight, nu7);
    EXPECT_FALSE(params.NetworkUpgradeActive(nu7 - 1, Consensus::UPGRADE_NU7));
    EXPECT_TRUE(params.NetworkUpgradeActive(nu7, Consensus::UPGRADE_NU7));
    EXPECT_EQ(CurrentEpochBranchId(nu7 - 1, params), NetworkUpgradeInfo[Consensus::UPGRADE_NU6_3].nBranchId);
    EXPECT_EQ(CurrentEpochBranchId(nu7, params), 0x77190ad9);
    EXPECT_EQ(params.PoWTargetSpacing(nu7 - 1), 75);
    EXPECT_EQ(params.PoWTargetSpacing(nu7), 25);
    EXPECT_EQ(params.PoWAveragingWindow(nu7 - 1), 17);
    EXPECT_EQ(params.PoWAveragingWindow(nu7), 102);

    EXPECT_EQ(params.HeightForHalving(3), 6132390);
    EXPECT_EQ(params.HeightForHalving(4), 11172390);
    EXPECT_EQ(params.GetBlockSubsidy(nu7 - 1), 156250000);
    EXPECT_EQ(params.GetBlockSubsidy(nu7), 52083333);
    EXPECT_EQ(params.GetBlockSubsidy(6132389), 52083333);
    EXPECT_EQ(params.GetBlockSubsidy(6132390), 26041666);
    EXPECT_EQ(params.vFundingStreams[Consensus::FS_FPF_ZCG_H3]->GetEndHeight(), 6132390);
    EXPECT_EQ(params.vFundingStreams[Consensus::FS_CCF_H3]->GetEndHeight(), 6132390);
    EXPECT_EQ(StreamValue(params, Consensus::FS_FPF_ZCG_H3, nu7), 4166666);
    EXPECT_EQ(StreamValue(params, Consensus::FS_CCF_H3, nu7), 6249999);
    EXPECT_EQ(StreamValue(params, Consensus::FS_FPF_ZCG_H3, 6132390), 0);
    EXPECT_EQ(StreamValue(params, Consensus::FS_CCF_H3, 6132390), 0);
    EXPECT_EQ(params.NSMReissuanceHeight(), 8939664);

    const auto& stream = *params.vFundingStreams[Consensus::FS_FPF_ZCG_H3];
    const KeyIO keyIO(Params(CBaseChainParams::MAIN));
    const CScript oldRecipient = GetScriptForDestination(
        keyIO.DecodeDestination("t3cFfPt1Bcvgez9ZbMBFWeZsskxTkPzGCow"));
    const CScript newRecipient = GetScriptForDestination(
        keyIO.DecodeDestination("t1MkHnkxVjNpNbCrSs3AJ8J7ZSp6NTYiUcG"));
    // ZIP 2008 switches recipients at the first address period after activation.
    for (int height : {nu7 - 1, nu7, 3612389}) {
        EXPECT_EQ(std::get<CScript>(stream.Recipient(params, height)), oldRecipient) << height;
    }
    for (int height : {3612390, 6132389}) {
        EXPECT_EQ(std::get<CScript>(stream.Recipient(params, height)), newRecipient) << height;
    }
}

TEST(NU7Subsidy, TestnetSchedule) {
    // NU7 activates on Testnet at 4,465,026. Zakura derives the same values from that height.
    const auto& params = Params(CBaseChainParams::TESTNET).GetConsensus();
    const int nu7 = 4465026;
    EXPECT_EQ(params.vUpgrades[Consensus::UPGRADE_NU7].nActivationHeight, nu7);
    EXPECT_EQ(CurrentEpochBranchId(nu7 - 1, params), NetworkUpgradeInfo[Consensus::UPGRADE_NU6_3].nBranchId);
    EXPECT_EQ(CurrentEpochBranchId(nu7, params), 0x77190ad9);

    // ZIP 218 spacing and averaging window; the minimum-difficulty gap stays 450 seconds.
    EXPECT_EQ(params.PoWTargetSpacing(nu7 - 1), 75);
    EXPECT_EQ(params.PoWTargetSpacing(nu7), 25);
    EXPECT_EQ(params.PoWAveragingWindow(nu7 - 1), 17);
    EXPECT_EQ(params.PoWAveragingWindow(nu7), 102);
    for (int height : {nu7 - 1, nu7, nu7 + 1}) {
        EXPECT_EQ(params.MinDifficultyGap(height), 450) << height;
    }

    // The third halving moves to 4,497,948, where the Revision 2 streams now end.
    EXPECT_EQ(params.HeightForHalving(3), 4497948);
    EXPECT_EQ(params.HeightForHalving(4), 9537948);
    EXPECT_EQ(params.GetBlockSubsidy(nu7 - 1), 156250000);
    EXPECT_EQ(params.GetBlockSubsidy(nu7), 52083333);
    EXPECT_EQ(params.GetBlockSubsidy(4497947), 52083333);
    EXPECT_EQ(params.GetBlockSubsidy(4497948), 26041666);
    EXPECT_EQ(params.vFundingStreams[Consensus::FS_FPF_ZCG_H3]->GetEndHeight(), 4497948);
    EXPECT_EQ(params.vFundingStreams[Consensus::FS_CCF_H3]->GetEndHeight(), 4497948);
    EXPECT_EQ(StreamValue(params, Consensus::FS_FPF_ZCG_H3, nu7), 4166666);
    EXPECT_EQ(StreamValue(params, Consensus::FS_CCF_H3, nu7), 6249999);
    EXPECT_EQ(StreamValue(params, Consensus::FS_FPF_ZCG_H3, 4497948), 0);

    // ZIP 237 reissuance starts at 16,235,274 - 2 * 4,465,026.
    EXPECT_EQ(params.NSMReissuanceHeight(), 7305222);
}

TEST(NU7Subsidy, RegtestHalvings) {
    // Regtest with everything through NU6.3 at 1 and NU7 at 300: Halving(h) = (h + 603) / 864.
    const Consensus::Params params = Nu7RegtestParams(300);
    const std::vector<int> expected{287, 1125, 1989, 2853, 3717, 4581};
    for (size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(params.HeightForHalving(i + 1), expected[i]) << "halving " << i + 1;
    }
    EXPECT_EQ(params.GetBlockSubsidy(299), 312500000);
    EXPECT_EQ(params.GetBlockSubsidy(300), 104166666);
    EXPECT_EQ(params.HeightForHalving(0), 0);
    // As in Zakura, any halving reached by the largest height has a height.
    EXPECT_EQ(params.HeightForHalving(64), 54693);
    const int lastHalving = params.Halving(std::numeric_limits<int>::max());
    EXPECT_EQ(params.HeightForHalving(lastHalving + 1), std::nullopt);
}

TEST(NU7Subsidy, Revision2StreamsEndAtTheMovedThirdHalving) {
    // Testnet NU7 at 4,386,000: the Revision 2 streams end at 4,656,000 instead of 4,476,000,
    // paying 8% to FPF and 12% to the lockbox of the NU7 subsidy until then.
    const auto testnet = CreateChainParamsWithNU7ForTesting(CBaseChainParams::TESTNET, 4386000);
    const auto& params = testnet->GetConsensus();
    EXPECT_EQ(params.vFundingStreams[Consensus::FS_FPF_ZCG_H3]->GetEndHeight(), 4656000);
    EXPECT_EQ(params.vFundingStreams[Consensus::FS_CCF_H3]->GetEndHeight(), 4656000);
    for (int height : {4475999, 4476000, 4655999}) {
        EXPECT_EQ(StreamValue(params, Consensus::FS_FPF_ZCG_H3, height), 4166666) << height;
        EXPECT_EQ(StreamValue(params, Consensus::FS_CCF_H3, height), 6249999) << height;
    }
    EXPECT_EQ(StreamValue(params, Consensus::FS_FPF_ZCG_H3, 4656000), 0);
    EXPECT_EQ(StreamValue(params, Consensus::FS_CCF_H3, 4656000), 0);

    // Mainnet NU7 at 3,543,000: the streams end at the moved third halving, 6,133,200.
    const auto mainnet = CreateChainParamsWithNU7ForTesting(CBaseChainParams::MAIN, 3543000);
    EXPECT_EQ(mainnet->GetConsensus().vFundingStreams[Consensus::FS_FPF_ZCG_H3]->GetEndHeight(), 6133200);
    EXPECT_EQ(mainnet->GetConsensus().vFundingStreams[Consensus::FS_CCF_H3]->GetEndHeight(), 6133200);

    // Without NU7 the Revision 2 end heights are unchanged.
    const auto withoutNU7 = CreateChainParamsWithNU7ForTesting(
        CBaseChainParams::MAIN, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    EXPECT_EQ(withoutNU7->GetConsensus().vFundingStreams[Consensus::FS_FPF_ZCG_H3]->GetEndHeight(), 4406400);
    EXPECT_EQ(Consensus::NU7AdjustedFundingStreamHeight(4476000, std::nullopt), 4476000);
    EXPECT_EQ(Consensus::NU7AdjustedFundingStreamHeight(4476000, 4476000), 4476000);
    EXPECT_EQ(Consensus::NU7AdjustedFundingStreamHeight(4476000, 4476003), 4476000);
}

TEST(NU7Subsidy, AddressPeriodsTripleAfterNU7) {
    // Testnet NU7 at 4,200,000: the period boundary at or after activation is 4,293,000,
    // and later boundaries are 105,000 (3 * 35,000) blocks apart.
    const auto testnet = CreateChainParamsWithNU7ForTesting(CBaseChainParams::TESTNET, 4200000);
    const auto& params = testnet->GetConsensus();
    std::vector<int> boundaries;
    for (int height = 4200000; boundaries.size() < 7 && height < 5028000; height++) {
        if (params.FundingStreamAddressPeriod(height) != params.FundingStreamAddressPeriod(height - 1)) {
            boundaries.push_back(height);
        }
    }
    EXPECT_EQ(boundaries, (std::vector<int>{4293000, 4398000, 4503000, 4608000, 4713000, 4818000, 4923000}));

    // The period does not jump at activation, and before NU7 it is unchanged.
    EXPECT_EQ(params.FundingStreamAddressPeriod(4200000), params.FundingStreamAddressPeriod(4199999));
    const auto& noNu7 = Params(CBaseChainParams::TESTNET).GetConsensus();
    for (int height : {3536500, 4000000, 4199999}) {
        EXPECT_EQ(params.FundingStreamAddressPeriod(height), noNu7.FundingStreamAddressPeriod(height)) << height;
    }
}

TEST(NU7Subsidy, Zip2008RotatesTheFpfRecipient) {
    const CScript oldRecipient = GetScriptForDestination(
        KeyIO(Params(CBaseChainParams::MAIN)).DecodeDestination("t3cFfPt1Bcvgez9ZbMBFWeZsskxTkPzGCow"));
    const CScript newRecipient = GetScriptForDestination(
        KeyIO(Params(CBaseChainParams::MAIN)).DecodeDestination("t1MkHnkxVjNpNbCrSs3AJ8J7ZSp6NTYiUcG"));

    // N = AddressIndex(A - 1) + 1: the first list index that pays the ZIP 2008 address.
    for (const auto& [nu7, firstNewIndex] : std::vector<std::pair<int, int>>{
             {3566400, 12}, {3566403, 13}, {3601398, 13}}) {
        const auto mainnet = CreateChainParamsWithNU7ForTesting(CBaseChainParams::MAIN, nu7);
        const auto& params = mainnet->GetConsensus();
        const auto& fs = *params.vFundingStreams[Consensus::FS_FPF_ZCG_H3];
        const auto& recipients = fs.GetRecipients();
        ASSERT_EQ(recipients.size(), 36);
        for (int i = 0; i < 36; i++) {
            const CScript& expected = i < firstNewIndex ? oldRecipient : newRecipient;
            EXPECT_EQ(std::get<CScript>(recipients[i]), expected) << "NU7 " << nu7 << " index " << i;
        }
        // The block before activation still pays the old address.
        EXPECT_EQ(std::get<CScript>(fs.Recipient(params, nu7 - 1)), oldRecipient) << nu7;
    }

    // When A starts a period (3,566,400), the new address is paid from A itself.
    const auto mainnet = CreateChainParamsWithNU7ForTesting(CBaseChainParams::MAIN, 3566400);
    const auto& params = mainnet->GetConsensus();
    EXPECT_EQ(std::get<CScript>(params.vFundingStreams[Consensus::FS_FPF_ZCG_H3]->Recipient(params, 3566400)),
              newRecipient);
}
