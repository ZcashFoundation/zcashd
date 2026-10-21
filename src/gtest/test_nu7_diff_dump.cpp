#include <gtest/gtest.h>

#include "arith_uint256.h"
#include "chain.h"
#include "chainparams.h"
#include "consensus/upgrades.h"
#include "consensus/validation.h"
#include "key_io.h"
#include "main.h"
#include "miner.h"
#include "pow.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "tinyformat.h"
#include "util/match.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

// The NU7 differential dump (qa/zcash/nu7-diff/SPEC.md): zcashd's value of every quantity,
// from its production functions, for compare.py to check against Zakura's. Run with
//
//   NU7_DIFF_OUT=zcashd.jsonl ./src/zcash-gtest --gtest_also_run_disabled_tests \
//       --gtest_filter=NU7Diff.DISABLED_Dump

namespace {

const std::vector<CAmount> NSM_BALANCES{
    0, 1, 100000000, 1000000000000, 21000000000000, 500000000000000, 1050000000000000};
const std::vector<CAmount> FEES{
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 99, 100, 101, 999, 12345, 100000001, 1099511627783};
const std::vector<std::string> RECEIVERS{"Ecc", "ZcashFoundation", "MajorGrants", "Deferred"};

/** Zakura's receiver name for a zcashd funding stream recipient. */
std::string ReceiverName(const std::string& recipient)
{
    static const std::map<std::string, std::string> names{
        {"Electric Coin Company", "Ecc"},
        {"Zcash Foundation", "ZcashFoundation"},
        {"Major Grants", "MajorGrants"},
        {"Zcash Community Grants NU6", "MajorGrants"},
        {"Zcash Community Grants to third halving", "MajorGrants"},
        {"Lockbox NU6", "Deferred"},
        {"Coinholder-Controlled Fund to third halving", "Deferred"},
    };
    auto it = names.find(recipient);
    return it == names.end() ? "Unknown:" + recipient : it->second;
}

/** Zakura's name for an upgrade epoch. */
std::string UpgradeName(int epoch)
{
    static const std::map<int, std::string> names{
        {Consensus::BASE_SPROUT, "BeforeOverwinter"}, {Consensus::UPGRADE_OVERWINTER, "Overwinter"},
        {Consensus::UPGRADE_SAPLING, "Sapling"}, {Consensus::UPGRADE_BLOSSOM, "Blossom"},
        {Consensus::UPGRADE_HEARTWOOD, "Heartwood"}, {Consensus::UPGRADE_CANOPY, "Canopy"},
        {Consensus::UPGRADE_NU5, "Nu5"}, {Consensus::UPGRADE_NU6, "Nu6"}, {Consensus::UPGRADE_NU6_1, "Nu6_1"},
        {Consensus::UPGRADE_NU6_2, "Nu6_2"}, {Consensus::UPGRADE_NU6_3, "Nu6_3"}, {Consensus::UPGRADE_NU7, "Nu7"},
    };
    auto it = names.find(epoch);
    return it == names.end() ? strprintf("Unknown:%d", epoch) : it->second;
}

/** One scenario of the dump: a network and its NU7 activation height. */
struct Scenario {
    std::string name;
    const CChainParams& chainparams;
    int nu7;
    int lo;
    int hi;
    bool isTestnet;
    bool difficulty;
};

/** Writes JSONL records for one scenario. */
class Dump {
    std::ofstream& out;
    const Scenario& s;
    const Consensus::Params& p;

public:
    Dump(std::ofstream& out, const Scenario& s) : out(out), s(s), p(s.chainparams.GetConsensus()) {}

    /** Writes one record; `height` is null when unset. */
    void Emit(const std::string& quantity, const std::string& key, std::optional<int> height, const std::string& value)
    {
        out << "{\"scenario\": \"" << s.name << "\", \"quantity\": \"" << quantity << "\", \"key\": \"" << key
            << "\", \"height\": " << (height.has_value() ? std::to_string(height.value()) : "null")
            << ", \"value\": \"" << value << "\"}\n";
    }

