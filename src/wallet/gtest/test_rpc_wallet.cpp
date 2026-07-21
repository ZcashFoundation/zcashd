#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "main.h"
#include "primitives/transaction.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "transaction_builder.h"
#include "util/test.h"
#include "gtest/utils.h"
#include "wallet/asyncrpcoperation_common.h"
#include "wallet/asyncrpcoperation_shieldcoinbase.h"
#include "wallet/asyncrpcoperation_sendmany.h"
#include "zcash/JoinSplit.hpp"
#include "zcash/memo.h"
#include "zip317.h"

#include <librustzcash.h>
#include <rust/bridge.h>
#include <rust/ed25519.h>

#include <stdexcept>

namespace
{
bool find_error(const UniValue& objError, const std::string& expected) {
    return find_value(objError, "message").get_str().find(expected) != string::npos;
}

CWalletTx FakeWalletTx() {
    CMutableTransaction mtx;
    mtx.vout.resize(1);
    mtx.vout[0].nValue = 1;
    return CWalletTx(nullptr, mtx);
}

SpendableInputs MixedSaplingAndOrchardInputs(
        const SaplingPaymentAddress& saplingAddress,
        CAmount value)
{
    SpendableInputs inputs;
    inputs.saplingNoteEntries.push_back(SaplingNoteEntry{
        SaplingOutPoint{},
        saplingAddress,
        SaplingNote(saplingAddress, value, Zip212Enabled::AfterZip212),
        {},
        100});

    auto seed = MnemonicSeed::Random(0);
    auto orchardKey = OrchardSpendingKey::ForAccount(seed, 0, 0);
    auto orchardAddress = orchardKey.ToFullViewingKey()
        .ToIncomingViewingKey()
        .Address(diversifier_index_t{0});
    inputs.orchardNoteMetadata.push_back(OrchardNoteMetadata{
        OrchardOutPoint{},
        orchardAddress,
        value,
        {}});

    return inputs;
}

class ScopedFakeChainTip
{
private:
    uint256 blockHash;
    CBlockIndex fakeIndex;
    CBlockIndex* previousTip;

public:
    explicit ScopedFakeChainTip(const CBlock& block)
        : blockHash(block.GetHash()),
          fakeIndex(block),
          previousTip(chainActive.Tip())
    {
        if (!mapBlockIndex.insert(std::make_pair(blockHash, &fakeIndex)).second) {
            throw std::runtime_error("Fake block is already present in mapBlockIndex");
        }
        chainActive.SetTip(&fakeIndex);
    }

    ~ScopedFakeChainTip()
    {
        chainActive.SetTip(previousTip);
        mapBlockIndex.erase(blockHash);
    }

    CBlockIndex* Get()
    {
        return &fakeIndex;
    }

    ScopedFakeChainTip(const ScopedFakeChainTip&) = delete;
    ScopedFakeChainTip& operator=(const ScopedFakeChainTip&) = delete;
};

class ScopedNU6point3Wallet
{
public:
    ScopedNU6point3Wallet()
    {
        RegtestActivateNU6point3();
        LoadGlobalWallet();
    }

    ~ScopedNU6point3Wallet()
    {
        RegtestDeactivateNU6point3();
        UnloadGlobalWallet();
    }

    ScopedNU6point3Wallet(const ScopedNU6point3Wallet&) = delete;
    ScopedNU6point3Wallet& operator=(const ScopedNU6point3Wallet&) = delete;
};

/// Expects that the fee calculated during transaction construction matches the fee used by block
/// construction. It allows the fee included in the transaction to be `MARGINAL_FEE` higher than the
/// fee expected by block construction.
void ExpectConsistentFee(const TransactionStrategy& strategy, const TransactionEffects& effects)
{
    auto buildResult = effects.ApproveAndBuild(
            Params(),
            *pwalletMain,
            chainActive,
            strategy);
    auto tx = buildResult.GetTxOrThrow();

    auto expectedFee = tx.GetConventionalFee();
    // Allow one incremental fee tick of buffer above the conventional fee.
    EXPECT_TRUE(effects.GetFee() == expectedFee || effects.GetFee() == expectedFee + MARGINAL_FEE)
        << "effects.GetFee() = " << effects.GetFee() << std::endl
        << "tx.GetConventionalFee() = " << expectedFee;
}
}

