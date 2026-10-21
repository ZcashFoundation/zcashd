// Copyright (c) 2019-2023 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "params.h"

#include <amount.h>
#include <key_io.h>
#include <script/standard.h>
#include "upgrades.h"
#include "util/system.h"
#include "util/match.h"

#include <limits>

namespace Consensus {
    /**
     * General information about each funding stream.
     * Ordered by Consensus::FundingStreamIndex.
     */
    constexpr struct FSInfo FundingStreamInfo[Consensus::MAX_FUNDING_STREAMS] = {
        {
            .recipient = "Electric Coin Company",
            .specification = "https://zips.z.cash/zip-0214",
            .valueNumerator = 7,
            .valueDenominator = 100,
        },
        {
            .recipient = "Zcash Foundation",
            .specification = "https://zips.z.cash/zip-0214",
            .valueNumerator = 5,
            .valueDenominator = 100,
        },
        {
            .recipient = "Major Grants",
            .specification = "https://zips.z.cash/zip-0214",
            .valueNumerator = 8,
            .valueDenominator = 100,
        },
        {
            .recipient = "Zcash Community Grants NU6",
            .specification = "https://zips.z.cash/zip-0214",
            .valueNumerator = 8,
            .valueDenominator = 100,
        },
        {
            .recipient = "Lockbox NU6",
            .specification = "https://zips.z.cash/zip-0214",
            .valueNumerator = 12,
            .valueDenominator = 100,
        },
        {
            .recipient = "Zcash Community Grants to third halving",
            .specification = "https://zips.z.cash/zip-0214",
            .valueNumerator = 8,
            .valueDenominator = 100,
        },
        {
            .recipient = "Coinholder-Controlled Fund to third halving",
            .specification = "https://zips.z.cash/zip-0214",
            .valueNumerator = 12,
            .valueDenominator = 100,
        }
    };
    static constexpr bool validateFundingStreamInfo(uint32_t idx) {
        return (idx >= Consensus::MAX_FUNDING_STREAMS || (
            FundingStreamInfo[idx].valueNumerator < FundingStreamInfo[idx].valueDenominator &&
            FundingStreamInfo[idx].valueNumerator < (INT64_MAX / MAX_MONEY) &&
            validateFundingStreamInfo(idx + 1)));
    }
    static_assert(
        validateFundingStreamInfo(Consensus::FIRST_FUNDING_STREAM),
        "Invalid FundingStreamInfo");

    std::optional<int> Params::GetActivationHeight(Consensus::UpgradeIndex idx) const {
        auto nActivationHeight = vUpgrades[idx].nActivationHeight;
        if (nActivationHeight == Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT) {
            return std::nullopt;
        } else {
            return nActivationHeight;
        }
    }

    bool Params::NetworkUpgradeActive(int nHeight, Consensus::UpgradeIndex idx) const {
        return NetworkUpgradeState(nHeight, *this, idx) == UPGRADE_ACTIVE;
    }

    int Params::HeightOfLatestSettledUpgrade() const {
        for (auto idxInt = Consensus::MAX_NETWORK_UPGRADES - 1; idxInt > Consensus::BASE_SPROUT; idxInt--) {
            if (vUpgrades[idxInt].hashActivationBlock.has_value()) {
                return vUpgrades[idxInt].nActivationHeight;
            }
        }
        return 0;
    }

    bool Params::FeatureRequired(const Consensus::ConsensusFeature feature) const {
        return vRequiredFeatures.count(feature) > 0;
    }

    bool Params::FeatureActive(const int nHeight, const Consensus::ConsensusFeature feature) const {
        return Features.FeatureActive(*this, nHeight, feature);
    }

    bool Params::FutureTimestampSoftForkActive(int nHeight) const {
        return nHeight >= nFutureTimestampSoftForkHeight;
    }

    bool Params::TemporaryOrchardDisablingSoftForkActive(int nHeight) const {
        return
            nTemporaryOrchardDisablingSoftForkHeight != Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT &&
            nHeight >= nTemporaryOrchardDisablingSoftForkHeight;
    }

