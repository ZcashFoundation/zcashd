// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2016-2023 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include <script/script.h>
#include <amount.h>
#include <consensus/funding.h>
#include "uint256.h"
#include "key_constants.h"
#include <zcash/address/sapling.hpp>

#include <optional>
#include <variant>

namespace Consensus {

// Early declaration to ensure it is accessible.
struct Params;

/**
 * Index into Params.vUpgrades and NetworkUpgradeInfo
 *
 * Being array indices, these MUST be numbered consecutively.
 *
 * The order of these indices MUST match the order of the upgrades on-chain, as
 * several functions depend on the enum being sorted.
 */
enum UpgradeIndex : uint32_t {
    // Sprout must be first
    BASE_SPROUT,
    UPGRADE_TESTDUMMY,
    UPGRADE_OVERWINTER,
    UPGRADE_SAPLING,
    UPGRADE_BLOSSOM,
    UPGRADE_HEARTWOOD,
    UPGRADE_CANOPY,
    UPGRADE_NU5,
    UPGRADE_NU6,
    UPGRADE_NU6_1,
    UPGRADE_NU6_2,
    UPGRADE_NU6_3,
    UPGRADE_NU7,
    // Add new network upgrades before this line.
    // NOTE: Also add new upgrades to NetworkUpgradeInfo in upgrades.cpp
    UPGRADE_ZFUTURE,
    MAX_NETWORK_UPGRADES
};

struct NetworkUpgrade {
    /**
     * The first protocol version which will understand the new consensus rules
     */
    int nProtocolVersion = 0;

    /**
     * Height of the first block for which the new consensus rules will be active.
     * Defaults to NO_ACTIVATION_HEIGHT so that an entry a network forgets to set
     * never activates (zero would mean ALWAYS_ACTIVE).
     */
    int nActivationHeight = NO_ACTIVATION_HEIGHT;

    /**
     * Special value for nActivationHeight indicating that the upgrade is always active.
     * This is useful for testing, as it means tests don't need to deal with the activation
     * process (namely, faking a chain of somewhat-arbitrary length).
     *
     * New blockchains that want to enable upgrade rules from the beginning can also use
     * this value. However, additional care must be taken to ensure the genesis block
     * satisfies the enabled rules.
     */
    static constexpr int ALWAYS_ACTIVE = 0;

    /**
     * Special value for nActivationHeight indicating that the upgrade will never activate.
     * This is useful when adding upgrade code that has a testnet activation height, but
     * should remain disabled on mainnet.
     */
    static constexpr int NO_ACTIVATION_HEIGHT = -1;

    /**
     * The hash of the block at height nActivationHeight, if known. This is set manually
     * after a network upgrade activates.
     *
     * We use this in IsInitialBlockDownload to detect whether we are potentially being
     * fed a fake alternate chain. We use NU activation blocks for this purpose instead of
     * the checkpoint blocks, because network upgrades (should) have significantly more
     * scrutiny than regular releases. nMinimumChainWork MUST be set to at least the chain
     * work of this block, otherwise this detection will have false positives.
     */
    std::optional<uint256> hashActivationBlock;
};

/**
 * Type for the development funding lockbox(es). At present there is only one; this is
 * not implemented as a singleton because it needs to be copyable so long as the
 * `FundingStream` type is so.
 */
class Lockbox
{
public:
    Lockbox() {}

    // At present there is only a single lockbox.
    friend bool operator==(const Lockbox& lhs, const Lockbox& rhs)
    {
        return true;
    }

    friend bool operator<(const Lockbox& lhs, const Lockbox& rhs)
    {
        return false;
    }
};

typedef std::variant<libzcash::SaplingPaymentAddress, CScript, Lockbox> FundingStreamRecipient;
typedef std::pair<FundingStreamRecipient, CAmount> FundingStreamElement;

/**
 * Index into Params.vFundingStreams.
 *
 * Being array indices, these MUST be numbered consecutively.
 */
enum FundingStreamIndex : uint32_t {
    FS_ZIP214_BP,
    FS_ZIP214_ZF,
    FS_ZIP214_MG,
    FS_FPF_ZCG,
    FS_DEFERRED,
    FS_FPF_ZCG_H3,
    FS_CCF_H3,
    MAX_FUNDING_STREAMS,
};
const auto FIRST_FUNDING_STREAM = FS_ZIP214_BP;

extern const struct FSInfo FundingStreamInfo[];

enum FundingStreamError {
    CANOPY_NOT_ACTIVE,
    ILLEGAL_RANGE,
    INSUFFICIENT_RECIPIENTS,
    NU6_NOT_ACTIVE,
};

class FundingStream
{
private:
    int startHeight;
    int endHeight;
    std::vector<FundingStreamRecipient> recipients;

