#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <set>
#include <vector>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/defender_split.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/types.hpp>

#include "test_support.hpp"

// Tests for the defender pool decomposition, binomial_coefficient,
// unrank_combination and apply_defender_split.

namespace be = dds::belief_evaluation;

using be::apply_defender_split;
using be::binomial_coefficient;
using be::DefenderPool;
using be::defender_pool_decomposition;
using be::holding;
using be::is_consistent;
using be::unrank_combination;

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

// --- hand-derived pool decomposition, asserted in full, three different shapes -

TEST(DefenderSplitTest, TwoSuitEndingDecomposesToTheHandDerivedPoolInCanonicalOrder)
{
    // East holds one diamond and one club (2 cards); West holds one diamond
    // and two clubs (3 cards). Not mid-trick: an ordinary two-suit pool, to pin
    // the suit-then-rank order. The mid-trick test below has the count
    // asymmetry that "half the pool" gets wrong.
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

    // Suits ascending (diamonds before clubs), ranks ascending within a suit.
    std::vector<std::pair<int, int>> const expected{
        {Diamonds, 3}, {Diamonds, 5}, {Clubs, 4}, {Clubs, 6}, {Clubs, 7}};
    EXPECT_EQ(suit_rank_pairs(pool.cards), expected);
    EXPECT_EQ(pool.fixed_seat_count, 2);  // East holds D3, C4
}