    int Params::Halving(int nHeight) const {
        // zip208, zip218
        // Halving(height) :=
        // floor((height - SlowStartShift) / PreBlossomHalvingInterval), if not IsBlossomActivated(height)
        // floor((BlossomActivationHeight - SlowStartShift) / PreBlossomHalvingInterval + (height - BlossomActivationHeight) / PostBlossomHalvingInterval), if IsBlossomActivated(height) and not IsNU7Activated(height)
        // floor((BlossomActivationHeight - SlowStartShift) / PreBlossomHalvingInterval + (NU7ActivationHeight - BlossomActivationHeight) / PostBlossomHalvingInterval
        //       + (height - NU7ActivationHeight) / PostNU7HalvingInterval), otherwise
        if (NetworkUpgradeActive(nHeight, Consensus::UPGRADE_NU7)) {
            assert(NetworkUpgradeActive(nHeight, Consensus::UPGRADE_BLOSSOM));
            int64_t blossomActivationHeight = vUpgrades[Consensus::UPGRADE_BLOSSOM].nActivationHeight;
            int64_t nu7ActivationHeight = vUpgrades[Consensus::UPGRADE_NU7].nActivationHeight;
            // As for Blossom, scale by the NU7 interval to keep the sum in integers:
            // PostNU7HalvingInterval = PreBlossomHalvingInterval * 2 * 3.
            int64_t scaledHalvings =
                ((blossomActivationHeight - SubsidySlowStartShift())
                    * Consensus::BLOSSOM_POW_TARGET_SPACING_RATIO * Consensus::NU7_POW_TARGET_SPACING_RATIO)
                + ((nu7ActivationHeight - blossomActivationHeight) * Consensus::NU7_POW_TARGET_SPACING_RATIO)
                + (nHeight - nu7ActivationHeight);
            return (int) (scaledHalvings / nPostNU7SubsidyHalvingInterval);
        } else if (NetworkUpgradeActive(nHeight, Consensus::UPGRADE_BLOSSOM)) {
            int64_t blossomActivationHeight = vUpgrades[Consensus::UPGRADE_BLOSSOM].nActivationHeight;
            // Ideally we would say:
            // halvings = (blossomActivationHeight - SubsidySlowStartShift()) / nPreBlossomSubsidyHalvingInterval
            //     + (nHeight - blossomActivationHeight) / nPostBlossomSubsidyHalvingInterval;
            // But, (blossomActivationHeight - SubsidySlowStartShift()) / nPreBlossomSubsidyHalvingInterval
            // would need to be treated as a rational number in order for this to work.
            // Define scaledHalvings := halvings * nPostBlossomSubsidyHalvingInterval;
            int64_t scaledHalvings = ((blossomActivationHeight - SubsidySlowStartShift()) * Consensus::BLOSSOM_POW_TARGET_SPACING_RATIO)
                + (nHeight - blossomActivationHeight);
            return (int) (scaledHalvings / nPostBlossomSubsidyHalvingInterval);
        } else {
            return (nHeight - SubsidySlowStartShift()) / nPreBlossomSubsidyHalvingInterval;
        }
    }