TEST(WalletRPCTests, PrepareTransaction)
{
    LoadProofParameters();
    SelectParams(CBaseChainParams::TESTNET);

    LoadGlobalWallet();

    RegtestActivateSapling();
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        if (!pwalletMain->HaveMnemonicSeed()) {
            pwalletMain->GenerateNewSeed();
        }

        KeyIO keyIO(Params());
        // add keys manually
        auto taddr = pwalletMain->GenerateNewKey(true).GetID();
        auto pa = pwalletMain->GenerateNewLegacySaplingZKey();

        const Consensus::Params& consensusParams = Params().GetConsensus();

        int nextBlockHeight = chainActive.Height() + 1;

        // Add a fake transaction to the wallet
        CMutableTransaction mtx = CreateNewContextualCMutableTransaction(consensusParams, nextBlockHeight, false);
        CScript scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(taddr) << OP_EQUALVERIFY << OP_CHECKSIG;
        mtx.vout.push_back(CTxOut(5 * COIN, scriptPubKey));

        CWalletTx wtx(pwalletMain, mtx);
        pwalletMain->LoadWalletTx(wtx);

        // Fake-mine the transaction
        EXPECT_EQ(-1, chainActive.Height());
        CBlock block;
        block.vtx.push_back(wtx);
        block.hashMerkleRoot = BlockMerkleRoot(block);
        auto blockHash = block.GetHash();
        CBlockIndex fakeIndex {block};
        mapBlockIndex.insert(std::make_pair(blockHash, &fakeIndex));
        chainActive.SetTip(&fakeIndex);
        EXPECT_TRUE(chainActive.Contains(&fakeIndex));
        EXPECT_EQ(0, chainActive.Height());
        wtx.SetMerkleBranch(block);
        pwalletMain->LoadWalletTx(wtx);

        WalletTxBuilder builder(Params(), minRelayTxFee);

        auto selector = CWallet::LegacyTransparentZTXOSelector(
                true,
                TransparentCoinbasePolicy::Disallow);

        { // send from legacy account with change, but insufficient policy
            auto saplingKey = pwalletMain->GenerateNewLegacySaplingZKey();
            Payment saplingPayment(saplingKey, 4 * COIN, std::nullopt);
            std::vector<Payment> payments {saplingPayment};

            TransactionStrategy strategy(PrivacyPolicy::AllowRevealedSenders);

            SpendableInputs inputs;
            inputs.utxos.emplace_back(&wtx, 0, std::nullopt, 100, true);

            (void)builder.PrepareTransaction(
                    *pwalletMain,
                    selector,
                    inputs,
                    payments,
                    chainActive,
                    strategy,
                    std::nullopt,
                    1)
                .map_error([&](const auto& err) {
                    examine(err, match {
                        [](AddressResolutionError are) {
                            EXPECT_EQ(are, AddressResolutionError::TransparentChangeNotAllowed);
                        },
                        [&](const auto& e) {
                            try {
                                ThrowInputSelectionError(e, selector, strategy);
                            } catch (const UniValue& value) {
                                FAIL() << value.write();
                            }
                        },
                    });
                })
                .map([](const auto&) {
                    FAIL() << "Expected an error";
                });
        }

        // Tear down
        chainActive.SetTip(NULL);
        mapBlockIndex.erase(blockHash);

    }
    // Revert to default
    RegtestDeactivateSapling();
    UnloadGlobalWallet();
}