    FundingStream(int startHeight, int endHeight, const std::vector<FundingStreamRecipient>& recipients):
        startHeight(startHeight), endHeight(endHeight), recipients(recipients) { }
public:
    FundingStream(const FundingStream& fs):
        startHeight(fs.startHeight), endHeight(fs.endHeight), recipients(fs.recipients) { }

    static std::variant<FundingStream, FundingStreamError> ValidateFundingStream(
        const Consensus::Params& params,
        const int startHeight,
        const int endHeight,
        const std::vector<FundingStreamRecipient>& recipients
    );

    static FundingStream ParseFundingStream(
        const Consensus::Params& params,
        const KeyConstants& keyConstants,
        const int startHeight,
        const int endHeight,
        const std::vector<std::string>& strAddresses,
        const bool allowDeferredPool);

    int GetStartHeight() const { return startHeight; };
    int GetEndHeight() const { return endHeight; };
    const std::vector<FundingStreamRecipient>& GetRecipients() const {
        return recipients;
    };

    FundingStreamRecipient Recipient(const Params& params, int nHeight) const;
};

/**
 * Index into Params.vOnetimeLockboxDisbursements.
 *
 * Being array indices, these MUST be numbered consecutively.
 */
enum OnetimeLockboxDisbursementIndex : uint32_t {
    LD_ZIP271_NU6_1_CHUNK_1,
    LD_ZIP271_NU6_1_CHUNK_2,
    LD_ZIP271_NU6_1_CHUNK_3,
    LD_ZIP271_NU6_1_CHUNK_4,
    LD_ZIP271_NU6_1_CHUNK_5,
    LD_ZIP271_NU6_1_CHUNK_6,
    LD_ZIP271_NU6_1_CHUNK_7,
    LD_ZIP271_NU6_1_CHUNK_8,
    LD_ZIP271_NU6_1_CHUNK_9,
    LD_ZIP271_NU6_1_CHUNK_10,
    MAX_ONETIME_LOCKBOX_DISBURSEMENTS
};
const auto FIRST_ONETIME_LOCKBOX_DISBURSEMENT = LD_ZIP271_NU6_1_CHUNK_1;

/**
 * An amount of funds that the activation block for the given upgrade must
 * disburse from the lockbox to the given recipient.
 */
class OnetimeLockboxDisbursement
{
private:
    UpgradeIndex upgrade;
    CAmount zatoshis;
    CScript recipient;

    OnetimeLockboxDisbursement(UpgradeIndex upgrade, CAmount zatoshis, CScript& recipient):
        upgrade(upgrade), zatoshis(zatoshis), recipient(recipient) { }
public:
    OnetimeLockboxDisbursement(const OnetimeLockboxDisbursement& fs):
        upgrade(fs.upgrade), zatoshis(fs.zatoshis), recipient(fs.recipient) { }

    static OnetimeLockboxDisbursement Parse(
        const Consensus::Params& params,
        const KeyConstants& keyConstants,
        const UpgradeIndex upgrade,
        const CAmount zatoshis,
        const std::string& strAddress);

    UpgradeIndex GetUpgrade() const { return upgrade; };
    CAmount GetAmount() const { return zatoshis; };
    CScript GetRecipient() const { return recipient; };
};

enum ConsensusFeature : uint32_t {
    // Index value for the maximum consensus feature ID.
    MAX_FEATURES
};
const auto FIRST_CONSENSUS_FEATURE = MAX_FEATURES;

template <class Feature>
struct FeatureInfo {
    std::vector<Feature> dependsOn;
    UpgradeIndex activation;
};

/**
 * A FeatureSet encodes a directed acyclic graph of feature dependencies
 * as an array indexed by feature ID. Values are FeatureInfo objects
 * containing the list of feature IDs upon which the index's feature ID
 * depends.
 *
 * The `Feature` and `Params` template parameters permit for the
 * logic of `FeatureActive` to be tested against a mock set of
 * features and activation heights.
 */
template <class Feature, class Params>
class FeatureSet {
private:
    std::vector<FeatureInfo<Feature>> features;
public:
    FeatureSet(std::vector<FeatureInfo<Feature>> features): features(features) {
    }