    /**
     * This method determines the block height of the `halvingIndex`th
     * halving, as known at the specified `nHeight` block height.
     *
     * Previous implementations of this logic were specialized to the
     * first halving.
     */
    int Params::HalvingHeight(int nHeight, int halvingIndex) const {
        assert(nHeight >= 0);
        assert(halvingIndex > 0);

        // zip208
        // HalvingHeight(i) := max({ height ⦂ N | Halving(height) < i }) + 1
        //
        // Halving(h) returns the halving index at the specified height.  It is
        // defined as floor(f(h)) where f is a strictly increasing rational
        // function, so it's sufficient to solve for f(height) = halvingIndex
        // in the rationals and then take ceiling(height).
        //
        // H := blossom activation height;
        // SS := SubsidySlowStartShift();
        // R := 1 / (postInterval / preInterval) = BLOSSOM_POW_TARGET_SPACING_RATIO
        // (The following calculation depends on BLOSSOM_POW_TARGET_SPACING_RATIO being an integer.)
        //
        // preBlossom:
        // i = (height - SS) / preInterval
        // height = (preInterval * i) + SS
        //
        // postBlossom:
        // i = (H - SS) / preInterval + (HalvingHeight(i) - H) / postInterval
        // preInterval = postInterval / R
        // i = (H - SS) / (postInterval / R) + (HalvingHeight(i) - H) / postInterval
        // i = (R * (H - SS) + HalvingHeight(i) - H) / postInterval
        // postInterval * i = R * (H - SS) + HalvingHeight(i) - H
        // HalvingHeight(i) = postInterval * i - R * (H - SS) + H
        if (NetworkUpgradeActive(nHeight, Consensus::UPGRADE_BLOSSOM)) {
            int blossomActivationHeight = vUpgrades[Consensus::UPGRADE_BLOSSOM].nActivationHeight;

            return
                (nPostBlossomSubsidyHalvingInterval * halvingIndex)
                - (BLOSSOM_POW_TARGET_SPACING_RATIO * (blossomActivationHeight - SubsidySlowStartShift()))
                + blossomActivationHeight;
        } else {
            return (nPreBlossomSubsidyHalvingInterval * halvingIndex) + SubsidySlowStartShift();
        }
    }

    int Params::GetLastFoundersRewardBlockHeight(int nHeight) const {
        return HalvingHeight(nHeight, 1) - 1;
    }

    std::optional<int> Params::HeightForHalving(int halvingIndex) const {
        if (halvingIndex <= 0) {
            return 0;
        }
        if (Halving(std::numeric_limits<int>::max()) < halvingIndex) {
            return std::nullopt;
        }
        // Halving() is non-decreasing in height, so binary search its whole domain.
        int low = 0;
        int high = std::numeric_limits<int>::max();
        while (low < high) {
            int middle = low + (high - low) / 2;
            if (Halving(middle) < halvingIndex) {
                low = middle + 1;
            } else {
                high = middle;
            }
        }
        return low;
    }

    int64_t Params::FundingStreamAddressPeriod(int nHeight) const {
        // zip207 Revision 2
        // AddressPeriod(height) :=
        // floor((height + PostBlossomHalvingInterval - HeightForHalving(1)) / AddressChangeInterval), if not IsNU7Activated(height)
        // floor((R * (A + PostBlossomHalvingInterval - HeightForHalving(1)) + height - A) / (R * AddressChangeInterval)), otherwise
        // where A = NU7ActivationHeight and R = NU7PoWTargetSpacingRatio.
        const int64_t firstHalvingHeight = HeightForHalving(1).value();
        auto periodOffset = [&](int64_t height) {
            return height - firstHalvingHeight + nPostBlossomSubsidyHalvingInterval;
        };
        // The offsets can be negative on regtest, so round towards negative infinity
        // as the specification's floor does.
        auto floorDiv = [](int64_t numerator, int64_t denominator) {
            int64_t quotient = numerator / denominator;
            return (numerator % denominator != 0 && numerator < 0) ? quotient - 1 : quotient;
        };
        if (NetworkUpgradeActive(nHeight, Consensus::UPGRADE_NU7)) {
            const int64_t nu7ActivationHeight = vUpgrades[Consensus::UPGRADE_NU7].nActivationHeight;
            return floorDiv(
                Consensus::NU7_POW_TARGET_SPACING_RATIO * periodOffset(nu7ActivationHeight)
                    + (nHeight - nu7ActivationHeight),
                Consensus::NU7_POW_TARGET_SPACING_RATIO * int64_t(nFundingPeriodLength));
        }
        return floorDiv(periodOffset(nHeight), nFundingPeriodLength);
    }

    int Params::FundingPeriodIndex(int fundingStreamStartHeight, int nHeight) const {
        assert(fundingStreamStartHeight <= nHeight);

        // AddressIndex(height) := AddressPeriod(height) - AddressPeriod(StartHeight).
        // A start height that is not aligned to a period boundary makes the first
        // funding period shorter than the others.
        return (int) (FundingStreamAddressPeriod(nHeight) - FundingStreamAddressPeriod(fundingStreamStartHeight));
    }

