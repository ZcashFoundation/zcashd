#include <gtest/gtest.h>

#include "chainparams.h"
#include "consensus/upgrades.h"
#include "util/test.h"
#include "version.h"

#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

class UpgradesTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
        // Revert to default
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    }
};

TEST(MainnetUpgradeTest, NU6_3Activation) {
    const auto& params = Params(CBaseChainParams::MAIN).GetConsensus();
    constexpr int activationHeight = 3428143;

    EXPECT_EQ(
        params.vUpgrades[Consensus::UPGRADE_NU6_3].nActivationHeight,
        activationHeight);
    EXPECT_FALSE(params.NetworkUpgradeActive(
        activationHeight - 1, Consensus::UPGRADE_NU6_3));
    EXPECT_TRUE(params.NetworkUpgradeActive(
        activationHeight, Consensus::UPGRADE_NU6_3));
}

TEST(UpgradeTable, Complete) {
    std::set<uint32_t> branchIds;
    for (int i = Consensus::BASE_SPROUT; i < Consensus::MAX_NETWORK_UPGRADES; i++) {
        const auto& info = NetworkUpgradeInfo[i];
        EXPECT_FALSE(info.strName.empty()) << "upgrade index " << i;
        EXPECT_TRUE(branchIds.insert(info.nBranchId).second) << info.strName;
        EXPECT_EQ(info.nBranchId == 0, i == Consensus::BASE_SPROUT) << info.strName;
    }
    EXPECT_EQ(NetworkUpgradeInfo[Consensus::UPGRADE_NU7].nBranchId, 0x77190ad9);
    EXPECT_EQ(NetworkUpgradeInfo[Consensus::UPGRADE_NU7].strName, "NU7");
    EXPECT_EQ(Consensus::UPGRADE_NU7 + 1, Consensus::UPGRADE_ZFUTURE);
    EXPECT_EQ(NetworkUpgradeInfo[Consensus::UPGRADE_ZFUTURE].nBranchId, 0xffffffff);

    // ZIP 204: NU7 is 170190 on Mainnet and 170180 on Testnet and Regtest.
    // The public activation heights match Zakura; default Regtest stays unscheduled.
    constexpr int unscheduled = Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
    for (const auto& [network, protocolVersion, activationHeight] : std::vector<std::tuple<std::string, int, int>>{
             {CBaseChainParams::MAIN, 170190, 3543405},
             {CBaseChainParams::TESTNET, 170180, 4465026},
             {CBaseChainParams::REGTEST, 170180, unscheduled}}) {
        const auto& nu7 = Params(network).GetConsensus().vUpgrades[Consensus::UPGRADE_NU7];
        EXPECT_EQ(nu7.nProtocolVersion, protocolVersion) << network;
        EXPECT_EQ(nu7.nActivationHeight, activationHeight) << network;
        EXPECT_GE(PROTOCOL_VERSION, nu7.nProtocolVersion) << network;
    }
}

TEST(UpgradeTable, RustBranchIdMatchesCpp) {
    auto expectSameBranchIds = [](const CChainParams& chainparams) {
        const auto& params = chainparams.GetConsensus();
        auto rustNetwork = chainparams.RustNetwork();
        for (int i = Consensus::UPGRADE_OVERWINTER; i < Consensus::UPGRADE_ZFUTURE; i++) {
            const int activationHeight = params.vUpgrades[i].nActivationHeight;
            if (activationHeight <= 0) {
                continue;
            }
            for (int height : {activationHeight - 1, activationHeight}) {
                EXPECT_EQ(consensus::branch_id(*rustNetwork, height), CurrentEpochBranchId(height, params))
                    << chainparams.NetworkIDString() << " height " << height;
            }
        }
    };
    expectSameBranchIds(Params(CBaseChainParams::MAIN));
    expectSameBranchIds(Params(CBaseChainParams::TESTNET));

    const auto& params = RegtestActivateNU7(false, 150);
    auto rustNetwork = Params().RustNetwork();
    EXPECT_EQ(consensus::branch_id(*rustNetwork, 149), NetworkUpgradeInfo[Consensus::UPGRADE_NU6_3].nBranchId);
    EXPECT_EQ(consensus::branch_id(*rustNetwork, 150), 0x77190ad9);
    EXPECT_EQ(CurrentEpochBranchId(150, params), 0x77190ad9);
    RegtestDeactivateNU7();
}