    bool FeatureActive(
            const Params& params,
            const int nHeight,
            const Feature feature) const {
        assert(feature < features.size());

        // The feature must be explicitly required by a CLI argument or by
        // the feature being universally available above a network upgrade
        // activation height.
        if (params.NetworkUpgradeActive(nHeight, features[feature].activation) ||
                params.FeatureRequired(feature)) {
            // Transitively check that if a feature is active, all of the other features
            // that it depends on are also active.
            auto requires = features[feature].dependsOn;
            assert(std::all_of(
                requires.begin(),
                requires.end(),
                [&](Feature feat) {
                    return FeatureActive(params, nHeight, feat);
                }
            ));

            return true;
        } else {
            return false;
        }
    }
};

/**
 * The set of features supported by Zcashd.
 */
const FeatureSet<ConsensusFeature, Params> Features({});

/** ZIP208 block target interval in seconds. */
static const unsigned int PRE_BLOSSOM_POW_TARGET_SPACING = 150;
static const unsigned int POST_BLOSSOM_POW_TARGET_SPACING = 75;
static_assert(PRE_BLOSSOM_POW_TARGET_SPACING > POST_BLOSSOM_POW_TARGET_SPACING, "Blossom target spacing must be less than pre-Blossom target spacing.");
static_assert(PRE_BLOSSOM_POW_TARGET_SPACING % POST_BLOSSOM_POW_TARGET_SPACING == 0, "Blossom target spacing must exactly divide pre-Blossom target spacing.");

static const int BLOSSOM_POW_TARGET_SPACING_RATIO = PRE_BLOSSOM_POW_TARGET_SPACING / POST_BLOSSOM_POW_TARGET_SPACING;
static_assert(BLOSSOM_POW_TARGET_SPACING_RATIO * POST_BLOSSOM_POW_TARGET_SPACING == PRE_BLOSSOM_POW_TARGET_SPACING, "Invalid BLOSSOM_POW_TARGET_SPACING_RATIO");

/** ZIP 218 block target interval in seconds from NU7. */
static const unsigned int POST_NU7_POW_TARGET_SPACING = 25;
static_assert(POST_BLOSSOM_POW_TARGET_SPACING % POST_NU7_POW_TARGET_SPACING == 0, "NU7 target spacing must exactly divide post-Blossom target spacing.");

static const int NU7_POW_TARGET_SPACING_RATIO = POST_BLOSSOM_POW_TARGET_SPACING / POST_NU7_POW_TARGET_SPACING;
static_assert(NU7_POW_TARGET_SPACING_RATIO == 3, "ZIP 218 defines NU7PoWTargetSpacingRatio as 3");

/** ZIP 218 PoW averaging window, in blocks, from NU7 (PostNU7PoWAveragingWindow). */
static const int64_t POST_NU7_POW_AVERAGING_WINDOW = 102;

/**
 * Testnet minimum-difficulty gap, in target spacings: 6 before NU7 (ZIP 208) and 18 from
 * NU7 (ZIP 218 and ZIP 259), so the gap stays 450 seconds across the spacing change.
 */
static const int PRE_NU7_MIN_DIFFICULTY_GAP_SPACINGS = 6;
static const int POST_NU7_MIN_DIFFICULTY_GAP_SPACINGS = 18;

static const unsigned int PRE_BLOSSOM_HALVING_INTERVAL = 840000;
static const unsigned int PRE_BLOSSOM_REGTEST_HALVING_INTERVAL = 144;

#define POST_BLOSSOM_HALVING_INTERVAL(preBlossomInterval) \
    (preBlossomInterval * Consensus::BLOSSOM_POW_TARGET_SPACING_RATIO)

/** ZIP 218 PostNU7HalvingInterval, in blocks. */
#define POST_NU7_HALVING_INTERVAL(preBlossomInterval) \
    (POST_BLOSSOM_HALVING_INTERVAL(preBlossomInterval) * Consensus::NU7_POW_TARGET_SPACING_RATIO)

/**
 * ZIP 214 Revision 3: a funding stream end height after NU7 activation A moves to
 * A + NU7PoWTargetSpacingRatio * (height - A), so it keeps its date; for the Revision 2
 * streams this is HeightForHalving(3). Heights at or before A, and all heights on a
 * network without NU7, are unchanged.
 */
int NU7AdjustedFundingStreamHeight(int height, std::optional<int> nu7Activation);

/**
 * ZIP 237 BLOCK_SUBSIDY_FRACTION from NU7: floor(LN2_SCALED / PostNU7HalvingInterval) / 10^10.
 * As in Zakura, the fraction is the same on every network.
 */
static const int64_t NSM_LN2_SCALED = 6931680000;
static const int64_t NSM_BLOCK_SUBSIDY_FRACTION_NUMERATOR = 1375;
static const int64_t NSM_BLOCK_SUBSIDY_FRACTION_DENOMINATOR = 10000000000;
static_assert(
    NSM_LN2_SCALED / POST_NU7_HALVING_INTERVAL(PRE_BLOSSOM_HALVING_INTERVAL) == NSM_BLOCK_SUBSIDY_FRACTION_NUMERATOR,
    "ZIP 237 BLOCK_SUBSIDY_FRACTION numerator is floor(LN2_SCALED / PostNU7HalvingInterval)");

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    /**
     * Returns the activation height for the specified network upgrade, if any.
     */
    std::optional<int> GetActivationHeight(Consensus::UpgradeIndex idx) const;