TEST(WalletRPCTests, PrepareTransactionAvoidsOrchardAfterNU6point3)
{
    ScopedNU6point3Wallet wallet;

    {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        if (!pwalletMain->HaveMnemonicSeed()) {
            pwalletMain->GenerateNewSeed();
        }

        EXPECT_EQ(-1, chainActive.Height());
        CBlock block;
        block.hashMerkleRoot = BlockMerkleRoot(block);
        ScopedFakeChainTip fakeTip(block);
        EXPECT_TRUE(chainActive.Contains(fakeTip.Get()));
        EXPECT_EQ(0, chainActive.Height());

        auto [ufvk, accountId] = pwalletMain->GenerateNewUnifiedSpendingKey();
        auto selector = pwalletMain->ZTXOSelectorForAccount(
                accountId,
                true,
                TransparentCoinbasePolicy::Disallow).value();
        auto sourceSaplingAddress =
            ufvk.GetSaplingKey().value().FindAddress(diversifier_index_t{0}).first;

        WalletTxBuilder builder(Params(), minRelayTxFee);

        auto transparentRecipient = pwalletMain->GenerateNewKey(true).GetID();
        std::vector<Payment> transparentPayments{
            Payment(transparentRecipient, COIN, std::nullopt)};
        auto transparentEffects = builder.PrepareTransaction(
                *pwalletMain,
                selector,
                MixedSaplingAndOrchardInputs(sourceSaplingAddress, 2 * COIN),
                transparentPayments,
                chainActive,
                TransactionStrategy(PrivacyPolicy::AllowRevealedRecipients),
                MINIMUM_FEE,
                1);
        ASSERT_TRUE(transparentEffects.has_value());
        EXPECT_EQ(transparentEffects->GetSpendable().GetSaplingTotal(), 2 * COIN);
        EXPECT_EQ(transparentEffects->GetSpendable().GetOrchardTotal(), 0);
        EXPECT_TRUE(transparentEffects->GetPayments().HasSaplingRecipient());
        EXPECT_FALSE(transparentEffects->GetPayments().HasOrchardRecipient());

        auto orchardOnlyInputs =
            MixedSaplingAndOrchardInputs(sourceSaplingAddress, 2 * COIN);
        orchardOnlyInputs.saplingNoteEntries.clear();
        auto orchardOnlyResult = builder.PrepareTransaction(
                *pwalletMain,
                selector,
                orchardOnlyInputs,
                transparentPayments,
                chainActive,
                TransactionStrategy(PrivacyPolicy::AllowRevealedRecipients),
                MINIMUM_FEE,
                1);
        ASSERT_FALSE(orchardOnlyResult.has_value());
        EXPECT_TRUE(std::holds_alternative<IronwoodUnsupportedError>(
                orchardOnlyResult.error()));

        auto genuinelyInsufficientResult = builder.PrepareTransaction(
                *pwalletMain,
                selector,
                MixedSaplingAndOrchardInputs(sourceSaplingAddress, COIN / 10),
                transparentPayments,
                chainActive,
                TransactionStrategy(PrivacyPolicy::AllowRevealedRecipients),
                MINIMUM_FEE,
                1);
        ASSERT_FALSE(genuinelyInsufficientResult.has_value());
        EXPECT_TRUE(std::holds_alternative<InvalidFundsError>(
                genuinelyInsufficientResult.error()));

        auto destinationSaplingKey = pwalletMain->GenerateNewLegacySaplingZKey();
        auto orchardSeed = MnemonicSeed::Random(0);
        auto destinationOrchardKey = OrchardSpendingKey::ForAccount(orchardSeed, 0, 0);
        auto destinationOrchardAddress = destinationOrchardKey.ToFullViewingKey()
            .ToIncomingViewingKey()
            .Address(diversifier_index_t{0});
        UnifiedAddress unifiedRecipient;
        ASSERT_TRUE(unifiedRecipient.AddReceiver(destinationOrchardAddress));
        ASSERT_TRUE(unifiedRecipient.AddReceiver(destinationSaplingKey));

        std::vector<Payment> unifiedPayments{
            Payment(unifiedRecipient, COIN, std::nullopt)};
        auto unifiedEffects = builder.PrepareTransaction(
                *pwalletMain,
                selector,
                MixedSaplingAndOrchardInputs(sourceSaplingAddress, 2 * COIN),
                unifiedPayments,
                chainActive,
                TransactionStrategy(PrivacyPolicy::FullPrivacy),
                MINIMUM_FEE,
                1);
        ASSERT_TRUE(unifiedEffects.has_value());
        EXPECT_EQ(unifiedEffects->GetSpendable().GetOrchardTotal(), 0);
        EXPECT_TRUE(unifiedEffects->GetPayments().HasSaplingRecipient());
        EXPECT_FALSE(unifiedEffects->GetPayments().HasOrchardRecipient());
    }
}