    /** The active funding stream values at `height` by receiver, from GetActiveFundingStreams. */
    std::map<std::string, CAmount> StreamValues(int height) const
    {
        std::map<std::string, CAmount> values;
        const CAmount subsidy = p.GetBlockSubsidy(height);
        for (const auto& [info, fs] : p.GetActiveFundingStreams(height)) {
            values[ReceiverName(info.recipient)] += info.Value(subsidy);
        }
        return values;
    }

    /** Section A's cheap values at `height`, in a fixed order of (quantity, key, value). */
    std::vector<std::tuple<std::string, std::string, std::string>> Cheap(int height) const
    {
        std::vector<std::tuple<std::string, std::string, std::string>> out;
        out.emplace_back("nu", "", UpgradeName(CurrentEpoch(height, p)));
        out.emplace_back("branch_id", "", strprintf("0x%08x", CurrentEpochBranchId(height, p)));
        out.emplace_back("target_spacing", "", std::to_string(p.PoWTargetSpacing(height)));
        out.emplace_back("averaging_window", "", std::to_string(p.PoWAveragingWindow(height)));
        out.emplace_back("halving_index", "", std::to_string(p.Halving(height)));
        const CAmount subsidy = p.GetBlockSubsidy(height);
        out.emplace_back("block_subsidy", "", std::to_string(subsidy));
        const auto values = StreamValues(height);
        for (const auto& r : RECEIVERS) {
            auto it = values.find(r);
            out.emplace_back("funding_stream_value", r, std::to_string(it == values.end() ? 0 : it->second));
        }
        out.emplace_back("lockbox_value", "", std::to_string(values.count("Deferred") ? values.at("Deferred") : 0));
        out.emplace_back("miner_subsidy", "", std::to_string(MinerSubsidy(s.chainparams, height)));
        if (s.isTestnet) {
            out.emplace_back("testnet_min_difficulty_gap_secs", "", std::to_string(p.MinDifficultyGap(height)));
        }
        return out;
    }

    /**
     * The miner's output in the coinbase the miner builds at `height` for a transparent miner,
     * with no fees and no NSM reissuance.
     */
    CAmount CoinbaseMinerOutput(int height) const
    {
        boost::shared_ptr<CReserveScript> script(new CReserveScript());
        script->reserveScript = CScript() << OP_TRUE;
        return CreateCoinbaseTransaction(s.chainparams, 0, 0, script, height).vout[0].nValue;
    }

    /** The address the coinbase at `height` must pay `receiver`, or "none". */
    std::string StreamAddress(int height, const std::string& receiver) const
    {
        KeyIO keyIO(s.chainparams);
        for (const auto& [info, fs] : p.GetActiveFundingStreams(height)) {
            if (ReceiverName(info.recipient) != receiver) {
                continue;
            }
            return examine(fs.Recipient(p, height), match {
                [&](const CScript& script) {
                    CTxDestination dest;
                    return ExtractDestination(script, dest) ? keyIO.EncodeDestination(dest) : std::string("ERR:script");
                },
                [&](const libzcash::SaplingPaymentAddress& addr) { return keyIO.EncodePaymentAddress(addr); },
                [](const Consensus::Lockbox&) { return std::string("none"); },
            });
        }
        return "none";
    }

    /** Heights in [from, to] where the address period changes, inside funding stream ranges. */
    std::set<int> PeriodBoundaries(int from, int to) const
    {
        std::set<int> out;
        for (const auto& fs : p.vFundingStreams) {
            if (!fs) {
                continue;
            }
            int cur = std::max(fs->GetStartHeight(), from);
            const int end = std::min(fs->GetEndHeight() - 1, to);
            while (cur < end && p.FundingStreamAddressPeriod(end) != p.FundingStreamAddressPeriod(cur)) {
                // The period never decreases, so binary search for the next change.
                const int64_t period = p.FundingStreamAddressPeriod(cur);
                int a = cur + 1, b = end;
                while (a < b) {
                    const int m = a + (b - a) / 2;
                    if (p.FundingStreamAddressPeriod(m) > period) {
                        b = m;
                    } else {
                        a = m + 1;
                    }
                }
                out.insert(a);
                cur = a;
            }
        }
        return out;
    }

