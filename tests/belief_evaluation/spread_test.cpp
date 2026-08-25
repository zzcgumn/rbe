#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

#include <api/dll.h>
#include <utility/constants.h>

#include <belief_evaluation/spread.hpp>
#include <belief_evaluation/validation.hpp>

// spread() and SpreadPolicy have no solver dependency and no solve_board
// call anywhere in this file -- FutureTricks values below are hand-built,
// pinning the two policies' behaviour against values a reader can check
// without running the solver.

namespace
{
    constexpr int Spades = 0;
    constexpr int Hearts = 1;

    constexpr int Two = 2;
    constexpr int Jack = 11;
    constexpr int Queen = 12;
    constexpr int King = 13;
    constexpr int Ace = 14;

    auto empty_future_tricks() -> FutureTricks
    {
        return FutureTricks{};  // cards = 0, every array zeroed
    }
}

class SpreadTest : public ::testing::Test
{
};

// --- equals' bit convention, pinned -----------------------------------

TEST_F(SpreadTest, EqualsIsReadInDealsBitConventionWithNoShift)
{
    // solver_if.cpp sets futp->equals[i] = mp->sequence << 2 at every
    // population site -- bit r = absolute rank r, matching
    // Deal::remainCards, not the compacted (r - 2) convention legal_plays()
    // and the lookup tables use. A three-card touching sequence king,
    // queen, jack: entry 0 is the king, and equals[0] names queen and jack
    // as touching it -- bits 12 and 11 of a Deal-convention mask, i.e.
    // (1u << Queen) | (1u << Jack), unshifted.
    FutureTricks fut = empty_future_tricks();
    fut.cards = 1;
    fut.suit[0] = Spades;
    fut.rank[0] = King;
    fut.equals[0] = (1 << Queen) | (1 << Jack);
    fut.score[0] = 9;

    std::vector<WeightedCard> const distribution = spread(fut, SpreadPolicy::TouchingSequence);

    ASSERT_EQ(distribution.size(), 3u);
    std::vector<int> ranks;
    for (WeightedCard const& entry : distribution)
    {
        EXPECT_EQ(entry.card.suit, Spades);
        ranks.push_back(entry.card.rank);
    }
    EXPECT_TRUE(std::find(ranks.begin(), ranks.end(), King) != ranks.end());
    EXPECT_TRUE(std::find(ranks.begin(), ranks.end(), Queen) != ranks.end());
    EXPECT_TRUE(std::find(ranks.begin(), ranks.end(), Jack) != ranks.end());
}

// --- cards == 1 ------------------------------------------------------------

TEST_F(SpreadTest, ASingleCandidateGetsProbabilityOne)
{
    FutureTricks fut = empty_future_tricks();
    fut.cards = 1;
    fut.suit[0] = Spades;
    fut.rank[0] = Ace;
    fut.equals[0] = 0;  // no touching cards
    fut.score[0] = 13;

    std::vector<WeightedCard> const distribution = spread(fut, SpreadPolicy::TouchingSequence);

    ASSERT_EQ(distribution.size(), 1u);
    EXPECT_EQ(distribution[0].card.suit, Spades);
    EXPECT_EQ(distribution[0].card.rank, Ace);
    EXPECT_DOUBLE_EQ(distribution[0].probability, 1.0);

    // AllOptimal agrees here too: with only one candidate, it is trivially
    // also the maximum.
    std::vector<WeightedCard> const all_optimal = spread(fut, SpreadPolicy::AllOptimal);
    ASSERT_EQ(all_optimal.size(), 1u);
    EXPECT_EQ(all_optimal[0].card.rank, Ace);
    EXPECT_DOUBLE_EQ(all_optimal[0].probability, 1.0);
}

// --- a singleton equals group (no touching cards) ---------------------

TEST_F(SpreadTest, ASingletonEqualsGroupIsJustTheOneCard)
{
    FutureTricks fut = empty_future_tricks();
    fut.cards = 2;
    fut.suit[0] = Spades;
    fut.rank[0] = Ace;
    fut.equals[0] = 0;
    fut.score[0] = 13;
    fut.suit[1] = Hearts;
    fut.rank[1] = Two;
    fut.equals[1] = 0;
    fut.score[1] = 7;  // strictly worse than the ace

    std::vector<WeightedCard> const distribution = spread(fut, SpreadPolicy::TouchingSequence);

    // TouchingSequence: the single canonical best entry (index 0, the ace)
    // spread over its own (empty) equals group -- just the ace.
    ASSERT_EQ(distribution.size(), 1u);
    EXPECT_EQ(distribution[0].card.suit, Spades);
    EXPECT_EQ(distribution[0].card.rank, Ace);
    EXPECT_DOUBLE_EQ(distribution[0].probability, 1.0);
}

// --- a three-card touching sequence ------------------------------------

TEST_F(SpreadTest, AThreeCardTouchingSequenceSplitsEvenlyAcrossAllThree)
{
    FutureTricks fut = empty_future_tricks();
    fut.cards = 1;
    fut.suit[0] = Spades;
    fut.rank[0] = King;
    fut.equals[0] = (1 << Queen) | (1 << Jack);  // KQJ all touch
    fut.score[0] = 9;

    std::vector<WeightedCard> const distribution = spread(fut, SpreadPolicy::TouchingSequence);

    ASSERT_EQ(distribution.size(), 3u);
    double total = 0.0;
    for (WeightedCard const& entry : distribution)
    {
        EXPECT_EQ(entry.card.suit, Spades);
        EXPECT_DOUBLE_EQ(entry.probability, 1.0 / 3.0);
        total += entry.probability;
    }
    EXPECT_NEAR(total, 1.0, ProbabilitySumTolerance);
}