// TODO: test private methods
TEST(WalletRPCTests, RPCZMergeToAddressInternals)
{
    LoadProofParameters();

    SelectParams(CBaseChainParams::TESTNET);
    LoadGlobalWallet();

    const Consensus::Params& consensusParams = Params().GetConsensus();
    KeyIO keyIO(Params());
    {
    LOCK2(cs_main, pwalletMain->cs_wallet);

    EXPECT_EQ(-1, chainActive.Height());
    CBlock block;
    block.hashMerkleRoot = BlockMerkleRoot(block);
    auto blockHash = block.GetHash();
    CBlockIndex fakeIndex {block};
    mapBlockIndex.insert(std::make_pair(blockHash, &fakeIndex));
    chainActive.SetTip(&fakeIndex);
    EXPECT_TRUE(chainActive.Contains(&fakeIndex));
    EXPECT_EQ(0, chainActive.Height());

    // Mutable tx containing contextual information we need to build tx
    // We removed the ability to create pre-Sapling Sprout proofs, so we can
    // only create Sapling-onwards transactions.
    int nHeight = consensusParams.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight;
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(consensusParams, nHeight + 1, false);

    // Add keys manually
    auto taddr = pwalletMain->GenerateNewKey(true).GetID();
    std::string taddr_string = keyIO.EncodeDestination(taddr);

    NetAmountRecipient taddr1(keyIO.DecodePaymentAddress(taddr_string).value(), std::nullopt);
    auto sproutKey = pwalletMain->GenerateNewSproutZKey();
    NetAmountRecipient zaddr1(sproutKey, std::nullopt);

    auto saplingKey = pwalletMain->GenerateNewLegacySaplingZKey();
    NetAmountRecipient zaddr2(saplingKey, std::nullopt);

    WalletTxBuilder builder(Params(), minRelayTxFee);
    auto selector = CWallet::LegacyTransparentZTXOSelector(
            true,
            TransparentCoinbasePolicy::Disallow);
    TransactionStrategy strategy(PrivacyPolicy::AllowRevealedSenders);

    SpendableInputs inputs;
    auto wtx = FakeWalletTx();
    inputs.utxos.emplace_back(&wtx, 0, std::nullopt, 100, true);

    // Can’t send to Sprout
    (void)builder.PrepareTransaction(
            *pwalletMain,
            selector,
            inputs,
            zaddr1,
            chainActive,
            strategy,
            0,
            1)
        .map_error([](const auto& err) {
            EXPECT_TRUE(examine(err, match {
                [](const AddressResolutionError& are) {
                    return are == AddressResolutionError::SproutRecipientsNotSupported;
                },
                [](const auto&) { return false; },
            }));
        })
        .map([](const auto&) { EXPECT_TRUE(false); });

    // Insufficient funds
    (void)builder.PrepareTransaction(
            *pwalletMain,
            selector,
            inputs,
            zaddr2,
            chainActive,
            strategy,
            std::nullopt,
            1)
        .map_error([](const auto& err) {
            EXPECT_TRUE(examine(err, match {
                [](const InvalidFundsError& ife) {
                    return std::holds_alternative<InsufficientFundsError>(ife.reason);
                },
                [](const auto&) { return false; },
            }));
        })
        .map([](const auto&) { EXPECT_TRUE(false); });

    // Tear down
    chainActive.SetTip(NULL);
    mapBlockIndex.erase(blockHash);

    }
    UnloadGlobalWallet();
}