    std::variant<FundingStream, FundingStreamError> FundingStream::ValidateFundingStream(
        const Consensus::Params& params,
        const int startHeight,
        const int endHeight,
        const std::vector<FundingStreamRecipient>& recipients
    ) {
        if (!params.NetworkUpgradeActive(startHeight, Consensus::UPGRADE_CANOPY)) {
            return FundingStreamError::CANOPY_NOT_ACTIVE;
        }

        if (endHeight < startHeight) {
            return FundingStreamError::ILLEGAL_RANGE;
        }

        const auto expectedRecipients = params.FundingPeriodIndex(startHeight, endHeight - 1) + 1;
        if (expectedRecipients > recipients.size()) {
            return FundingStreamError::INSUFFICIENT_RECIPIENTS;
        }

        // Lockbox output periods must not start before NU6
        if (!params.NetworkUpgradeActive(startHeight, Consensus::UPGRADE_NU6)) {
            for (auto recipient : recipients) {
                if (std::holds_alternative<Consensus::Lockbox>(recipient)) {
                    return FundingStreamError::NU6_NOT_ACTIVE;
                }
            }
        }

        return FundingStream(startHeight, endHeight, recipients);
    };

    class GetFundingStreamOrThrow {
    public:
        FundingStream operator()(const FundingStream& fs) const {
            return fs;
        }

        FundingStream operator()(const FundingStreamError& e) const {
            switch (e) {
                case FundingStreamError::CANOPY_NOT_ACTIVE:
                    throw std::runtime_error("Canopy network upgrade not active at funding stream start height.");
                case FundingStreamError::ILLEGAL_RANGE:
                    throw std::runtime_error("Illegal start/end height combination for funding stream.");
                case FundingStreamError::INSUFFICIENT_RECIPIENTS:
                    throw std::runtime_error("Insufficient recipient identifiers to fully exhaust funding stream.");
                case FundingStreamError::NU6_NOT_ACTIVE:
                    throw std::runtime_error("NU6 network upgrade not active at lockbox period start height.");
                default:
                    throw std::runtime_error("Unrecognized error validating funding stream.");
            };
        }
    };

    FundingStream FundingStream::ParseFundingStream(
        const Consensus::Params& params,
        const KeyConstants& keyConstants,
        const int startHeight,
        const int endHeight,
        const std::vector<std::string>& strAddresses,
        const bool allowDeferredPool)
    {
        KeyIO keyIO(keyConstants);

        // Parse the address strings into concrete types.
        std::vector<FundingStreamRecipient> recipients;
        for (const auto& strAddr : strAddresses) {
            if (allowDeferredPool && strAddr == "DEFERRED_POOL") {
                recipients.push_back(Lockbox());
                continue;
            }

            auto addr = keyIO.DecodePaymentAddress(strAddr);
            if (!addr.has_value()) {
                throw std::runtime_error("Funding stream address was not a valid " PACKAGE_NAME " address.");
            }

            examine(addr.value(), match {
                [&](const CKeyID& keyId) {
                    recipients.push_back(GetScriptForDestination(keyId));
                },
                [&](const CScriptID& scriptId) {
                    recipients.push_back(GetScriptForDestination(scriptId));
                },
                [&](const libzcash::SaplingPaymentAddress& zaddr) {
                    recipients.push_back(zaddr);
                },
                [&](const auto& zaddr) {
                    throw std::runtime_error("Funding stream address was not a valid transparent P2SH or Sapling address.");
                }
            });
        }

        auto validationResult = FundingStream::ValidateFundingStream(params, startHeight, endHeight, recipients);
        return std::visit(GetFundingStreamOrThrow(), validationResult);
    };