TEST_F(UpgradesTest, NetworkUpgradeState) {
    SelectParams(CBaseChainParams::REGTEST);
    const Consensus::Params& params = Params().GetConsensus();

    // Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT
    EXPECT_EQ(
        NetworkUpgradeState(0, params, Consensus::UPGRADE_TESTDUMMY),
        UPGRADE_DISABLED);
    EXPECT_EQ(
        NetworkUpgradeState(1000000, params, Consensus::UPGRADE_TESTDUMMY),
        UPGRADE_DISABLED);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    EXPECT_EQ(
        NetworkUpgradeState(0, params, Consensus::UPGRADE_TESTDUMMY),
        UPGRADE_ACTIVE);
    EXPECT_EQ(
        NetworkUpgradeState(1000000, params, Consensus::UPGRADE_TESTDUMMY),
        UPGRADE_ACTIVE);

    int nActivationHeight = 100;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, nActivationHeight);

    EXPECT_EQ(
        NetworkUpgradeState(0, params, Consensus::UPGRADE_TESTDUMMY),
        UPGRADE_PENDING);
    EXPECT_EQ(
        NetworkUpgradeState(nActivationHeight - 1, params, Consensus::UPGRADE_TESTDUMMY),
        UPGRADE_PENDING);
    EXPECT_EQ(
        NetworkUpgradeState(nActivationHeight, params, Consensus::UPGRADE_TESTDUMMY),
        UPGRADE_ACTIVE);
    EXPECT_EQ(
        NetworkUpgradeState(1000000, params, Consensus::UPGRADE_TESTDUMMY),
        UPGRADE_ACTIVE);
}

TEST_F(UpgradesTest, CurrentEpoch) {
    SelectParams(CBaseChainParams::REGTEST);
    const Consensus::Params& params = Params().GetConsensus();
    auto nBranchId = NetworkUpgradeInfo[Consensus::UPGRADE_TESTDUMMY].nBranchId;

    // Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT
    EXPECT_EQ(CurrentEpoch(0, params), Consensus::BASE_SPROUT);
    EXPECT_EQ(CurrentEpochBranchId(0, params), 0);
    EXPECT_EQ(CurrentEpoch(1000000, params), Consensus::BASE_SPROUT);
    EXPECT_EQ(CurrentEpochBranchId(1000000, params), 0);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    EXPECT_EQ(CurrentEpoch(0, params), Consensus::UPGRADE_TESTDUMMY);
    EXPECT_EQ(CurrentEpochBranchId(0, params), nBranchId);
    EXPECT_EQ(CurrentEpoch(1000000, params), Consensus::UPGRADE_TESTDUMMY);
    EXPECT_EQ(CurrentEpochBranchId(1000000, params), nBranchId);

    int nActivationHeight = 100;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, nActivationHeight);

    EXPECT_EQ(CurrentEpoch(0, params), Consensus::BASE_SPROUT);
    EXPECT_EQ(CurrentEpochBranchId(0, params), 0);
    EXPECT_EQ(CurrentEpoch(nActivationHeight - 1, params), Consensus::BASE_SPROUT);
    EXPECT_EQ(CurrentEpochBranchId(nActivationHeight - 1, params), 0);
    EXPECT_EQ(CurrentEpoch(nActivationHeight, params), Consensus::UPGRADE_TESTDUMMY);
    EXPECT_EQ(CurrentEpochBranchId(nActivationHeight, params), nBranchId);
    EXPECT_EQ(CurrentEpoch(1000000, params), Consensus::UPGRADE_TESTDUMMY);
    EXPECT_EQ(CurrentEpochBranchId(1000000, params), nBranchId);
}

