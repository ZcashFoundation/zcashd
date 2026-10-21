#include <gtest/gtest.h>

#include "chain.h"
#include "chainparams.h"
#include "pow.h"
#include "random.h"
#include "util/test.h"

#include <utility>
#include <vector>

void TestDifficultyAveragingImpl(const Consensus::Params& params)
{
    size_t lastBlk = 2*params.nPowAveragingWindow;
    size_t firstBlk = lastBlk - params.nPowAveragingWindow;

    // Start with blocks evenly-spaced and equal difficulty
    std::vector<CBlockIndex> blocks(lastBlk+1);
    for (int i = 0; i <= lastBlk; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = i ? blocks[i - 1].nTime + params.PoWTargetSpacing(i) : 1269211443;
        blocks[i].nBits = 0x1e7fffff; /* target 0x007fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    // Result should be the same as if last difficulty was used
    arith_uint256 bnAvg;
    bnAvg.SetCompact(blocks[lastBlk].nBits);
    EXPECT_EQ(CalculateNextWorkRequired(bnAvg,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params,
                                        blocks[lastBlk].nHeight + 1),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));
    // Result should be unchanged, modulo integer division precision loss
    arith_uint256 bnRes;
    bnRes.SetCompact(0x1e7fffff);
    bnRes /= params.AveragingWindowTimespan(blocks[lastBlk].nHeight + 1);
    bnRes *= params.AveragingWindowTimespan(blocks[lastBlk].nHeight + 1);
    EXPECT_EQ(bnRes.GetCompact(), GetNextWorkRequired(&blocks[lastBlk], nullptr, params));

    // Randomise the final block time (plus 1 to ensure it is always different)
    blocks[lastBlk].nTime += GetRand(params.PoWTargetSpacing(blocks[lastBlk].nHeight + 1)/2) + 1;

    // Result should be the same as if last difficulty was used
    bnAvg.SetCompact(blocks[lastBlk].nBits);
    EXPECT_EQ(CalculateNextWorkRequired(bnAvg,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params,
                                        blocks[lastBlk].nHeight + 1),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));
    // Result should not be unchanged
    EXPECT_NE(0x1e7fffff, GetNextWorkRequired(&blocks[lastBlk], nullptr, params));

    // Change the final block difficulty
    blocks[lastBlk].nBits = 0x1e0fffff;

    // Result should not be the same as if last difficulty was used
    bnAvg.SetCompact(blocks[lastBlk].nBits);
    EXPECT_NE(CalculateNextWorkRequired(bnAvg,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params,
                                        blocks[lastBlk].nHeight + 1),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));

    // Result should be the same as if the average difficulty was used
    arith_uint256 average = UintToArith256(uint256S("0000796968696969696969696969696969696969696969696969696969696969"));
    EXPECT_EQ(CalculateNextWorkRequired(average,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params,
                                        blocks[lastBlk].nHeight + 1),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));
}

TEST(PoW, DifficultyAveraging) {
    SelectParams(CBaseChainParams::MAIN);
    TestDifficultyAveragingImpl(Params().GetConsensus());
}

TEST(PoW, DifficultyAveragingBlossom) {
    TestDifficultyAveragingImpl(RegtestActivateBlossom(true).GetConsensus());
    RegtestDeactivateBlossom();
}

TEST(PoW, MinDifficultyRules) {
    SelectParams(CBaseChainParams::TESTNET);
    const Consensus::Params& params = Params().GetConsensus();
    size_t lastBlk = 2*params.nPowAveragingWindow;
    size_t firstBlk = lastBlk - params.nPowAveragingWindow;

    // Start with blocks evenly-spaced and equal difficulty
    std::vector<CBlockIndex> blocks(lastBlk+1);
    for (int i = 0; i <= lastBlk; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = params.nPowAllowMinDifficultyBlocksAfterHeight.value() + i;
        blocks[i].nTime = i ? blocks[i - 1].nTime + params.PoWTargetSpacing(i) : 1269211443;
        blocks[i].nBits = 0x1e7fffff; /* target 0x007fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    // Create a new block at the target spacing
    CBlockHeader next;
    next.nTime = blocks[lastBlk].nTime + params.PoWTargetSpacing(blocks[lastBlk].nHeight + 1);

    // Result should be unchanged, modulo integer division precision loss
    arith_uint256 bnRes;
    bnRes.SetCompact(0x1e7fffff);
    bnRes /= params.AveragingWindowTimespan(blocks[lastBlk].nHeight + 1);
    bnRes *= params.AveragingWindowTimespan(blocks[lastBlk].nHeight + 1);
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), bnRes.GetCompact());

    // Delay last block up to the edge of the min-difficulty limit
    next.nTime += params.PoWTargetSpacing(blocks[lastBlk].nHeight + 1) * 5;

    // Result should be unchanged, modulo integer division precision loss
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), bnRes.GetCompact());

    // Delay last block over the min-difficulty limit
    next.nTime += 1;

    // Result should be the minimum difficulty
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params),
              UintToArith256(params.powLimit).GetCompact());
}

// ZIP 218 difficulty vectors, transcribed from Zakura's contextual difficulty tests
// (zakura-header-chain, validation/contextual/tests/validation.rs).
static const uint32_t NU7_VECTOR_TARGET_BITS[5] = {0x1e0ffff0, 0x1e0e0000, 0x1e0c8000, 0x1e0b4000, 0x1e0a2000};
static const int64_t NU7_VECTOR_TIME_STEPS[7] = {19, 31, 23, 29, 17, 37, 21};
static const uint32_t NU7_VECTOR_CANDIDATE_TIME = 2000000000;
// PostNU7PoWAveragingWindow plus the 11-block median-time-past span.
static const size_t NU7_VECTOR_CONTEXT_LEN = 113;

/** Testnet consensus parameters with only Blossom (at height 1) and NU7 scheduled. */
static Consensus::Params Nu7TestnetParams(int nu7Height)
{
    Consensus::Params params = Params(CBaseChainParams::TESTNET).GetConsensus();
    for (int i = Consensus::UPGRADE_OVERWINTER; i < Consensus::MAX_NETWORK_UPGRADES; i++) {
        params.vUpgrades[i].nActivationHeight = Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
    }
    params.vUpgrades[Consensus::UPGRADE_BLOSSOM].nActivationHeight = 1;
    params.vUpgrades[Consensus::UPGRADE_NU7].nActivationHeight = nu7Height;
    return params;
}

/**
 * Fills `blocks` with the `len` ancestors of a candidate block at `candidateHeight`,
 * oldest first. Counting from the parent, ancestor i has target
 * NU7_VECTOR_TARGET_BITS[i % 5] and is NU7_VECTOR_TIME_STEPS[0..=i] seconds older
 * than the candidate.
 */
static void BuildNu7VectorChain(std::vector<CBlockIndex>& blocks, int candidateHeight, size_t len)
{
    blocks.assign(len, CBlockIndex());
    int64_t time = NU7_VECTOR_CANDIDATE_TIME;
    for (size_t i = 0; i < len; i++) {
        CBlockIndex& block = blocks[len - 1 - i];
        time -= NU7_VECTOR_TIME_STEPS[i % 7];
        block.nHeight = candidateHeight - 1 - i;
        block.nTime = time;
        block.nBits = NU7_VECTOR_TARGET_BITS[i % 5];
    }
    for (size_t j = 0; j < len; j++) {
        blocks[j].pprev = j ? &blocks[j - 1] : nullptr;
    }
}

TEST(PoW, NU7DifficultyVectors) {
    const Consensus::Params params = Nu7TestnetParams(200);
    CBlockHeader candidate;
    candidate.nTime = NU7_VECTOR_CANDIDATE_TIME;

    // Before NU7 the 17-block window applies; at and after NU7 the 102-block window and
    // the 25-second spacing apply, using the pre-NU7 blocks still inside the window.
    for (const auto& [height, expected] : std::vector<std::pair<int, uint32_t>>{
             {199, 0x1e0af369}, {200, 0x1e0cd7fd}, {201, 0x1e0cd7fd}}) {
        std::vector<CBlockIndex> blocks;
        BuildNu7VectorChain(blocks, height, NU7_VECTOR_CONTEXT_LEN);
        EXPECT_EQ(GetNextWorkRequired(&blocks.back(), &candidate, params), expected)
            << "candidate height " << height;
    }
}

TEST(PoW, NU7MovesPowLimitCutoffToHeight102) {
    const Consensus::Params params = Nu7TestnetParams(1);
    CBlockHeader candidate;
    candidate.nTime = NU7_VECTOR_CANDIDATE_TIME;

    for (const auto& [height, expected] : std::vector<std::pair<int, uint32_t>>{
             {102, 0x2007ffff}, {103, 0x1e0caecf}}) {
        std::vector<CBlockIndex> blocks;
        BuildNu7VectorChain(blocks, height, height);
        EXPECT_EQ(GetNextWorkRequired(&blocks.back(), &candidate, params), expected)
            << "candidate height " << height;
    }
}

TEST(PoW, NU7TestnetMinDifficultyGap) {
    const Consensus::Params params = Nu7TestnetParams(200);
    CBlockHeader candidate;
    candidate.nTime = NU7_VECTOR_CANDIDATE_TIME;

    // From NU7 a testnet block qualifies for minimum difficulty only if it is strictly more
    // than 18 * 25 = 450 seconds after its parent (ZIP 218 and ZIP 259).
    for (const auto& [gap, expected] : std::vector<std::pair<uint32_t, uint32_t>>{
             {150, 0x1e0cd7fd}, {151, 0x1e0cd7fd}, {450, 0x1e0cd18e}, {451, 0x2007ffff}}) {
        std::vector<CBlockIndex> blocks;
        BuildNu7VectorChain(blocks, 300000, NU7_VECTOR_CONTEXT_LEN);
        blocks.back().nTime = NU7_VECTOR_CANDIDATE_TIME - gap;
        EXPECT_EQ(GetNextWorkRequired(&blocks.back(), &candidate, params), expected)
            << "parent gap " << gap;
    }
}

TEST(PoW, NU7AveragingWindowAtPowLimitDoesNotOverflow) {
    // 102 targets at the testnet PoW limit overflow a plain 256-bit sum. With every
    // target equal, the mean must be exactly that target.
    const Consensus::Params params = Nu7TestnetParams(1);
    const uint32_t limitBits = UintToArith256(params.powLimit).GetCompact();
    std::vector<CBlockIndex> blocks(NU7_VECTOR_CONTEXT_LEN);
    for (size_t i = 0; i < blocks.size(); i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = 300000 + i;
        blocks[i].nTime = 1760000000 + 25 * i;
        blocks[i].nBits = limitBits;
    }
    const CBlockIndex* last = &blocks.back();
    const CBlockIndex* first = &blocks[blocks.size() - 1 - params.PoWAveragingWindow(last->nHeight + 1)];
    arith_uint256 target;
    target.SetCompact(limitBits);
    EXPECT_EQ(GetNextWorkRequired(last, nullptr, params),
              CalculateNextWorkRequired(target, last->GetMedianTimePast(), first->GetMedianTimePast(),
                                        params, last->nHeight + 1));
}

TEST(PoW, ExactMeanMatchesPlainSumBeforeNU7) {
    // Before NU7 the window is 17 blocks, where a plain sum cannot overflow, so the
    // quotient/remainder mean must give exactly the same result as the plain one.
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();
    const size_t window = params.PoWAveragingWindow(0);
    uint64_t seed = 0x5eed;
    for (int round = 0; round < 100; round++) {
        std::vector<CBlockIndex> blocks(window + 12);
        arith_uint256 sum {0};
        for (size_t i = 0; i < blocks.size(); i++) {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
            blocks[i].nHeight = 1000 + i;
            blocks[i].nTime = 1500000000 + 150 * i + (seed >> 58);
            blocks[i].nBits = 0x1d000000 | ((seed >> 20) & 0x7fffff);
            if (i >= blocks.size() - window) {
                arith_uint256 target;
                target.SetCompact(blocks[i].nBits);
                sum += target;
            }
        }
        const CBlockIndex* last = &blocks.back();
        const CBlockIndex* first = &blocks[blocks.size() - 1 - window];
        EXPECT_EQ(GetNextWorkRequired(last, nullptr, params),
                  CalculateNextWorkRequired(sum / window, last->GetMedianTimePast(),
                                            first->GetMedianTimePast(), params, last->nHeight + 1))
            << "round " << round;
    }
}