    OnetimeLockboxDisbursement OnetimeLockboxDisbursement::Parse(
        const Consensus::Params& params,
        const KeyConstants& keyConstants,
        const UpgradeIndex upgrade,
        const CAmount zatoshis,
        const std::string& strAddress)
    {
        KeyIO keyIO(keyConstants);

        if (upgrade < Consensus::UPGRADE_NU6_1) {
            throw std::runtime_error("Cannot define one-time lockbox disbursements prior to NU6.1.");
        }

        // Parse the address string into concrete types.
        auto addr = keyIO.DecodePaymentAddress(strAddress);
        if (!addr.has_value()) {
            throw std::runtime_error("One-time lockbox disbursement address was not a valid " PACKAGE_NAME " address.");
        }

        CScript recipient;
        examine(addr.value(), match {
            [&](const CScriptID& scriptId) {
                recipient = GetScriptForDestination(scriptId);
            },
            [&](const auto& zaddr) {
                throw std::runtime_error("One-time lockbox disbursement address was not a valid transparent P2SH address.");
            }
        });

        // TODO: Consider verifying that the set of (recipient, amount) tuples
        // are distinct from all possible funding stream tuples.
        return OnetimeLockboxDisbursement(upgrade, zatoshis, recipient);
    };

    void Params::AddZIP207FundingStream(
        const KeyConstants& keyConstants,
        FundingStreamIndex idx,
        int startHeight,
        int endHeight,
        const std::vector<std::string>& strAddresses)
    {
        vFundingStreams[idx] = FundingStream::ParseFundingStream(
                *this, keyConstants,
                startHeight, endHeight, strAddresses,
                false);
    };

    void Params::AddZIP207LockboxStream(
        const KeyConstants& keyConstants,
        FundingStreamIndex idx,
        int startHeight,
        int endHeight)
    {
        auto intervalCount = FundingPeriodIndex(startHeight, endHeight - 1) + 1;
        std::vector<FundingStreamRecipient> recipients(intervalCount, Lockbox());
        auto validationResult = FundingStream::ValidateFundingStream(
                *this,
                startHeight,
                endHeight,
                recipients);
        vFundingStreams[idx] = std::visit(GetFundingStreamOrThrow(), validationResult);
    };

    void Params::AddZIP271LockboxDisbursement(
        const KeyConstants& keyConstants,
        OnetimeLockboxDisbursementIndex idx,
        UpgradeIndex upgrade,
        CAmount zatoshis,
        const std::string& strAddress)
    {
        vOnetimeLockboxDisbursements[idx] = OnetimeLockboxDisbursement::Parse(
                *this, keyConstants,
                upgrade, zatoshis, strAddress);
    };

    CAmount Params::GetBlockSubsidy(int nHeight) const
    {
        CAmount nSubsidy = 12.5 * COIN;

        // Mining slow start
        // The subsidy is ramped up linearly, skipping the middle payout of
        // MAX_SUBSIDY/2 to keep the monetary curve consistent with no slow start.
        if (nHeight < this->SubsidySlowStartShift()) {
            nSubsidy /= this->nSubsidySlowStartInterval;
            nSubsidy *= nHeight;
            return nSubsidy;
        } else if (nHeight < this->nSubsidySlowStartInterval) {
            nSubsidy /= this->nSubsidySlowStartInterval;
            nSubsidy *= (nHeight+1);
            return nSubsidy;
        }

        assert(nHeight >= this->SubsidySlowStartShift());

        int halvings = this->Halving(nHeight);

        // Force block reward to zero when right shift is undefined.
        if (halvings >= 64)
            return 0;

        // zip208
        // BlockSubsidy(height) :=
        // SlowStartRate · height, if height < SlowStartInterval / 2
        // SlowStartRate · (height + 1), if SlowStartInterval / 2 ≤ height and height < SlowStartInterval
        // floor(MaxBlockSubsidy / 2^Halving(height)), if SlowStartInterval ≤ height and not IsBlossomActivated(height)
        // floor(MaxBlockSubsidy / (BlossomPoWTargetSpacingRatio · 2^Halving(height))), otherwise
        // floor(MaxBlockSubsidy / (BlossomPoWTargetSpacingRatio · NU7PoWTargetSpacingRatio · 2^Halving(height))), if IsNU7Activated(height) (zip218)
        if (this->NetworkUpgradeActive(nHeight, Consensus::UPGRADE_NU7)) {
            return (nSubsidy / (Consensus::BLOSSOM_POW_TARGET_SPACING_RATIO * Consensus::NU7_POW_TARGET_SPACING_RATIO)) >> halvings;
        } else if (this->NetworkUpgradeActive(nHeight, Consensus::UPGRADE_BLOSSOM)) {
            return (nSubsidy / Consensus::BLOSSOM_POW_TARGET_SPACING_RATIO) >> halvings;
        } else {
            // Subsidy is cut in half every 840,000 blocks which will occur approximately every 4 years.
            return nSubsidy >> halvings;
        }
    }