TEST_F(UpgradesTest, IsActivationHeight) {
    SelectParams(CBaseChainParams::REGTEST);
    const Consensus::Params& params = Params().GetConsensus();

    // Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT
    EXPECT_FALSE(IsActivationHeight(-1, params, Consensus::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(0, params, Consensus::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(1, params, Consensus::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(1000000, params, Consensus::UPGRADE_TESTDUMMY));

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    EXPECT_FALSE(IsActivationHeight(-1, params, Consensus::UPGRADE_TESTDUMMY));
    EXPECT_TRUE(IsActivationHeight(0, params, Consensus::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(1, params, Consensus::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(1000000, params, Consensus::UPGRADE_TESTDUMMY));

    int nActivationHeight = 100;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, nActivationHeight);

    EXPECT_FALSE(IsActivationHeight(-1, params, Consensus::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(0, params, Consensus::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(1, params, Consensus::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(nActivationHeight - 1, params, Consensus::UPGRADE_TESTDUMMY));
    EXPECT_TRUE(IsActivationHeight(nActivationHeight, params, Consensus::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(nActivationHeight + 1, params, Consensus::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(1000000, params, Consensus::UPGRADE_TESTDUMMY));
}

TEST_F(UpgradesTest, IsActivationHeightForAnyUpgrade) {
    SelectParams(CBaseChainParams::REGTEST);
    const Consensus::Params& params = Params().GetConsensus();

    // Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(-1, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(0, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(1, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(1000000, params));

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(-1, params));
    EXPECT_TRUE(IsActivationHeightForAnyUpgrade(0, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(1, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(1000000, params));

    int nActivationHeight = 100;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, nActivationHeight);

    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(-1, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(0, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(1, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(nActivationHeight - 1, params));
    EXPECT_TRUE(IsActivationHeightForAnyUpgrade(nActivationHeight, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(nActivationHeight + 1, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(1000000, params));
}

TEST_F(UpgradesTest, NextEpoch) {
    SelectParams(CBaseChainParams::REGTEST);
    const Consensus::Params& params = Params().GetConsensus();

    // Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT
    EXPECT_EQ(NextEpoch(-1, params), std::nullopt);
    EXPECT_EQ(NextEpoch(0, params), std::nullopt);
    EXPECT_EQ(NextEpoch(1, params), std::nullopt);
    EXPECT_EQ(NextEpoch(1000000, params), std::nullopt);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    EXPECT_EQ(NextEpoch(-1, params), std::nullopt);
    EXPECT_EQ(NextEpoch(0, params), std::nullopt);
    EXPECT_EQ(NextEpoch(1, params), std::nullopt);
    EXPECT_EQ(NextEpoch(1000000, params), std::nullopt);

    int nActivationHeight = 100;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, nActivationHeight);

    EXPECT_EQ(NextEpoch(-1, params), std::nullopt);
    EXPECT_EQ(NextEpoch(0, params), static_cast<int>(Consensus::UPGRADE_TESTDUMMY));
    EXPECT_EQ(NextEpoch(1, params), static_cast<int>(Consensus::UPGRADE_TESTDUMMY));
    EXPECT_EQ(NextEpoch(nActivationHeight - 1, params), static_cast<int>(Consensus::UPGRADE_TESTDUMMY));
    EXPECT_EQ(NextEpoch(nActivationHeight, params), std::nullopt);
    EXPECT_EQ(NextEpoch(nActivationHeight + 1, params), std::nullopt);
    EXPECT_EQ(NextEpoch(1000000, params), std::nullopt);
}

TEST_F(UpgradesTest, NextActivationHeight) {
    SelectParams(CBaseChainParams::REGTEST);
    const Consensus::Params& params = Params().GetConsensus();

    // Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT
    EXPECT_EQ(NextActivationHeight(-1, params), std::nullopt);
    EXPECT_EQ(NextActivationHeight(0, params), std::nullopt);
    EXPECT_EQ(NextActivationHeight(1, params), std::nullopt);
    EXPECT_EQ(NextActivationHeight(1000000, params), std::nullopt);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    EXPECT_EQ(NextActivationHeight(-1, params), std::nullopt);
    EXPECT_EQ(NextActivationHeight(0, params), std::nullopt);
    EXPECT_EQ(NextActivationHeight(1, params), std::nullopt);
    EXPECT_EQ(NextActivationHeight(1000000, params), std::nullopt);

    int nActivationHeight = 100;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, nActivationHeight);

    EXPECT_EQ(NextActivationHeight(-1, params), std::nullopt);
    EXPECT_EQ(NextActivationHeight(0, params), nActivationHeight);
    EXPECT_EQ(NextActivationHeight(1, params), nActivationHeight);
    EXPECT_EQ(NextActivationHeight(nActivationHeight - 1, params), nActivationHeight);
    EXPECT_EQ(NextActivationHeight(nActivationHeight, params), std::nullopt);
    EXPECT_EQ(NextActivationHeight(nActivationHeight + 1, params), std::nullopt);
    EXPECT_EQ(NextActivationHeight(1000000, params), std::nullopt);
}