TEST(DefenderSplitTest, MidTrickEndingWithOneDefenderAlreadyPlayedGivesAsymmetricCounts)
{
    // A diamond is on the trick in progress, outside both defenders'
    // remainCards. East holds two diamonds and West three: not the 2-2 or 3-3
    // that half the pool size would give.
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

// --- matches node.cpp's per-suit pool, cross-checked -----------------------

TEST(DefenderSplitTest, PerSuitPoolMatchesTheUnionOfBothDefendersRemainCardsDirectly)
{
    // Cross-checks defender_pool_decomposition's flattened cards against the
    // per-suit union that node.cpp's unexported defender_pool() computes,
    // derived here independently rather than by calling it.
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

// --- mid-trick handled, count read from the root not halved ----------------

TEST(DefenderSplitTest, FixedSeatCountIsReadFromRootPopcountNotHalfThePoolSize)
{
    Deal root{};
    root.trump = DDS_NOTRUMP;
    root.first = North;
    root.currentTrickSuit[0] = Clubs;
    root.currentTrickRank[0] = 9;
    root.remainCards[East][Clubs] = holding({2});         // one card
    root.remainCards[West][Clubs] = holding({3, 4, 5});    // three

    DefenderPool const pool = defender_pool_decomposition(root, North);

    EXPECT_EQ(pool.cards.size(), 4u);
    EXPECT_EQ(pool.fixed_seat_count, 1);  // East; half of 4 would wrongly give 2
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

// --- binomial_coefficient, against an independently hand-built triangle ---

TEST(BinomialCoefficientTest, MatchesAHandWrittenPascalsTriangleForEveryNAndK)
{
    // Built without binomial_coefficient: the test applies the addition rule
    // itself.
    constexpr int MaxN = 26;
    std::array<std::array<std::uint64_t, MaxN + 1>, MaxN + 1> triangle{};
    for (int n = 0; n <= MaxN; ++n)
    {
        triangle[n][0] = 1;
        for (int k = 1; k <= n; ++k)
        {
            triangle[n][k] = triangle[n - 1][k - 1] + (k <= n - 1 ? triangle[n - 1][k] : 0);
        }
    }

    for (int n = 0; n <= MaxN; ++n)
    {
        for (int k = 0; k <= n; ++k)
        {
            EXPECT_EQ(binomial_coefficient(n, k), triangle[n][k]) << "n=" << n << " k=" << k;
        }
    }
}

TEST(BinomialCoefficientTest, HandDerivedValuesIncludingTheBridgeSizedBound)
{
    EXPECT_EQ(binomial_coefficient(0, 0), 1u);
    EXPECT_EQ(binomial_coefficient(5, 0), 1u);
    EXPECT_EQ(binomial_coefficient(5, 5), 1u);
    EXPECT_EQ(binomial_coefficient(5, 2), 10u);
    EXPECT_EQ(binomial_coefficient(10, 5), 252u);
    EXPECT_EQ(binomial_coefficient(26, 13), 10400600u);
}

TEST(BinomialCoefficientTest, KOutOfRangeIsZeroNotAnError)
{
    EXPECT_EQ(binomial_coefficient(5, -1), 0u);
    EXPECT_EQ(binomial_coefficient(5, 6), 0u);
}

TEST(BinomialCoefficientTest, NOutOfRangeIsAnAssertedCallerErrorNotAnOutOfBoundsRead)
{
    // n must stay within [0, 26]: thirteen tricks leave at most 26 cards
    // between the two defenders. The Pascal's triangle table is sized to
    // exactly that, so any other n indexes outside the table.
    // EXPECT_DEBUG_DEATH, not EXPECT_DEATH: see the sibling assertion in
    // ExhaustiveLayoutSourceTest.
    EXPECT_DEBUG_DEATH({ binomial_coefficient(27, 0); }, "");
    EXPECT_DEBUG_DEATH({ binomial_coefficient(-1, 0); }, "");
}

// --- unrank_combination, exhaustive bijection -------------------------------

namespace
{
    /// Every k-subset of {0, ..., n-1} that `unrank_combination` produces over
    /// all of [0, C(n, k)), as a set of sets. This proves coverage of the
    /// whole range, not just that consecutive indices differ.
    auto all_unranked_subsets(int n, int k) -> std::set<std::set<int>>
    {
        std::set<std::set<int>> subsets;
        std::uint64_t const total = binomial_coefficient(n, k);
        for (std::uint64_t index = 0; index < total; ++index)
        {
            std::vector<int> const subset = unrank_combination(index, n, k);
            subsets.insert(std::set<int>(subset.begin(), subset.end()));
        }
        return subsets;
    }
}  // namespace

TEST(UnrankCombinationTest, IsABijectionOntoTheKSubsetsOfATenElementDomain)
{
    // C(10, 5) = 252: large enough to exercise the enumeration, small enough
    // to check every index.
    int const n = 10;
    int const k = 5;
    std::uint64_t const total = binomial_coefficient(n, k);
    ASSERT_EQ(total, 252u);

    std::set<std::set<int>> const subsets = all_unranked_subsets(n, k);
    EXPECT_EQ(subsets.size(), total);  // every index produced a distinct subset
}

TEST(UnrankCombinationTest, IsABijectionOnAnAsymmetricPair)
{
    int const n = 9;
    int const k = 2;
    std::uint64_t const total = binomial_coefficient(n, k);
    ASSERT_EQ(total, 36u);

    std::set<std::set<int>> const subsets = all_unranked_subsets(n, k);
    EXPECT_EQ(subsets.size(), total);
}

TEST(UnrankCombinationTest, EveryReturnedSubsetHasExactlyKDistinctInRangeElements)
{
    int const n = 7;
    int const k = 3;
    std::uint64_t const total = binomial_coefficient(n, k);
    for (std::uint64_t index = 0; index < total; ++index)
    {
        std::vector<int> const subset = unrank_combination(index, n, k);
        ASSERT_EQ(subset.size(), static_cast<std::size_t>(k)) << "index=" << index;
        std::set<int> const distinct(subset.begin(), subset.end());
        EXPECT_EQ(distinct.size(), subset.size()) << "index=" << index;  // no repeats
        for (int element : subset)
        {
            EXPECT_GE(element, 0) << "index=" << index;
            EXPECT_LT(element, n) << "index=" << index;
        }
    }
}

// --- unrank_combination: hand-derived indices (a mutation test's target) --

TEST(UnrankCombinationTest, HandDerivedIndicesOnTheFiveChooseTwoDomain)
{
    // Worked out by hand in colex order (see the header for the
    // construction). For C(5, 2), colex gives {0,1}, {0,2}, {1,2}, {0,3},
    // {1,3}, {2,3}, {0,4}, {1,4}, {2,4}, {3,4} for indices 0 to 9. The test
    // checks the first index, the last, and index 3 in between.
    EXPECT_EQ(unrank_combination(0, 5, 2), (std::vector<int>{0, 1}));
    EXPECT_EQ(unrank_combination(9, 5, 2), (std::vector<int>{3, 4}));  // last index, C(5,2) - 1
    EXPECT_EQ(unrank_combination(3, 5, 2), (std::vector<int>{0, 3}));  // an interior index
}

// --- edge cases -------------------------------------------------------------

TEST(UnrankCombinationTest, KEqualsZeroGivesTheEmptySubsetForTheOnlyValidIndex)
{
    EXPECT_TRUE(unrank_combination(0, 5, 0).empty());
    EXPECT_EQ(binomial_coefficient(5, 0), 1u);
}

TEST(UnrankCombinationTest, KEqualsNGivesTheWholeDomainForTheOnlyValidIndex)
{
    EXPECT_EQ(unrank_combination(0, 4, 4), (std::vector<int>{0, 1, 2, 3}));
    EXPECT_EQ(binomial_coefficient(4, 4), 1u);
}

TEST(UnrankCombinationTest, IsAPureFunctionSameIndexSameSubsetEveryTime)
{
    std::vector<int> const first = unrank_combination(17, 9, 4);
    std::vector<int> const second = unrank_combination(17, 9, 4);
    EXPECT_EQ(first, second);
}

// --- apply_defender_split ---------------------------------------------------

namespace
{
    /// Every field Deal carries, compared directly: the whole Deal, not just
    /// the two defenders' remainCards.
    auto deals_are_field_identical(Deal const& a, Deal const& b) -> bool
    {
        if (a.trump != b.trump || a.first != b.first)
        {
            return false;
        }
        for (int i = 0; i < 3; ++i)
        {
            if (a.currentTrickSuit[i] != b.currentTrickSuit[i] || a.currentTrickRank[i] != b.currentTrickRank[i])
            {
                return false;
            }
        }
        for (int hand = 0; hand < DDS_HANDS; ++hand)
        {
            for (int suit = 0; suit < DDS_SUITS; ++suit)
            {
                if (a.remainCards[hand][suit] != b.remainCards[hand][suit])
                {
                    return false;
                }
            }
        }
        return true;
    }

    /// The indices into pool.cards that root's defender split gives the fixed
    /// seat. The round-trip test below feeds these back into
    /// apply_defender_split. They are read from root directly, without the
    /// production code under test.
    auto own_split_indices(Deal const& root, int declarer, DefenderPool const& pool) -> std::vector<int>
    {
        int const fixed_seat = (declarer + 1) % DDS_HANDS;
        std::vector<int> indices;
        for (int i = 0; i < static_cast<int>(pool.cards.size()); ++i)
        {
            be::Card const& card = pool.cards[static_cast<std::size_t>(i)];
            if ((root.remainCards[fixed_seat][card.suit] & (1u << card.rank)) != 0)
            {
                indices.push_back(i);
            }
        }
        return indices;
    }

    /// A five-card pool (diamonds and clubs), small enough to try all
    /// C(5, 2) = 10 splits. trump, first and the current-trick fields differ
    /// from their defaults, so the identity checks cannot pass by
    /// coincidence.
    auto make_two_suit_pool_root() -> Deal
    {
        Deal root{};
        root.trump = Hearts;
        root.first = West;
        root.currentTrickSuit[0] = Spades;
        root.currentTrickRank[0] = 9;
        root.remainCards[North][Spades] = holding({14, 13});  // declarer
        root.remainCards[South][Hearts] = holding({14, 13});  // dummy
        root.remainCards[East][Diamonds] = holding({3});
        root.remainCards[East][Clubs] = holding({4});
        root.remainCards[West][Diamonds] = holding({5});
        root.remainCards[West][Clubs] = holding({6, 7});
        return root;
    }
}  // namespace

TEST(ApplyDefenderSplitTest, TheRootsOwnSplitRoundTripsToTheRootLayoutFieldForField)
{
    Deal const root = make_two_suit_pool_root();
    DefenderPool const pool = defender_pool_decomposition(root, North);

    std::vector<int> const own_split = own_split_indices(root, North, pool);
    ASSERT_EQ(own_split.size(), static_cast<std::size_t>(pool.fixed_seat_count));

    Deal const round_tripped = apply_defender_split(root, North, pool, own_split);
    EXPECT_TRUE(deals_are_field_identical(round_tripped, root));
}

TEST(ApplyDefenderSplitTest, AnOutOfRangeIndexIsAnAssertedCallerErrorNotAnOutOfBoundsWrite)
{
    // fixed_seat_cards comes from the caller, and this function trusts it for
    // legality (see apply_defender_split). Trusting it must not mean writing
    // wherever a bad index points. A negative and a too-large index are both
    // asserted with EXPECT_DEBUG_DEATH, not EXPECT_DEATH (see the sibling
    // assertion in ExhaustiveLayoutSourceTest). In a build without assert,
    // the guard stops the out-of-bounds write.
    Deal const root = make_two_suit_pool_root();
    DefenderPool const pool = defender_pool_decomposition(root, North);
    EXPECT_DEBUG_DEATH({ apply_defender_split(root, North, pool, {-1}); }, "");
    EXPECT_DEBUG_DEATH(
        { apply_defender_split(root, North, pool, {static_cast<int>(pool.cards.size())}); }, "");
}

TEST(ApplyDefenderSplitTest, EveryResultOverTheWholeSpaceIsConsistentWithTheRoot)
{
    Deal const root = make_two_suit_pool_root();
    int const dummy = South;
    DefenderPool const pool = defender_pool_decomposition(root, North);
    int const n = static_cast<int>(pool.cards.size());
    int const k = pool.fixed_seat_count;
    std::uint64_t const total = binomial_coefficient(n, k);
    ASSERT_EQ(total, 10u);  // C(5, 2)

    for (std::uint64_t index = 0; index < total; ++index)
    {
        std::vector<int> const subset = unrank_combination(index, n, k);
        Deal const result = apply_defender_split(root, North, pool, subset);
        EXPECT_TRUE(is_consistent(result, root, North, dummy)) << "index=" << index;
    }
}

TEST(ApplyDefenderSplitTest, EveryResultOverTheWholeSpaceIsALegalPosition)
{
    // Separate from the is_consistent check above: is_consistent never
    // compares hand sizes (see DefenderPool). This checks exact counts and
    // disjointness against what defender_pool_decomposition read from the
    // root, not against the root read again. So a bug in the decomposition
    // shows here instead of being hidden by reusing its answer.
    Deal const root = make_two_suit_pool_root();
    DefenderPool const pool = defender_pool_decomposition(root, North);
    int const n = static_cast<int>(pool.cards.size());
    int const k = pool.fixed_seat_count;
    std::uint64_t const total = binomial_coefficient(n, k);

    for (std::uint64_t index = 0; index < total; ++index)
    {
        std::vector<int> const subset = unrank_combination(index, n, k);
        Deal const result = apply_defender_split(root, North, pool, subset);

        int fixed_seat_count = 0;
        int other_seat_count = 0;
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            fixed_seat_count += std::popcount(result.remainCards[East][suit]);
            other_seat_count += std::popcount(result.remainCards[West][suit]);
            // Disjoint: no bit set in both defenders' holdings for this suit.
            EXPECT_EQ(result.remainCards[East][suit] & result.remainCards[West][suit], 0u) << "index=" << index;
        }
        EXPECT_EQ(fixed_seat_count, pool.fixed_seat_count) << "index=" << index;
        EXPECT_EQ(fixed_seat_count + other_seat_count, static_cast<int>(pool.cards.size())) << "index=" << index;
    }
}

TEST(ApplyDefenderSplitTest, UntouchedFieldsAreByteIdenticalToRootEvenWhenTheSplitDiffersFromRoots)
{
    Deal const root = make_two_suit_pool_root();
    DefenderPool const pool = defender_pool_decomposition(root, North);

    // A split different from root's: all of the pool to West.
    Deal const result = apply_defender_split(root, North, pool, /*fixed_seat_cards=*/{});

    EXPECT_EQ(result.trump, root.trump);
    EXPECT_EQ(result.first, root.first);
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_EQ(result.currentTrickSuit[i], root.currentTrickSuit[i]);
        EXPECT_EQ(result.currentTrickRank[i], root.currentTrickRank[i]);
    }
    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        EXPECT_EQ(result.remainCards[North][suit], root.remainCards[North][suit]);
        EXPECT_EQ(result.remainCards[South][suit], root.remainCards[South][suit]);
    }
}