    CAmount Params::MinerFeeShare(int nHeight, CAmount nFees) const {
        assert(MoneyRange(nFees));
        if (!NetworkUpgradeActive(nHeight, Consensus::UPGRADE_NU7)) {
            return nFees;
        }
        // NSMFeeContribution := floor(6 * TransactionFees / 10), over the whole block.
        return nFees - (nFees * 6) / 10;
    }

    /**
     * The lowest height above `height` at which the scheduled block subsidy may change:
     * the next halving or the next target spacing era (Blossom or NU7), if any.
     */
    static std::optional<int> NextSubsidyBoundary(const Params& params, int height) {
        std::optional<int> boundary = params.HeightForHalving(params.Halving(height) + 1);
        for (auto idx : {Consensus::UPGRADE_BLOSSOM, Consensus::UPGRADE_NU7}) {
            auto era = params.GetActivationHeight(idx);
            if (era.has_value() && *era > height && (!boundary.has_value() || *era < *boundary)) {
                boundary = era;
            }
        }
        return boundary;
    }

    CAmount Params::ScheduledIssuance(int nHeight) const {
        CAmount total = 0;

        // The slow start pays rate * h below its shift and rate * (h + 1) from the shift
        // to its end (see GetBlockSubsidy), so each phase sums consecutive integers.
        const int64_t interval = nSubsidySlowStartInterval;
        const int64_t shift = SubsidySlowStartShift();
        if (interval > 0 && shift > 0 && nHeight >= 1) {
            const CAmount rate = CAmount(12.5 * COIN) / interval;
            auto sumFromOneThrough = [](int64_t n) { return n * (n + 1) / 2; };
            const int64_t slowStartEnd = std::min<int64_t>(nHeight, interval - 1);
            total += rate * sumFromOneThrough(std::min(slowStartEnd, shift - 1));
            if (slowStartEnd >= shift) {
                total += rate * (sumFromOneThrough(slowStartEnd + 1) - sumFromOneThrough(shift));
            }
        }

        // After the slow start the subsidy only changes at a halving or a spacing era,
        // so sum constant runs.
        int block = std::max<int>(interval, 1);
        while (block <= nHeight) {
            const CAmount subsidy = GetBlockSubsidy(block);
            if (subsidy == 0) {
                break;
            }
            const auto boundary = NextSubsidyBoundary(*this, block);
            const int runEnd = boundary.has_value() ? std::min(*boundary - 1, nHeight) : nHeight;
            total += CAmount(runEnd - block + 1) * subsidy;
            assert(total >= 0);
            if (runEnd == nHeight) {
                break;
            }
            block = runEnd + 1;
        }
        return total;
    }