    /** Whether some active funding stream pays `receiver` at `height`. */
    bool IsRecipient(int height, const std::string& receiver) const
    {
        for (const auto& [info, fs] : p.GetActiveFundingStreams(height)) {
            if (ReceiverName(info.recipient) == receiver) {
                return true;
            }
        }
        return false;
    }

    void SectionA()
    {
        // Cheap quantities: records at the first height and at every change.
        std::set<int> changes;
        std::vector<std::tuple<std::string, std::string, std::string>> prev;
        for (int h = s.lo; h <= s.hi; h++) {
            auto cur = Cheap(h);
            bool any = false;
            for (size_t i = 0; i < cur.size(); i++) {
                if (prev.empty() || std::get<2>(prev[i]) != std::get<2>(cur[i])) {
                    Emit(std::get<0>(cur[i]), std::get<1>(cur[i]), h, std::get<2>(cur[i]));
                    any = true;
                }
            }
            if (any && h != s.lo) {
                changes.insert(h);
            }
            prev = std::move(cur);
        }

        // The coinbase the miner builds must pay it MinerSubsidy. A whole coinbase is too slow to
        // build at every height, but the change points are where its outputs change.
        EXPECT_EQ(CoinbaseMinerOutput(s.lo), MinerSubsidy(s.chainparams, s.lo)) << s.name << " " << s.lo;
        for (int h : changes) {
            EXPECT_EQ(CoinbaseMinerOutput(h), MinerSubsidy(s.chainparams, h)) << s.name << " " << h;
        }

        // Expensive quantities, at the heights the Zakura harness uses.
        std::set<int> heights{s.lo};
        for (int h : changes) {
            heights.insert(h);
            heights.insert(h - 1);
        }
        for (int h = ((s.lo + 999) / 1000) * 1000; h <= s.hi; h += 1000) {
            heights.insert(h);
        }
        for (int h = s.nu7 - 10; h <= s.nu7 + 10; h++) {
            heights.insert(h);
        }
        for (int b : PeriodBoundaries(s.lo, s.hi)) {
            heights.insert(b);
            heights.insert(b - 1);
        }
        if (auto start = p.NSMReissuanceHeight()) {
            for (int h : {start.value() - 1, start.value(), start.value() + 1}) {
                heights.insert(h);
            }
        }
        for (int h : heights) {
            if (h < s.lo || h > s.hi) {
                continue;
            }
            for (const auto& r : RECEIVERS) {
                Emit("funding_stream_address", r, h, StreamAddress(h, r));
            }
            for (CAmount balance : NSM_BALANCES) {
                Emit("nsm_reissuance", std::to_string(balance), h, std::to_string(p.AdditionalBlockSubsidy(h, balance)));
            }
        }
    }

    void SectionB()
    {
        std::vector<std::string> halvings;
        for (int i = 1; i <= 6; i++) {
            auto h = p.HeightForHalving(i);
            halvings.push_back(h.has_value() ? std::to_string(h.value()) : "none");
        }
        Emit("halving_heights", "", std::nullopt, Join(halvings));

        for (const auto& r : RECEIVERS) {
            std::vector<std::pair<int, int>> ranges;
            for (uint32_t idx = Consensus::FIRST_FUNDING_STREAM; idx < Consensus::MAX_FUNDING_STREAMS; idx++) {
                const auto& fs = p.vFundingStreams[idx];
                if (fs && ReceiverName(Consensus::FundingStreamInfo[idx].recipient) == r) {
                    ranges.emplace_back(fs->GetStartHeight(), fs->GetEndHeight());
                }
            }
            std::sort(ranges.begin(), ranges.end());
            std::vector<std::string> parts;
            for (const auto& [start, end] : ranges) {
                parts.push_back(strprintf("%d..%d", start, end));
            }
            Emit("funding_stream_ranges", r, std::nullopt, parts.empty() ? "none" : Join(parts));
        }

        auto start = p.NSMReissuanceHeight();
        Emit("nsm_reissuance_start_height", "", std::nullopt, start.has_value() ? std::to_string(start.value()) : "none");

        const auto boundaries = PeriodBoundaries(s.nu7, std::numeric_limits<int>::max());
        for (const auto& r : RECEIVERS) {
            std::vector<std::string> found;
            if (r != "Deferred") {
                for (int b : boundaries) {
                    if (found.size() < 10 && IsRecipient(b, r) && IsRecipient(b - 1, r)) {
                        found.push_back(std::to_string(b));
                    }
                }
            }
            Emit("address_period_boundaries", r, std::nullopt, found.empty() ? "none" : Join(found));
        }
    }