    /**
     * Returns true if the given network upgrade is active as of the given block
     * height. Caller must check that the height is >= 0 (and handle unknown
     * heights).
     */
    bool NetworkUpgradeActive(int nHeight, Consensus::UpgradeIndex idx) const;

    /**
     * Returns the activation height of the latest settled upgrade, as defined
     * in <https://zips.z.cash/protocol/protocol.pdf#blockchain>.
     */
    int HeightOfLatestSettledUpgrade() const;

    bool FutureTimestampSoftForkActive(int nHeight) const;

    bool TemporaryOrchardDisablingSoftForkActive(int nHeight) const;

    bool FeatureActive(int nHeight, Consensus::ConsensusFeature feature) const;

    bool FeatureRequired(Consensus::ConsensusFeature feature) const;

    uint256 hashGenesisBlock;

    bool fCoinbaseMustBeShielded = false;

    /** Needs to evenly divide MAX_SUBSIDY to avoid rounding errors. */
    int nSubsidySlowStartInterval;
    /**
     * Shift based on a linear ramp for slow start:
     *
     * MAX_SUBSIDY*(t_s/2 + t_r) = MAX_SUBSIDY*t_h  Coin balance
     *              t_s   + t_r  = t_h + t_c        Block balance
     *
     * t_s = nSubsidySlowStartInterval
     * t_r = number of blocks between end of slow start and first halving
     * t_h = nPreBlossomSubsidyHalvingInterval
     * t_c = SubsidySlowStartShift()
     */
    int SubsidySlowStartShift() const { return nSubsidySlowStartInterval / 2; }
    int nPreBlossomSubsidyHalvingInterval;
    int nPostBlossomSubsidyHalvingInterval;
    int nPostNU7SubsidyHalvingInterval;

    /**
     * Identify the halving index at the specified height. The result will be
     * negative during the slow-start period.
     */
    int Halving(int nHeight) const;

    /**
     * Get the block height of the specified halving.
     */
    int HalvingHeight(int nHeight, int halvingIndex) const;

    /**
     * The first height at which Halving() reaches halvingIndex, taking every target
     * spacing era (including NU7) into account, or nullopt if no height up to INT_MAX
     * reaches it.
     */
    std::optional<int> HeightForHalving(int halvingIndex) const;

    int GetLastFoundersRewardBlockHeight(int nHeight) const;

    /**
     * ZIP 207 AddressPeriod(nHeight), revised by ZIP 207 Revision 2 so that each
     * period keeps its duration after NU7 shortens the target spacing.
     */
    int64_t FundingStreamAddressPeriod(int nHeight) const;

    /**
     * The index of the funding stream recipient address for nHeight, for a stream
     * starting at fundingStreamStartHeight.
     */
    int FundingPeriodIndex(int fundingStreamStartHeight, int nHeight) const;

    /**
     * ZIP 237 INITIAL_NSM_VALUE_BALANCE, the NSM value balance after block
     * NU7ActivationHeight - 1, or nullopt to derive it there from the chain supply
     * (ScheduledIssuance(height) - chain total supply), which regtest does by default.
     */
    std::optional<CAmount> nInitialNSMValueBalance;
    /**
     * Whether the NSM value balance derived at NU7ActivationHeight - 1 must also equal
     * nInitialNSMValueBalance (Mainnet and Testnet, whose constants were measured).
     */
    bool fCheckInitialNSMValueBalance = false;
    /** For tests only: NSM reissuance starts here (not before NU7) instead of the derived height. */
    std::optional<int> nTestNSMReissuanceHeight;