    std::optional<int> Params::NSMReissuanceHeight() const {
        const auto nu7Activation = GetActivationHeight(Consensus::UPGRADE_NU7);
        if (!nu7Activation.has_value()) {
            return std::nullopt;
        }
        if (nTestNSMReissuanceHeight.has_value()) {
            return std::max(*nTestNSMReissuanceHeight, *nu7Activation);
        }
        const auto thirdHalving = HeightForHalving(3);
        if (!thirdHalving.has_value() || *thirdHalving == std::numeric_limits<int>::max()) {
            return std::nullopt;
        }
        const auto fourthHalving = HeightForHalving(4);
        const int runEnd = fourthHalving.has_value() ? *fourthHalving - 1 : std::numeric_limits<int>::max();
        const int first = std::max(*thirdHalving + 1, *nu7Activation);
        if (first > runEnd) {
            return std::nullopt;
        }

        // The subsidy is constant on [first, runEnd], so solve for the first crossing:
        // ceil(fraction * reserve) < subsidy exactly when reserve <= maxReserve.
        const CAmount subsidy = GetBlockSubsidy(first);
        if (subsidy == 0) {
            return std::nullopt;
        }
        const uint64_t maxReserve =
            (uint64_t(subsidy) - 1) * NSM_BLOCK_SUBSIDY_FRACTION_DENOMINATOR / NSM_BLOCK_SUBSIDY_FRACTION_NUMERATOR;
        const CAmount supplyBeforeFirst = ScheduledIssuance(first - 1);
        const uint64_t reserve = MAX_MONEY > supplyBeforeFirst ? uint64_t(MAX_MONEY - supplyBeforeFirst) : 0;
        const uint64_t excess = reserve > maxReserve ? reserve - maxReserve : 0;
        const uint64_t blocksUntilCrossing = (excess + uint64_t(subsidy) - 1) / uint64_t(subsidy);
        const uint64_t crossing = uint64_t(first) + blocksUntilCrossing;
        if (crossing > uint64_t(runEnd)) {
            return std::nullopt;
        }
        return int(crossing);
    }

    bool Params::IsNSMReissuanceActive(int nHeight) const {
        // Reissuance never starts before NU7, so skip deriving the height for older blocks.
        if (!NetworkUpgradeActive(nHeight, Consensus::UPGRADE_NU7)) {
            return false;
        }
        const auto start = NSMReissuanceHeight();
        return start.has_value() && nHeight >= *start;
    }

    CAmount Params::AdditionalBlockSubsidy(int nHeight, CAmount parentNSMValueBalance) const {
        if (!IsNSMReissuanceActive(nHeight)) {
            return 0;
        }
        assert(MoneyRange(parentNSMValueBalance));
        // MAX_MONEY * 1375 fits in an int64_t, so this ceiling division cannot overflow.
        return (parentNSMValueBalance * NSM_BLOCK_SUBSIDY_FRACTION_NUMERATOR + NSM_BLOCK_SUBSIDY_FRACTION_DENOMINATOR - 1)
            / NSM_BLOCK_SUBSIDY_FRACTION_DENOMINATOR;
    }

    std::vector<std::pair<FSInfo, FundingStream>> Params::GetActiveFundingStreams(int nHeight) const
    {
        std::vector<std::pair<FSInfo, FundingStream>> activeStreams;

        // Funding streams are disabled if Canopy is not active.
        if (NetworkUpgradeActive(nHeight, Consensus::UPGRADE_CANOPY)) {
            for (uint32_t idx = Consensus::FIRST_FUNDING_STREAM; idx < Consensus::MAX_FUNDING_STREAMS; idx++) {
                // The following indexed access is safe as Consensus::MAX_FUNDING_STREAMS is used
                // in the definition of vFundingStreams.
                auto fs = vFundingStreams[idx];

                // Funding period is [startHeight, endHeight).
                if (fs && nHeight >= fs.value().GetStartHeight() && nHeight < fs.value().GetEndHeight()) {
                    activeStreams.push_back(std::make_pair(FundingStreamInfo[idx], fs.value()));
                }
            }
        }

        return activeStreams;
    };

    std::set<FundingStreamElement> Params::GetActiveFundingStreamElements(int nHeight) const
    {
        return GetActiveFundingStreamElements(nHeight, GetBlockSubsidy(nHeight));
    }

    std::set<FundingStreamElement> Params::GetActiveFundingStreamElements(
        int nHeight,
        CAmount blockSubsidy) const
    {
        std::set<std::pair<FundingStreamRecipient, CAmount>> requiredElements;

        // Funding streams are disabled if Canopy is not active.
        if (NetworkUpgradeActive(nHeight, Consensus::UPGRADE_CANOPY)) {
            for (const auto& [fsinfo, fs] : GetActiveFundingStreams(nHeight)) {
                requiredElements.insert(std::make_pair(
                    fs.Recipient(*this, nHeight),
                    fsinfo.Value(blockSubsidy)));
            }
        }

        return requiredElements;
    };