TEST(WalletRPCTests, RPCZsendmanyTaddrToSapling)
{
    LoadProofParameters();
    SelectParams(CBaseChainParams::TESTNET);

    LoadGlobalWallet();

    RegtestActivateSapling();
    {
    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (!pwalletMain->HaveMnemonicSeed()) {
        pwalletMain->GenerateNewSeed();
    }

    KeyIO keyIO(Params());
    // add keys manually
    auto taddr = pwalletMain->GenerateNewKey(true).GetID();
    auto pa = pwalletMain->GenerateNewLegacySaplingZKey();

    const Consensus::Params& consensusParams = Params().GetConsensus();
    auto rustNetwork = Params().RustNetwork();

    int nextBlockHeight = chainActive.Height() + 1;

    // Add a fake transaction to the wallet
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(consensusParams, nextBlockHeight, false);
    CScript scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(taddr) << OP_EQUALVERIFY << OP_CHECKSIG;
    mtx.vout.push_back(CTxOut(5 * COIN, scriptPubKey));
    CWalletTx wtx(pwalletMain, mtx);
    pwalletMain->LoadWalletTx(wtx);

    // Fake-mine the transaction
    EXPECT_EQ(-1, chainActive.Height());
    CBlock block;
    block.vtx.push_back(wtx);
    block.hashMerkleRoot = BlockMerkleRoot(block);
    auto blockHash = block.GetHash();
    CBlockIndex fakeIndex {block};
    mapBlockIndex.insert(std::make_pair(blockHash, &fakeIndex));
    chainActive.SetTip(&fakeIndex);
    EXPECT_TRUE(chainActive.Contains(&fakeIndex));
    EXPECT_EQ(0, chainActive.Height());
    wtx.SetMerkleBranch(block);
    pwalletMain->LoadWalletTx(wtx);

    // Context that z_sendmany requires
    auto builder = WalletTxBuilder(Params(), minRelayTxFee);
    mtx = CreateNewContextualCMutableTransaction(consensusParams, nextBlockHeight, false);

    // we need AllowFullyTransparent because the transaction will result
    // in transparent change as a consequence of sending from a legacy taddr
    TransactionStrategy strategy(PrivacyPolicy::AllowFullyTransparent);
    auto selector = pwalletMain->ZTXOSelectorForAddress(
            taddr,
            true,
            TransparentCoinbasePolicy::Disallow,
            strategy.PermittedAccountSpendingPolicy()).value();
    std::vector<Payment> recipients = { Payment(pa, 1*COIN, Memo::FromBytes({0xAB, 0xCD})) };
    std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_sendmany(std::move(builder), selector, recipients, 0, 0, strategy, std::nullopt));
    std::shared_ptr<AsyncRPCOperation_sendmany> ptr = std::dynamic_pointer_cast<AsyncRPCOperation_sendmany> (operation);

    // Enable test mode so tx is not sent
    static_cast<AsyncRPCOperation_sendmany *>(operation.get())->testmode = true;

    // Generate the Sapling shielding transaction
    operation->main();
    if (!operation->isSuccess()) {
        FAIL() << operation->getErrorMessage();
    }

    // Get the transaction
    auto result = operation->getResult();
    ASSERT_TRUE(result.isObject());
    auto hexTx = result["hex"].getValStr();
    CDataStream ss(ParseHex(hexTx), SER_NETWORK, PROTOCOL_VERSION);
    CTransaction tx;
    ss >> tx;
    ASSERT_NE(tx.GetSaplingOutputsCount(), 0);

    auto accountKey = pwalletMain->GetLegacyAccountKey().ToAccountPubKey();
    auto ovks = accountKey.GetOVKsForShielding();

    auto extDecryptSucceeded = 0;
    auto extDecryptFailed = 0;
    for (auto& output: tx.GetSaplingOutputs()) {
        auto enc_ciphertext = output.enc_ciphertext();
        auto out_ciphertext = output.out_ciphertext();
        auto cv = output.cv();
        auto cmu = output.cmu();
        auto ephemeral_key = output.ephemeral_key();

        // We shouldn't be able to decrypt with the empty ovk
        EXPECT_THROW(wallet::try_sapling_output_recovery(
            *rustNetwork,
            nextBlockHeight,
            uint256().GetRawBytes(),
            {
                cv,
                cmu,
                ephemeral_key,
                enc_ciphertext,
                out_ciphertext,
            }), rust::Error);

        // We shouldn't be able to decrypt with a random ovk
        EXPECT_THROW(wallet::try_sapling_output_recovery(
            *rustNetwork,
            nextBlockHeight,
            random_uint256().GetRawBytes(),
            {
                cv,
                cmu,
                ephemeral_key,
                enc_ciphertext,
                out_ciphertext,
            }), rust::Error);

        // We should not be able to decrypt with the internal change OVK for shielding
        EXPECT_THROW(wallet::try_sapling_output_recovery(
            *rustNetwork,
            nextBlockHeight,
            ovks.first.GetRawBytes(),
            {
                cv,
                cmu,
                ephemeral_key,
                enc_ciphertext,
                out_ciphertext,
            }), rust::Error);

        // We should be able to decrypt one of the outputs with the external OVK for shielding.
        try {
            wallet::try_sapling_output_recovery(
                *rustNetwork,
                nextBlockHeight,
                ovks.second.GetRawBytes(),
                {
                    cv,
                    cmu,
                    ephemeral_key,
                    enc_ciphertext,
                    out_ciphertext,
                });
            extDecryptSucceeded += 1;
        } catch (...) {
            extDecryptFailed += 1;
        }
    }
    EXPECT_EQ(extDecryptSucceeded, 1);
    EXPECT_EQ(extDecryptFailed, 1);

    // Tear down
    chainActive.SetTip(NULL);
    mapBlockIndex.erase(blockHash);

    }
    // Revert to default
    RegtestDeactivateSapling();
    UnloadGlobalWallet();
}