    void SectionC()
    {
        for (int h : {s.nu7 - 1, s.nu7, s.nu7 + 1}) {
            for (CAmount fees : FEES) {
                const CAmount share = p.MinerFeeShare(h, fees);
                Emit("fee_burn", std::to_string(fees), h, std::to_string(fees - share));
                Emit("miner_fee_share", std::to_string(fees), h, std::to_string(share));
            }
        }
    }

    /**
     * Section D (and D2): a synthetic chain from `start` to `end` whose first 29 blocks
     * have `startBits`, and whose later bits are GetNextWorkRequired's own results.
     */
    void Difficulty(
        const std::string& suffix, int start, int end, uint32_t startBits, std::function<int64_t(int)> interval)
    {
        std::vector<CBlockIndex> blocks(end - start + 1);
        for (int h = start; h <= end; h++) {
            CBlockIndex& index = blocks[h - start];
            index.nHeight = h;
            index.pprev = h == start ? nullptr : &blocks[h - start - 1];
            index.nTime = h == start ? 1760000000 : blocks[h - start - 1].nTime + interval(h);
            if (h <= start + 28) {
                index.nBits = startBits;
                continue;
            }
            CBlockHeader header;
            header.nTime = index.nTime;
            index.nBits = GetNextWorkRequired(index.pprev, &header, p);
            if (h + 150 < s.nu7) {
                continue;
            }
            Emit("expected_bits" + suffix, "", h, strprintf("0x%08x", index.nBits));
            Emit("median_time_past" + suffix, "", h, std::to_string(index.pprev->GetMedianTimePast()));
            if (s.isTestnet && suffix.empty()) {
                Emit("is_min_difficulty_block", "", h, IsMinDifficultyBlock(index.pprev, &header, p) ? "true" : "false");
            }
        }
    }

    void SectionD()
    {
        static const int64_t PRE[12] = {37, 75, 90, 60, 150, 20, 25, 25, 10, 500, 75, 30};
        static const int64_t POST[12] = {12, 25, 30, 20, 50, 25, 8, 40, 25, 480, 25, 460};
        const bool isRegtest = s.chainparams.NetworkIDString() == CBaseChainParams::REGTEST;
        const int start = isRegtest ? 1 : s.nu7 - 200;
        const int end = isRegtest ? 600 : s.nu7 + 300;
        const uint32_t startBits = isRegtest ? UintToArith256(p.powLimit).GetCompact() : 0x1f07ffff;
        Difficulty("", start, end, startBits, [&](int h) { return h < s.nu7 ? PRE[h % 12] : POST[h % 12]; });

        // D2: a chain near the target spacing, away from the PoW limit, with a slow and
        // then a fast stretch on each side of NU7 to reach the adjustment bounds.
        if (!isRegtest) {
            static const int64_t PRE2[12] = {60, 90, 75, 70, 80, 75, 65, 85, 75, 72, 78, 75};
            static const int64_t POST2[12] = {20, 30, 25, 23, 27, 25, 22, 28, 25, 24, 26, 25};
            Difficulty("_d2", start, end, 0x1d00ffff, [&](int h) -> int64_t {
                if (h < s.nu7) {
                    return (h >= s.nu7 - 100 && h < s.nu7 - 60) ? 225 : (h >= s.nu7 - 60 && h < s.nu7 - 30) ? 25 : PRE2[h % 12];
                }
                return (h >= s.nu7 + 100 && h < s.nu7 + 150) ? 75 : (h >= s.nu7 + 150 && h < s.nu7 + 200) ? 8 : POST2[h % 12];
            });
        }
    }

