#include <gtest/gtest.h>

#include <bit>
#include <vector>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

#include <belief_evaluation/defender_split.hpp>
#include <belief_evaluation/types.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;

using be::DefenderPool;
using be::defender_pool_decomposition;
using be::holding;

namespace
{
    constexpr int Spades = 0;
    constexpr int Hearts = 1;
    constexpr int Diamonds = 2;
    constexpr int Clubs = 3;

    constexpr int North = 0;  // declarer in every fixture below
    constexpr int East = 1;   // the fixed seat: (declarer + 1) % DDS_HANDS
    constexpr int South = 2;  // dummy
    constexpr int West = 3;

    auto suit_rank_pairs(std::vector<be::Card> const& cards) -> std::vector<std::pair<int, int>>
    {
        std::vector<std::pair<int, int>> pairs;
        pairs.reserve(cards.size());
        for (be::Card const& card : cards)
        {
            pairs.emplace_back(card.suit, card.rank);
        }
        return pairs;
    }
}  // namespace

// --- criterion 5: hand-derived, asserted in full, three different shapes -

TEST(DefenderSplitTest, TwoSuitEndingDecomposesToTheHandDerivedPoolInCanonicalOrder)
{
    // East holds one diamond and one club (2 cards); West holds one diamond
    // and two clubs (3 cards) -- not mid-trick, just an ordinary shape with
    // a two-suit pool, to pin the suit-then-rank ordering criterion 5 asks
    // for. The mid-trick test below is the one with a count asymmetry that
    // a naive "half the pool" derivation would get wrong.
    Deal root{};
    root.trump = DDS_NOTRUMP;
    root.first = North;
    root.remainCards[North][Spades] = holding({14, 13});  // declarer: not in the pool
    root.remainCards[South][Hearts] = holding({14, 13});  // dummy: not in the pool
    root.remainCards[East][Diamonds] = holding({3});
    root.remainCards[East][Clubs] = holding({4});
    root.remainCards[West][Diamonds] = holding({5});
    root.remainCards[West][Clubs] = holding({6, 7});

    DefenderPool const pool = defender_pool_decomposition(root, North);

    // suits ascending (diamonds before clubs), ranks ascending within suit.
    std::vector<std::pair<int, int>> const expected{
        {Diamonds, 3}, {Diamonds, 5}, {Clubs, 4}, {Clubs, 6}, {Clubs, 7}};
    EXPECT_EQ(suit_rank_pairs(pool.cards), expected);
    EXPECT_EQ(pool.fixed_seat_count, 2);  // East holds D3, C4
}

TEST(DefenderSplitTest, MidTrickEndingWithOneDefenderAlreadyPlayedGivesAsymmetricCounts)
{
    // East already played the diamond 7 to the trick in progress -- it is
    // in currentTrickSuit/Rank, not in East's remainCards -- so East holds
    // one fewer diamond than West: 2 against 3, not the symmetric 2-2 or
    // 3-3 a derivation from half the pool size would wrongly assume.
    Deal root{};
    root.trump = DDS_NOTRUMP;
    root.first = North;
    root.currentTrickSuit[0] = Diamonds;
    root.currentTrickRank[0] = 7;
    root.remainCards[North][Spades] = holding({14});   // declarer: not in the pool
    root.remainCards[South][Spades] = holding({13});   // dummy: not in the pool
    root.remainCards[East][Diamonds] = holding({3, 5});
    root.remainCards[West][Diamonds] = holding({2, 4, 6});

    DefenderPool const pool = defender_pool_decomposition(root, North);

    std::vector<std::pair<int, int>> const expected{
        {Diamonds, 2}, {Diamonds, 3}, {Diamonds, 4}, {Diamonds, 5}, {Diamonds, 6}};
    EXPECT_EQ(suit_rank_pairs(pool.cards), expected);
    EXPECT_EQ(pool.fixed_seat_count, 2);  // East (fixed seat) holds D3, D5
}

