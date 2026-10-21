#include <gtest/gtest.h>

#include "amount.h"
#include "chainparams.h"

#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// ZIP 235 fee burn and ZIP 237 NSM vectors. Values come from Zakura's tests
// (zakura-chain value_balance and parameters/network tests) and its differential dump.

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

/** Public network consensus parameters with NU7 at nu7Height, which need not be a multiple of 3. */
static Consensus::Params WithNU7(const std::string& chain, int nu7Height)
{
    Consensus::Params params = Params(chain).GetConsensus();
    params.vUpgrades[Consensus::UPGRADE_NU7].nActivationHeight = nu7Height;
    return params;
}

/** ceil(BLOCK_SUBSIDY_FRACTION * (MAX_MONEY - ScheduledIssuance(height - 1))), the reference payout. */
static CAmount ReferencePayout(const Consensus::Params& params, int height)
{
    const CAmount reserve = MAX_MONEY - params.ScheduledIssuance(height - 1);
    return (reserve * Consensus::NSM_BLOCK_SUBSIDY_FRACTION_NUMERATOR + Consensus::NSM_BLOCK_SUBSIDY_FRACTION_DENOMINATOR - 1)
        / Consensus::NSM_BLOCK_SUBSIDY_FRACTION_DENOMINATOR;
}

TEST(NU7NSM, InitialBalancesMatchTheMeasuredIssuedSupply) {
    // INITIAL_NSM_VALUE_BALANCE = ScheduledIssuance(H) - IssuedSupply(H), with the issued
    // supply Zakura measured on each chain at the last height before NU6.
    for (const auto& [chain, height, scheduled, issued, expected] :
         std::vector<std::tuple<std::string, int, CAmount, CAmount, CAmount>>{
             {CBaseChainParams::MAIN, 2726399, 1575000000000000, 1574963141554480, 36858445520},
             {CBaseChainParams::TESTNET, 2975999, 1603125000000000, 1603069231585043, 55768414957}}) {
        const auto& params = Params(chain).GetConsensus();
        EXPECT_EQ(params.ScheduledIssuance(height), scheduled) << chain;
        EXPECT_EQ(scheduled - issued, expected) << chain;
        EXPECT_EQ(params.nInitialNSMValueBalance, expected) << chain;
        EXPECT_TRUE(params.fCheckInitialNSMValueBalance) << chain;
    }

    // Regtest derives its initial balance from the chain supply unless one is configured.
    const auto& regtest = Params(CBaseChainParams::REGTEST).GetConsensus();
    EXPECT_EQ(regtest.nInitialNSMValueBalance, std::nullopt);
    EXPECT_FALSE(regtest.fCheckInitialNSMValueBalance);
}

TEST(NU7NSM, ScheduledIssuanceSumsTheBlockSubsidy) {
    // A block-by-block sum over the slow start, Blossom and the first two halvings.
    for (const auto& [chain, lastHeight] : std::vector<std::pair<std::string, int>>{
             {CBaseChainParams::MAIN, 2726399}, {CBaseChainParams::TESTNET, 2975999}}) {
        const auto& params = Params(chain).GetConsensus();
        CAmount total = 0;
        for (int height = 1; height <= lastHeight; height++) {
            total += params.GetBlockSubsidy(height);
            if (height % 100000 == 0 || height < 40000) {
                ASSERT_EQ(params.ScheduledIssuance(height), total) << chain << " " << height;
            }
        }
        EXPECT_EQ(params.ScheduledIssuance(lastHeight), total) << chain;
    }
    EXPECT_EQ(Params(CBaseChainParams::MAIN).GetConsensus().ScheduledIssuance(0), 0);

    // Around every subsidy change with NU7, the sum moves by exactly one block subsidy.
    const auto mainnet = CreateChainParamsWithNU7ForTesting(CBaseChainParams::MAIN, 3543000);
    const auto& params = mainnet->GetConsensus();
    std::vector<int> boundaries{1, 9999, 10000, 19999, 20000, 653600, 1046400, 2726400, 3543000};
    for (int halving = 3; halving <= 6; halving++) {
        boundaries.push_back(params.HeightForHalving(halving).value());
    }
    for (int boundary : boundaries) {
        for (int height = std::max(1, boundary - 2); height <= boundary + 2; height++) {
            EXPECT_EQ(params.ScheduledIssuance(height) - params.ScheduledIssuance(height - 1),
                      params.GetBlockSubsidy(height))
                << "height " << height;
        }
    }

    // Regtest has no slow start and halves every 144 blocks before Blossom.
    const auto regtest = Nu7RegtestParams(300);
    CAmount total = 0;
    for (int height = 1; height <= 5000; height++) {
        total += regtest.GetBlockSubsidy(height);
        ASSERT_EQ(regtest.ScheduledIssuance(height), total) << "regtest " << height;
    }
}