    std::vector<OnetimeLockboxDisbursement> Params::GetLockboxDisbursementsForHeight(int nHeight) const
    {
        std::vector<OnetimeLockboxDisbursement> disbursements;

        // Disbursements are disabled if NU6.1 is not active.
        if (NetworkUpgradeActive(nHeight, Consensus::UPGRADE_NU6_1)) {
            for (uint32_t idx = Consensus::FIRST_ONETIME_LOCKBOX_DISBURSEMENT; idx < Consensus::MAX_ONETIME_LOCKBOX_DISBURSEMENTS; idx++) {
                // The following indexed access is safe as
                // Consensus::MAX_ONETIME_LOCKBOX_DISBURSEMENTS is used
                // in the definition of vOnetimeLockboxDisbursements.
                auto ld = vOnetimeLockboxDisbursements[idx];

                if (ld && GetActivationHeight(ld.value().GetUpgrade()) == nHeight) {
                    disbursements.push_back(ld.value());
                }
            }
        }

        return disbursements;
    };

    FundingStreamRecipient FundingStream::Recipient(const Consensus::Params& params, int nHeight) const
    {
        auto addressIndex = params.FundingPeriodIndex(startHeight, nHeight);

        assert(addressIndex >= 0 && addressIndex < recipients.size());
        return recipients[addressIndex];
    };

    int64_t Params::PoWTargetSpacing(int nHeight) const {
        // zip208, zip218
        // PoWTargetSpacing(height) :=
        // PreBlossomPoWTargetSpacing, if not IsBlossomActivated(height)
        // PostBlossomPoWTargetSpacing, if IsBlossomActivated(height) and not IsNU7Activated(height)
        // PostNU7PoWTargetSpacing, otherwise.
        if (NetworkUpgradeActive(nHeight, Consensus::UPGRADE_NU7)) {
            return nPostNU7PowTargetSpacing;
        }
        bool blossomActive = NetworkUpgradeActive(nHeight, Consensus::UPGRADE_BLOSSOM);
        return blossomActive ? nPostBlossomPowTargetSpacing : nPreBlossomPowTargetSpacing;
    }

    int64_t Params::PoWAveragingWindow(int nHeight) const {
        return NetworkUpgradeActive(nHeight, Consensus::UPGRADE_NU7)
            ? nPostNU7PowAveragingWindow
            : nPowAveragingWindow;
    }

    int64_t Params::AveragingWindowTimespan(int nHeight) const {
        return PoWAveragingWindow(nHeight) * PoWTargetSpacing(nHeight);
    }

    int64_t Params::MinDifficultyGap(int nHeight) const {
        const int spacings = NetworkUpgradeActive(nHeight, Consensus::UPGRADE_NU7)
            ? POST_NU7_MIN_DIFFICULTY_GAP_SPACINGS
            : PRE_NU7_MIN_DIFFICULTY_GAP_SPACINGS;
        return PoWTargetSpacing(nHeight) * spacings;
    }

    int64_t Params::MinActualTimespan(int nHeight) const {
        return (AveragingWindowTimespan(nHeight) * (100 - nPowMaxAdjustUp)) / 100;
    }

    int64_t Params::MaxActualTimespan(int nHeight) const {
        return (AveragingWindowTimespan(nHeight) * (100 + nPowMaxAdjustDown)) / 100;
    }

    int NU7AdjustedFundingStreamHeight(int height, std::optional<int> nu7Activation) {
        if (!nu7Activation.has_value() || *nu7Activation >= height) {
            return height;
        }
        const int64_t moved = *nu7Activation
            + int64_t(Consensus::NU7_POW_TARGET_SPACING_RATIO) * (height - *nu7Activation);
        assert(moved <= std::numeric_limits<int>::max());
        return (int) moved;
    }
}
