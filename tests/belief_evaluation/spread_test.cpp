#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/spread.hpp>
#include <belief_evaluation/validation.hpp>

// Tests for spread() under both SpreadPolicy values, on hand-built
// FutureTricks: the equals bit convention, ties and deduplication.

// A namespace alias and targeted `using` declarations, not `using
// namespace dds::belief_evaluation;`. The include of spread.hpp brings in
// the global ::Card from api/dds_data_types.hpp, so a bare `Card` would be
// ambiguous instead of a clear compile error.
namespace be = dds::belief_evaluation;
using be::ProbabilitySumTolerance;
using be::SpreadPolicy;
using be::WeightedCard;
using be::spread;

// spread() and SpreadPolicy need no solver. The FutureTricks values below
// are built by hand, so a reader can check them without running the
// solver.

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
    // dds's solver_if.cpp sets futp->equals[i] = mp->sequence << 2, so bit r
    // is absolute rank r, as in Deal::remainCards. It is not the compacted
    // (r - 2) convention of legal_plays() and the lookup tables. For the
    // touching sequence KQJ, entry 0 is the king and equals[0] holds bits 12
    // and 11: (1u << Queen) | (1u << Jack), unshifted.
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

    // AllOptimal agrees: with one candidate, it is also the maximum.
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

    // TouchingSequence spreads the best entry (index 0, the ace) over its
    // empty equals group: just the ace.
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

// --- two suits tied on score: the case that separates the two policies ---

TEST_F(SpreadTest, TouchingSequenceSpreadsOnlyOverTheCanonicalBestEntrysGroup)
{
    // Two entries tie for the top score (9): the ♠K, with a touching queen,
    // and the ♥A, a singleton. dds orders entry 0 first, so TouchingSequence
    // spreads only over the ♠K's group.
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

    // Three cards: ♠K, ♠Q (touching) and ♥A.
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
    // With solutions = 3, dds can list several entries from one touching
    // sequence, each naming the others in equals. Both entries here tie for
    // the top score and belong to KQJ. The union must be exactly {K, Q, J},
    // not six weighted entries.
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