TEST(ApplyDefenderSplitTest, HandlesTheLargestPoolTheDomainEverAllows)
{
    // 26 pooled cards, all the diamonds to East and all the clubs to West.
    // This is the largest pool possible: thirteen tricks leave at most 26
    // cards between the two defenders. A fixed-size internal buffer must hold
    // exactly this many without overrunning.
    Deal root{};
    root.trump = DDS_NOTRUMP;
    root.first = North;
    root.remainCards[North][Spades] = holding({14});
    root.remainCards[South][Hearts] = holding({14});
    unsigned full_suit = 0;
    for (int rank = 2; rank <= 14; ++rank)
    {
        full_suit |= holding({rank});
    }
    root.remainCards[East][Diamonds] = full_suit;
    root.remainCards[West][Clubs] = full_suit;

    DefenderPool const pool = defender_pool_decomposition(root, North);
    ASSERT_EQ(pool.cards.size(), 26u);
    ASSERT_EQ(pool.fixed_seat_count, 13);

    // Canonical order is suits ascending. With no spades or hearts in the
    // pool, diamonds fill indices 0 to 12 and clubs 13 to 25. So East's split
    // (all the diamonds) is exactly indices 0 to 12.
    std::vector<int> east_indices;
    for (int i = 0; i < 13; ++i)
    {
        east_indices.push_back(i);
    }

    Deal const result = apply_defender_split(root, North, pool, east_indices);
    EXPECT_TRUE(is_consistent(result, root, North, South));
    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        EXPECT_EQ(result.remainCards[East][suit], root.remainCards[East][suit]) << "suit " << suit;
        EXPECT_EQ(result.remainCards[West][suit], root.remainCards[West][suit]) << "suit " << suit;
    }
}