    void SectionE()
    {
        for (int h : {s.nu7 - 1, s.nu7}) {
            for (const auto& [key, group, version] : std::vector<std::tuple<std::string, uint32_t, int32_t>>{
                     {"v4", SAPLING_VERSION_GROUP_ID, SAPLING_TX_VERSION},
                     {"v5", ZIP225_VERSION_GROUP_ID, ZIP225_TX_VERSION},
                     {"v6", ZIP248_VERSION_GROUP_ID, ZIP248_TX_VERSION}}) {
                CMutableTransaction mtx;
                mtx.fOverwintered = true;
                mtx.nVersionGroupId = group;
                mtx.nVersion = version;
                if (version >= ZIP225_TX_VERSION) {
                    mtx.nConsensusBranchId = CurrentEpochBranchId(h, p);
                }
                CValidationState state;
                const bool ok = ContextualCheckTransaction(CTransaction(mtx), state, s.chainparams, h, true);
                Emit("tx_version_allowed", key, h, ok ? "true" : "false");
                Emit("tx_version_allowed", key + "-coinbase", h,
                     "MISSING:zcashd checks coinbase versions in ContextualCheckTransaction together with funding stream outputs");
            }
        }
    }

    static std::string Join(const std::vector<std::string>& parts)
    {
        std::string out;
        for (size_t i = 0; i < parts.size(); i++) {
            out += (i ? "," : "") + parts[i];
        }
        return out;
    }
};

} // namespace

TEST(NU7Diff, DISABLED_Dump) {
    const char* path = std::getenv("NU7_DIFF_OUT");
    if (path == nullptr) {
        GTEST_SKIP() << "set NU7_DIFF_OUT to the output file";
    }
    std::ofstream out(path);
    ASSERT_TRUE(out.good());

    // Regtest: every upgrade through NU6.3 at 1 and NU7 at 300, restored afterwards.
    std::vector<int> regtestHeights;
    for (int idx = Consensus::BASE_SPROUT; idx < Consensus::MAX_NETWORK_UPGRADES; idx++) {
        regtestHeights.push_back(Params(CBaseChainParams::REGTEST).GetConsensus().vUpgrades[idx].nActivationHeight);
    }
    for (int idx = Consensus::UPGRADE_OVERWINTER; idx <= Consensus::UPGRADE_NU6_3; idx++) {
        UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex(idx), 1);
    }
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_NU7, 300);

    const auto testnetA1 = CreateChainParamsWithNU7ForTesting(CBaseChainParams::TESTNET, 4200000);
    const auto testnetA2 = CreateChainParamsWithNU7ForTesting(CBaseChainParams::TESTNET, 4187001);
    const std::vector<Scenario> scenarios{
        {"testnet-A1", *testnetA1, 4200000, 4198000, 16200000, true, true},
        {"testnet-A2", *testnetA2, 4187001, 4185001, 16187001, true, false},
        {"testnet-real", Params(CBaseChainParams::TESTNET), 4465026, 4463026, 16465026, true, true},
        {"mainnet-real", Params(CBaseChainParams::MAIN), 3543405, 3541405, 15543405, false, true},
        {"regtest-R", Params(CBaseChainParams::REGTEST), 300, 1, 2000000, false, true},
    };
    const char* only = std::getenv("NU7_DIFF_ONLY");
    for (const auto& s : scenarios) {
        if (only != nullptr && s.name != only) {
            continue;
        }
        Dump dump(out, s);
        dump.SectionA();
        dump.SectionB();
        dump.SectionC();
        if (s.difficulty) {
            dump.SectionD();
        }
        dump.SectionE();
    }

    for (int idx = Consensus::BASE_SPROUT + 1; idx < Consensus::MAX_NETWORK_UPGRADES; idx++) {
        UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex(idx), regtestHeights[idx]);
    }
}