    /**
     * ZIP 235: the part of a block's total non-coinbase fees nFees that its coinbase may
     * claim. From NU7, floor(6 * nFees / 10) of the aggregate is removed from circulation.
     */
    CAmount MinerFeeShare(int nHeight, CAmount nFees) const;

    /**
     * The scheduled block subsidy summed over heights 1 to nHeight. As in Zakura, the
     * genesis subsidy (zero on Mainnet and Testnet) is excluded.
     */
    CAmount ScheduledIssuance(int nHeight) const;

    /**
     * ZIP 237 DEPLOYMENT_BLOCK_HEIGHT: the first height in [max(A, H3 + 1), H4) at which
     * ceil(BLOCK_SUBSIDY_FRACTION * (MAX_MONEY - ScheduledIssuance(height - 1))) is below the
     * scheduled subsidy, where A is the NU7 activation height and H3 and H4 the third and
     * fourth halvings. nullopt if NU7 is not scheduled or there is no such height.
     */
    std::optional<int> NSMReissuanceHeight() const;

    /** Whether ZIP 237 NSM reissuance applies to the block at nHeight. */
    bool IsNSMReissuanceActive(int nHeight) const;

    /**
     * ZIP 237 AdditionalBlockSubsidy(nHeight) given NSMValueBalance(nHeight - 1):
     * ceil(BLOCK_SUBSIDY_FRACTION * balance) once reissuance is active, otherwise zero.
     */
    CAmount AdditionalBlockSubsidy(int nHeight, CAmount parentNSMValueBalance) const;

    /** Used to check majorities for block version upgrade */
    int nMajorityEnforceBlockUpgrade;
    int nMajorityRejectBlockOutdated;
    int nMajorityWindow;
    NetworkUpgrade vUpgrades[MAX_NETWORK_UPGRADES];

    int nFundingPeriodLength;
    std::optional<FundingStream> vFundingStreams[MAX_FUNDING_STREAMS];
    void AddZIP207FundingStream(
        const KeyConstants& keyConstants,
        FundingStreamIndex idx,
        int startHeight,
        int endHeight,
        const std::vector<std::string>& addresses);

    void AddZIP207LockboxStream(
        const KeyConstants& keyConstants,
        FundingStreamIndex idx,
        int startHeight,
        int endHeight);

    std::optional<OnetimeLockboxDisbursement> vOnetimeLockboxDisbursements[MAX_ONETIME_LOCKBOX_DISBURSEMENTS];

    /**
     * Defines a one-time lockbox disbursement for this network.
     *
     * The disbursement amounts are hard-coded, instead of being calculated as the amount
     * in the lockbox at the upgrade activation. `nChainLockboxValue` tracks the latter,
     * but we only know it once we've received all of a block's ancestors, which would be
     * too late in the consensus rules. We need to be able to calculate `lockboxValue` in
     * `SetChainPoolValues`, at which point we only know the block's height and contents.
     */
    void AddZIP271LockboxDisbursement(
        const KeyConstants& keyConstants,
        OnetimeLockboxDisbursementIndex idx,
        UpgradeIndex upgrade,
        CAmount zatoshis,
        const std::string& strAddress);

    /**
     * Returns the total block subsidy as of the given block height
     */
    CAmount GetBlockSubsidy(int nHeight) const;

    /**
     * Returns the vector of active funding streams as of the given height.
     */
    std::vector<std::pair<FSInfo, FundingStream>> GetActiveFundingStreams(int nHeight) const;

    /**
     * Returns the vector of active funding stream elements as of the given height.
     */
    std::set<FundingStreamElement> GetActiveFundingStreamElements(int nHeight) const;

    /**
     * Returns the active funding stream elements at the given height, with
     * values determined based upon the specified block subsidy amount. This
     * should always be set to the value returned by `GetBlockSubsidy` for the
     * given height; it is passed explicitly rather than derived internally
     * as many call sites will have already called `GetBlockSubsidy` directly
     * for other purposes.
     */
    std::set<FundingStreamElement> GetActiveFundingStreamElements(
        int nHeight,
        CAmount blockSubsidy) const;