TEST(DefenderSplitTest, ThreeSuitEndingWithADifferentShapeStillMatchesHandDerivation)
{
    Deal root{};
    root.trump = Spades;
    root.first = West;
    root.remainCards[North][Spades] = holding({14, 13, 12});  // declarer: not in the pool
    root.remainCards[South][Hearts] = holding({14, 13, 12});  // dummy: not in the pool
    root.remainCards[East][Spades] = holding({2});
    root.remainCards[East][Hearts] = holding({3});
    root.remainCards[East][Clubs] = holding({4, 5});
    root.remainCards[West][Spades] = holding({6, 7});
    root.remainCards[West][Clubs] = holding({8});

    DefenderPool const pool = defender_pool_decomposition(root, North);

    std::vector<std::pair<int, int>> const expected{
        {Spades, 2}, {Spades, 6}, {Spades, 7}, {Hearts, 3}, {Clubs, 4}, {Clubs, 5}, {Clubs, 8}};
    EXPECT_EQ(suit_rank_pairs(pool.cards), expected);
    EXPECT_EQ(pool.fixed_seat_count, 4);  // East holds S2, H3, C4, C5
}

// --- criterion 3: matches node.cpp's own per-suit pool, cross-checked -----

TEST(DefenderSplitTest, PerSuitPoolMatchesTheUnionOfBothDefendersRemainCardsDirectly)
{
    // Cross-checks defender_pool_decomposition's flattened cards against
    // the same per-suit union node.cpp's own (unexported) defender_pool()
    // computes, derived independently here rather than by calling it.
    Deal root{};
    root.trump = DDS_NOTRUMP;
    root.first = North;
    root.remainCards[East][Spades] = holding({2, 3});
    root.remainCards[West][Spades] = holding({4});
    root.remainCards[East][Hearts] = holding({5});
    root.remainCards[West][Hearts] = holding({6, 7});
    root.remainCards[East][Diamonds] = holding({8});
    root.remainCards[West][Clubs] = holding({9});

    DefenderPool const pool = defender_pool_decomposition(root, North);

    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        unsigned const expected_suit_pool =
            root.remainCards[East][suit] | root.remainCards[West][suit];
        unsigned actual_suit_pool = 0;
        for (be::Card const& card : pool.cards)
        {
            if (card.suit == suit)
            {
                actual_suit_pool |= (1u << card.rank);
            }
        }
        EXPECT_EQ(actual_suit_pool, expected_suit_pool) << "suit " << suit;
    }
}

// --- criterion 4: mid-trick handled, count read from the root not halved --

TEST(DefenderSplitTest, FixedSeatCountIsReadFromRootPopcountNotHalfThePoolSize)
{
    Deal root{};
    root.trump = DDS_NOTRUMP;
    root.first = North;
    root.currentTrickSuit[0] = Clubs;
    root.currentTrickRank[0] = 9;
    root.remainCards[East][Clubs] = holding({2});         // one card, already down one from the trick
    root.remainCards[West][Clubs] = holding({3, 4, 5});    // three

    DefenderPool const pool = defender_pool_decomposition(root, North);

    EXPECT_EQ(pool.cards.size(), 4u);
    EXPECT_EQ(pool.fixed_seat_count, 1);  // East -- not 2, which half of 4 would wrongly give
}

// --- degenerate roots ------------------------------------------------------

TEST(DefenderSplitTest, AnEmptyPoolIsLegalAndGivesAnEmptySpace)
{
    Deal root{};
    root.trump = DDS_NOTRUMP;
    root.first = North;
    root.remainCards[North][Spades] = holding({14});
    root.remainCards[South][Hearts] = holding({13});
    // East and West hold nothing: the pool is empty.

    DefenderPool const pool = defender_pool_decomposition(root, North);

    EXPECT_TRUE(pool.cards.empty());
    EXPECT_EQ(pool.fixed_seat_count, 0);
}

TEST(DefenderSplitTest, AFixedSeatCountOfZeroOrTheWholePoolIsLegal)
{
    Deal root{};
    root.trump = DDS_NOTRUMP;
    root.first = North;
    root.remainCards[West][Diamonds] = holding({3, 4, 5});  // all of the pool; East holds none

    DefenderPool const pool = defender_pool_decomposition(root, North);

    EXPECT_EQ(pool.cards.size(), 3u);
    EXPECT_EQ(pool.fixed_seat_count, 0);
}