TEST(WalletRPCTests, ZIP317Fee)
{
    LoadProofParameters();
    SelectParams(CBaseChainParams::TESTNET);

    LoadGlobalWallet();

    RegtestActivateSapling();
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        if (!pwalletMain->HaveMnemonicSeed()) {
            pwalletMain->GenerateNewSeed();
        }

        KeyIO keyIO(Params());
        // add keys manually
        auto taddr = pwalletMain->GenerateNewKey(true).GetID();
        auto pa = pwalletMain->GenerateNewLegacySaplingZKey();

        const Consensus::Params& consensusParams = Params().GetConsensus();

        int nextBlockHeight = chainActive.Height() + 1;

        // Add a fake transaction to the wallet
        CMutableTransaction mtx = CreateNewContextualCMutableTransaction(consensusParams, nextBlockHeight, false);
        CScript scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(taddr) << OP_EQUALVERIFY << OP_CHECKSIG;
        size_t utxoCount = 100;
        for (size_t i = 0; i < utxoCount; i++) {
            mtx.vout.push_back(CTxOut(5 * COIN, scriptPubKey));
        }
        CWalletTx wtx(pwalletMain, mtx);
        pwalletMain->LoadWalletTx(wtx);

        // Fake-mine the transaction
        EXPECT_EQ(-1, chainActive.Height());
        CBlock block;
        block.vtx.push_back(wtx);
        block.hashMerkleRoot = BlockMerkleRoot(block);
        auto blockHash = block.GetHash();
        CBlockIndex fakeIndex {block};
        mapBlockIndex.insert(std::make_pair(blockHash, &fakeIndex));
        chainActive.SetTip(&fakeIndex);
        EXPECT_TRUE(chainActive.Contains(&fakeIndex));
        EXPECT_EQ(0, chainActive.Height());
        wtx.SetMerkleBranch(block);
        pwalletMain->LoadWalletTx(wtx);

        // Add keys manually
        std::string taddr_string = keyIO.EncodeDestination(taddr);

        WalletTxBuilder builder(Params(), minRelayTxFee);

        auto selector = CWallet::LegacyTransparentZTXOSelector(
                true,
                TransparentCoinbasePolicy::Disallow);

        { // test transparent inputs to NetAmountRecipient
            auto saplingKey = pwalletMain->GenerateNewLegacySaplingZKey();
            NetAmountRecipient zaddr(saplingKey, std::nullopt);

            TransactionStrategy strategy(PrivacyPolicy::AllowRevealedSenders);

            SpendableInputs inputs;
            for (size_t i = 0; i < utxoCount; i++) {
                CTxDestination address;
                ExtractDestination(scriptPubKey, address);
                inputs.utxos.emplace_back(&wtx, i, address, 100, true);
            }

            auto effects = builder.PrepareTransaction(
                    *pwalletMain,
                    selector,
                    inputs,
                    zaddr,
                    chainActive,
                    strategy,
                    std::nullopt,
                    1)
            .map_error([&](const auto& err) {
                try {
                    ThrowInputSelectionError(err, selector, strategy);
                } catch (const UniValue& value) {
                    FAIL() << value.write();
                }
            })
            .value();

            ExpectConsistentFee(strategy, effects);
        }

        { // test transparent inputs to Payment vector
            auto saplingKey = pwalletMain->GenerateNewLegacySaplingZKey();
            Payment saplingPayment(saplingKey, 200 * COIN, std::nullopt);
            auto saplingKey2 = pwalletMain->GenerateNewLegacySaplingZKey();
            Payment saplingPayment2(saplingKey2, 200 * COIN, std::nullopt);
            std::vector<Payment> payments {saplingPayment, saplingPayment2};

            TransactionStrategy strategy(PrivacyPolicy::AllowFullyTransparent);

            SpendableInputs inputs;
            for (size_t i = 0; i < utxoCount; i++) {
                CTxDestination address;
                ExtractDestination(scriptPubKey, address);
                inputs.utxos.emplace_back(&wtx, i, address, 100, true);
            }

            auto effects = builder.PrepareTransaction(
                    *pwalletMain,
                    selector,
                    inputs,
                    payments,
                    chainActive,
                    strategy,
                    std::nullopt,
                    1)
                .map_error([&](const auto& err) {
                    try {
                        ThrowInputSelectionError(err, selector, strategy);
                    } catch (const UniValue& value) {
                        FAIL() << value.write();
                    }
                })
                .value();

            ExpectConsistentFee(strategy, effects);
        }

        // Tear down
        chainActive.SetTip(NULL);
        mapBlockIndex.erase(blockHash);

    }
    // Revert to default
    RegtestDeactivateSapling();
    UnloadGlobalWallet();
}