    /**
     * Returns the vector of one-time lockbox disbursements occurring at the
     * given height.
     */
    std::vector<OnetimeLockboxDisbursement> GetLockboxDisbursementsForHeight(int nHeight) const;

    /**
     * A set of features that have been explicitly force-enabled
     * via the CLI, overriding block-height based decisions for
     * this feature.
     */
    std::set<ConsensusFeature> vRequiredFeatures;

    /**
     * Default block height at which the future timestamp soft fork rule activates.
     *
     * Genesis blocks are hard-coded into the binary for all networks
     * (mainnet, testnet, regtest), and have now-ancient timestamps. So we need to
     * handle the case where we might use the genesis block's timestamp as the
     * median-time-past.
     *
     * GetMedianTimePast() is implemented such that the chosen block is the
     * median of however many blocks we are able to select up to
     * nMedianTimeSpan = 11. For example, if nHeight == 6:
     *
     *    ,-<pmedian  ,-<pbegin            ,-<pend
     *   [-, -, -, -, 0, 1, 2, 3, 4, 5, 6] -
     *
     * and thus pbegin[(pend - pbegin)/2] will select block height 3, assuming
     * that the block timestamps are all greater than the genesis block's
     * timestamp. For regtest mode, this is a valid assumption; we generate blocks
     * deterministically and in-order. For mainnet it was true in practice, and
     * we aren't going to be starting a new chain linked directly from the mainnet
     * genesis block.
     *
     * Therefore, for regtest and mainnet we only risk using the regtest genesis
     * block's timestamp for nHeight < 2 (as GetMedianTimePast() uses floor division).
     *
     * Separately, for mainnet this is also necessary because there was a long time
     * between starting to find the mainnet genesis block (which was mined with a
     * single laptop) and mining the block at height 1. For any new mainnet chain
     * using Zcash code, the soft fork rule would be enabled from the start so that
     * miners would limit their timestamps accordingly.
     *
     * For testnet, the future timestamp soft fork rule was violated for many
     * blocks prior to Blossom activation. At Blossom, the time threshold for the
     * (testnet-specific) minimum difficulty rule was changed in such a way that
     * starting from shortly after the Blossom activation, no further blocks
     * violate the soft fork rule. So for testnet we override the soft fork
     * activation height in chainparams.cpp.
     */
    int nFutureTimestampSoftForkHeight = 2;

    int nTemporaryOrchardDisablingSoftForkHeight = Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;

    /** Proof of work parameters */
    unsigned int nEquihashN = 0;
    unsigned int nEquihashK = 0;
    uint256 powLimit;
    std::optional<uint32_t> nPowAllowMinDifficultyBlocksAfterHeight;
    bool fPowNoRetargeting;
    /** PoW averaging window, in blocks, before NU7. */
    int64_t nPowAveragingWindow;
    /** PoW averaging window, in blocks, from NU7 (ZIP 218). */
    int64_t nPostNU7PowAveragingWindow = POST_NU7_POW_AVERAGING_WINDOW;
    int64_t nPowMaxAdjustDown;
    int64_t nPowMaxAdjustUp;
    int64_t nPreBlossomPowTargetSpacing;
    int64_t nPostBlossomPowTargetSpacing;
    int64_t nPostNU7PowTargetSpacing = POST_NU7_POW_TARGET_SPACING;

    /** Regtest-only (`-regtestacceptunvalidatedpow`): accept block headers
     *  without validating the Equihash solution or the proof-of-work hash
     *  target. Zebra skips proof-of-work on regtest, so blocks it mines carry
     *  null solutions that stock validation would reject. */
    bool fAcceptUnvalidatedPoW = false;

    int64_t PoWTargetSpacing(int nHeight) const;
    /** Number of blocks averaged to compute the target of the block at nHeight (ZIP 218). */
    int64_t PoWAveragingWindow(int nHeight) const;
    int64_t AveragingWindowTimespan(int nHeight) const;
    /**
     * On networks that allow minimum-difficulty blocks, the block at nHeight may use the
     * minimum difficulty only if its time is strictly more than this many seconds after its
     * parent's time.
     */
    int64_t MinDifficultyGap(int nHeight) const;
    int64_t MinActualTimespan(int nHeight) const;
    int64_t MaxActualTimespan(int nHeight) const;

    uint256 nMinimumChainWork;
};
} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PARAMS_H