TEST(NU7NSM, ReissuanceHeights) {
    // Mainnet NU7 at 3,543,000 (Zakura's estimated-height test) and the Testnet
    // scenarios from the differential dump.
    const auto mainnet = CreateChainParamsWithNU7ForTesting(CBaseChainParams::MAIN, 3543000);
    EXPECT_EQ(mainnet->GetConsensus().NSMReissuanceHeight(), 8940474);
    const auto testnet = CreateChainParamsWithNU7ForTesting(CBaseChainParams::TESTNET, 4200000);
    EXPECT_EQ(testnet->GetConsensus().NSMReissuanceHeight(), 7835274);
    EXPECT_EQ(WithNU7(CBaseChainParams::TESTNET, 4187001).NSMReissuanceHeight(), 7861272);

    // Each NU7 block before the third halving pays a third of the pre-NU7 subsidy for three
    // times as many blocks, so the start height is a fixed offset from 3 * H3 - 2 * A.
    for (int nu7 : {3428145, 3543000, 3600001, 4000000, 4406399}) {
        EXPECT_EQ(WithNU7(CBaseChainParams::MAIN, nu7).NSMReissuanceHeight(), 16026474 - 2 * nu7) << nu7;
    }
    for (int nu7 : {4134000, 4187001, 4200000, 4386000, 4475999}) {
        EXPECT_EQ(WithNU7(CBaseChainParams::TESTNET, nu7).NSMReissuanceHeight(), 16235274 - 2 * nu7) << nu7;
    }

    // The start height is the first height after the third halving at which the reference
    // payout is below the scheduled subsidy.
    for (const auto& params : {WithNU7(CBaseChainParams::MAIN, 3543000), WithNU7(CBaseChainParams::TESTNET, 4187001)}) {
        const int start = params.NSMReissuanceHeight().value();
        EXPECT_GT(start, params.HeightForHalving(3).value());
        EXPECT_LT(start, params.HeightForHalving(4).value());
        EXPECT_LT(ReferencePayout(params, start), params.GetBlockSubsidy(start));
        EXPECT_GE(ReferencePayout(params, start - 1), params.GetBlockSubsidy(start - 1));
        EXPECT_FALSE(params.IsNSMReissuanceActive(start - 1));
        EXPECT_TRUE(params.IsNSMReissuanceActive(start));
    }

    // No NU7, or a schedule too short to drain the reserve (regtest), means no reissuance.
    EXPECT_EQ(WithNU7(CBaseChainParams::MAIN, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT).NSMReissuanceHeight(), std::nullopt);
    EXPECT_EQ(Nu7RegtestParams(1).NSMReissuanceHeight(), std::nullopt);
    EXPECT_EQ(Nu7RegtestParams(300).NSMReissuanceHeight(), std::nullopt);
    EXPECT_FALSE(Nu7RegtestParams(300).IsNSMReissuanceActive(1000000));

    // The test override never starts reissuance before NU7.
    auto regtest = Nu7RegtestParams(300);
    regtest.nTestNSMReissuanceHeight = 250;
    EXPECT_EQ(regtest.NSMReissuanceHeight(), 300);
    regtest.nTestNSMReissuanceHeight = 310;
    EXPECT_EQ(regtest.NSMReissuanceHeight(), 310);
    EXPECT_FALSE(regtest.IsNSMReissuanceActive(309));
    EXPECT_TRUE(regtest.IsNSMReissuanceActive(310));
}

TEST(NU7NSM, AdditionalBlockSubsidyRoundsUp) {
    const auto mainnet = CreateChainParamsWithNU7ForTesting(CBaseChainParams::MAIN, 3543000);
    const auto& params = mainnet->GetConsensus();
    const int start = 8940474;

    // ceil(1375 * balance / 10^10): a positive balance always pays at least one zatoshi.
    for (const auto& [balance, expected] : std::vector<std::pair<CAmount, CAmount>>{
             {0, 0},
             {1, 1},
             {100000000, 14},
             {9999999999, 1375},
             {10000000000, 1375},
             {10000000001, 1376},
             {1000000000000, 137500},
             {21000000000000, 2887500},
             {500000000000000, 68750000},
             {1050000000000000, 144375000},
             {MAX_MONEY, 288750000}}) {
        EXPECT_EQ(params.AdditionalBlockSubsidy(start, balance), expected) << balance;
        EXPECT_EQ(params.AdditionalBlockSubsidy(start - 1, balance), 0) << balance;
    }
}

TEST(NU7NSM, MinerFeeShare) {
    const auto params = Nu7RegtestParams(300);

    // From NU7 the miner keeps TransactionFees - floor(6 * TransactionFees / 10).
    for (const auto& [fees, share] : std::vector<std::pair<CAmount, CAmount>>{
             {0, 0}, {1, 1}, {2, 1}, {3, 2}, {10, 4}, {1000, 400}, {1001, 401}, {20002, 8001},
             {MAX_MONEY, 840000000000000}}) {
        EXPECT_EQ(params.MinerFeeShare(300, fees), share) << fees;
        EXPECT_EQ(params.MinerFeeShare(299, fees), fees) << fees;
    }
    for (CAmount fees = 0; fees <= 2000; fees++) {
        EXPECT_EQ(fees - params.MinerFeeShare(300, fees), (6 * fees) / 10) << fees;
    }
}

TEST(NU7NSM, FundingStreamsEndBeforeReissuance) {
    // init.cpp refuses to start if a funding stream is active once reissuance starts.
    for (const auto& [chain, nu7] : std::vector<std::pair<std::string, int>>{
             {CBaseChainParams::MAIN, 3543000}, {CBaseChainParams::TESTNET, 4200000}}) {
        const auto chainparams = CreateChainParamsWithNU7ForTesting(chain, nu7);
        const auto& params = chainparams->GetConsensus();
        const int start = params.NSMReissuanceHeight().value();
        for (const auto& fs : params.vFundingStreams) {
            if (fs) {
                EXPECT_LE(fs->GetEndHeight(), start) << chain;
            }
        }
    }
}