// --- two suits tied on score -- the case distinguishing the two policies ---

TEST_F(SpreadTest, TouchingSequenceSpreadsOnlyOverTheCanonicalBestEntrysGroup)
{
    // Two entries tie for the maximum score (9): the spade king (with a
    // touching queen) and the heart ace (a singleton). dds's own ordering
    // makes entry 0 the canonical best, so TouchingSequence spreads only
    // over the spade king's group.
    FutureTricks fut = empty_future_tricks();
    fut.cards = 2;
    fut.suit[0] = Spades;
    fut.rank[0] = King;
    fut.equals[0] = (1 << Queen);
    fut.score[0] = 9;
    fut.suit[1] = Hearts;
    fut.rank[1] = Ace;
    fut.equals[1] = 0;
    fut.score[1] = 9;

    std::vector<WeightedCard> const distribution = spread(fut, SpreadPolicy::TouchingSequence);

    ASSERT_EQ(distribution.size(), 2u);
    for (WeightedCard const& entry : distribution)
    {
        EXPECT_EQ(entry.card.suit, Spades);
        EXPECT_DOUBLE_EQ(entry.probability, 0.5);
    }
}

TEST_F(SpreadTest, AllOptimalSpreadsOverEveryTiedEntrysGroupAcrossBothSuits)
{
    FutureTricks fut = empty_future_tricks();
    fut.cards = 2;
    fut.suit[0] = Spades;
    fut.rank[0] = King;
    fut.equals[0] = (1 << Queen);
    fut.score[0] = 9;
    fut.suit[1] = Hearts;
    fut.rank[1] = Ace;
    fut.equals[1] = 0;
    fut.score[1] = 9;

    std::vector<WeightedCard> const distribution = spread(fut, SpreadPolicy::AllOptimal);

    // Three cards total: spade king, spade queen (touching), heart ace.
    ASSERT_EQ(distribution.size(), 3u);
    double total = 0.0;
    int spade_count = 0;
    int heart_count = 0;
    for (WeightedCard const& entry : distribution)
    {
        EXPECT_DOUBLE_EQ(entry.probability, 1.0 / 3.0);
        total += entry.probability;
        if (entry.card.suit == Spades)
        {
            ++spade_count;
        }
        else if (entry.card.suit == Hearts)
        {
            ++heart_count;
        }
    }
    EXPECT_EQ(spade_count, 2);
    EXPECT_EQ(heart_count, 1);
    EXPECT_NEAR(total, 1.0, ProbabilitySumTolerance);
}

// --- a clear best card with a lower-scoring alternative --------------------

TEST_F(SpreadTest, AllOptimalPicksOnlyTheStrictWinnerWhenNothingTies)
{
    FutureTricks fut = empty_future_tricks();
    fut.cards = 2;
    fut.suit[0] = Spades;
    fut.rank[0] = Ace;
    fut.equals[0] = 0;
    fut.score[0] = 13;
    fut.suit[1] = Hearts;
    fut.rank[1] = Two;
    fut.equals[1] = 0;
    fut.score[1] = 7;

    std::vector<WeightedCard> const distribution = spread(fut, SpreadPolicy::AllOptimal);

    ASSERT_EQ(distribution.size(), 1u);
    EXPECT_EQ(distribution[0].card.suit, Spades);
    EXPECT_EQ(distribution[0].card.rank, Ace);
    EXPECT_DOUBLE_EQ(distribution[0].probability, 1.0);
}

// --- deduplication: AllOptimal must not double-count a shared sequence ----

TEST_F(SpreadTest, AllOptimalDoesNotDoubleCountTwoEntriesInOneSequence)
{
    // dds sometimes lists more than one entry from the same touching
    // sequence (each naming the others via equals) when solutions = 3.
    // Both entries here tie for the maximum and both belong to the same
    // KQJ sequence -- the union must still be exactly {K, Q, J}, not six
    // weighted entries.
    FutureTricks fut = empty_future_tricks();
    fut.cards = 2;
    fut.suit[0] = Spades;
    fut.rank[0] = King;
    fut.equals[0] = (1 << Queen) | (1 << Jack);
    fut.score[0] = 9;
    fut.suit[1] = Spades;
    fut.rank[1] = Queen;
    fut.equals[1] = (1 << King) | (1 << Jack);
    fut.score[1] = 9;

    std::vector<WeightedCard> const distribution = spread(fut, SpreadPolicy::AllOptimal);

    ASSERT_EQ(distribution.size(), 3u);
    double total = 0.0;
    for (WeightedCard const& entry : distribution)
    {
        EXPECT_EQ(entry.card.suit, Spades);
        EXPECT_DOUBLE_EQ(entry.probability, 1.0 / 3.0);
        total += entry.probability;
    }
    EXPECT_NEAR(total, 1.0, ProbabilitySumTolerance);
}
