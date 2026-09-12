#include <gtest/gtest.h>

#include <array>
#include <utility>
#include <vector>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

#include <belief_evaluation/constrained_decomposition.hpp>
#include <belief_evaluation/defender_split.hpp>
#include <belief_evaluation/types.hpp>

namespace be = dds::belief_evaluation;

using be::binomial_coefficient;
using be::constrained_space_size;
using be::ConstrainedDecomposition;
using be::ConstrainedSpaceStatus;
using be::decompose_constrained;
using be::DefenderPool;

namespace
{
    constexpr int Spades = 0;
    constexpr int Hearts = 1;
    constexpr int Diamonds = 2;
    constexpr int Clubs = 3;

    /// No voids at all -- every suit false.
    auto no_voids() -> std::array<bool, DDS_SUITS>
    {
        return {};
    }

    auto void_in(std::initializer_list<int> suits) -> std::array<bool, DDS_SUITS>
    {
        std::array<bool, DDS_SUITS> result{};
        for (int suit : suits)
        {
            result[static_cast<std::size_t>(suit)] = true;
        }
        return result;
    }

    auto pool_of(std::vector<be::Card> const& cards, int fixed_seat_count) -> DefenderPool
    {
        DefenderPool pool;
        pool.cards = cards;
        pool.fixed_seat_count = fixed_seat_count;
        return pool;
    }

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
}

class ConstrainedDecompositionTest : public ::testing::Test
{
};

// --- no voids reproduces the existing unconstrained binomial exactly --------

TEST_F(ConstrainedDecompositionTest, NoVoidsPutsEveryCardInFreeAndMatchesPlan6aExactly)
{
    DefenderPool const pool = pool_of(
        {{Diamonds, 3}, {Diamonds, 5}, {Diamonds, 7}, {Clubs, 4}, {Clubs, 6}}, 2);

    ConstrainedDecomposition const decomposition = decompose_constrained(pool, no_voids(), no_voids());

    EXPECT_EQ(decomposition.status, ConstrainedSpaceStatus::Ok);
    EXPECT_TRUE(decomposition.forced_to_fixed_seat.empty());
    EXPECT_TRUE(decomposition.forced_to_other_seat.empty());
    EXPECT_EQ(suit_rank_pairs(decomposition.free_cards), suit_rank_pairs(pool.cards));
    EXPECT_EQ(decomposition.fixed_seat_needed, 2);

    // Not merely equal in value -- the same call, since with nothing forced
    // free_cards.size() == pool.cards.size() and fixed_seat_needed ==
    // pool.fixed_seat_count.
    EXPECT_EQ(
        constrained_space_size(decomposition),
        binomial_coefficient(
            static_cast<int>(pool.cards.size()), pool.fixed_seat_count));
    EXPECT_EQ(constrained_space_size(decomposition), 10u);  // C(5, 2)
}

// --- hand-derived size(), asserted in full, four different shapes -----------

TEST_F(ConstrainedDecompositionTest, OneDefenderVoidInOneSuitForcesThatSuitsWholePoolAway)
{
    // Fixed seat void in clubs: both club cards are forced to the other
    // defender: the diamonds are all that is left free.
    DefenderPool const pool = pool_of(
        {{Diamonds, 3}, {Diamonds, 5}, {Diamonds, 7}, {Clubs, 4}, {Clubs, 6}}, 2);

    ConstrainedDecomposition const decomposition =
        decompose_constrained(pool, void_in({Clubs}), no_voids());

    EXPECT_EQ(decomposition.status, ConstrainedSpaceStatus::Ok);
    EXPECT_TRUE(decomposition.forced_to_fixed_seat.empty());
    EXPECT_EQ(
        suit_rank_pairs(decomposition.forced_to_other_seat),
        (std::vector<std::pair<int, int>>{{Clubs, 4}, {Clubs, 6}}));
    EXPECT_EQ(
        suit_rank_pairs(decomposition.free_cards),
        (std::vector<std::pair<int, int>>{{Diamonds, 3}, {Diamonds, 5}, {Diamonds, 7}}));
    EXPECT_EQ(decomposition.fixed_seat_needed, 2);  // nothing forced to the fixed seat yet
    EXPECT_EQ(constrained_space_size(decomposition), 3u);  // C(3, 2)
}

TEST_F(ConstrainedDecompositionTest, BothDefendersVoidInDifferentSuitsForcesBothWays)
{
    // Fixed seat void in hearts (forced away); other defender void in
    // clubs (forced to the fixed seat); diamonds are the only free suit.
    DefenderPool const pool = pool_of(
        {{Diamonds, 3}, {Diamonds, 5}, {Hearts, 8}, {Hearts, 9}, {Clubs, 4}, {Clubs, 6}}, 3);

    ConstrainedDecomposition const decomposition =
        decompose_constrained(pool, void_in({Hearts}), void_in({Clubs}));

    EXPECT_EQ(decomposition.status, ConstrainedSpaceStatus::Ok);
    EXPECT_EQ(
        suit_rank_pairs(decomposition.forced_to_fixed_seat),
        (std::vector<std::pair<int, int>>{{Clubs, 4}, {Clubs, 6}}));
    EXPECT_EQ(
        suit_rank_pairs(decomposition.forced_to_other_seat),
        (std::vector<std::pair<int, int>>{{Hearts, 8}, {Hearts, 9}}));
    EXPECT_EQ(
        suit_rank_pairs(decomposition.free_cards),
        (std::vector<std::pair<int, int>>{{Diamonds, 3}, {Diamonds, 5}}));
    EXPECT_EQ(decomposition.fixed_seat_needed, 1);  // 3 needed, 2 already forced to it
    EXPECT_EQ(constrained_space_size(decomposition), 2u);  // C(2, 1)
}

TEST_F(ConstrainedDecompositionTest, AVoidCoveringTheWholePoolLeavesExactlyOneLayout)
{
    // The pool is entirely one suit, and the fixed seat is void in it --
    // and, consistently, holds none of it at the root. Every pool card is
    // forced to the other defender, nothing is free, and exactly one
    // assignment satisfies both hand sizes: C(0, 0) == 1.
    DefenderPool const pool = pool_of({{Diamonds, 3}, {Diamonds, 5}, {Diamonds, 7}, {Diamonds, 9}}, 0);

    ConstrainedDecomposition const decomposition =
        decompose_constrained(pool, void_in({Diamonds}), no_voids());

    EXPECT_EQ(decomposition.status, ConstrainedSpaceStatus::Ok);
    EXPECT_TRUE(decomposition.forced_to_fixed_seat.empty());
    EXPECT_EQ(suit_rank_pairs(decomposition.forced_to_other_seat), suit_rank_pairs(pool.cards));
    EXPECT_TRUE(decomposition.free_cards.empty());
    EXPECT_EQ(decomposition.fixed_seat_needed, 0);
    EXPECT_EQ(constrained_space_size(decomposition), 1u);  // C(0, 0)
}

// --- the three empty-space causes, distinguished ----------------------------

TEST_F(ConstrainedDecompositionTest, BothDefendersVoidInTheSameSuitIsContradictory)
{
    DefenderPool const pool = pool_of({{Diamonds, 3}, {Diamonds, 5}}, 1);

    ConstrainedDecomposition const decomposition =
        decompose_constrained(pool, void_in({Diamonds}), void_in({Diamonds}));

    EXPECT_EQ(decomposition.status, ConstrainedSpaceStatus::ContradictoryVoid);
    EXPECT_TRUE(decomposition.forced_to_fixed_seat.empty());
    EXPECT_TRUE(decomposition.forced_to_other_seat.empty());
    EXPECT_TRUE(decomposition.free_cards.empty());
    EXPECT_EQ(decomposition.fixed_seat_needed, 0);
    EXPECT_EQ(constrained_space_size(decomposition), 0u);
}

TEST_F(ConstrainedDecompositionTest, MoreCardsForcedToTheFixedSeatThanItHoldsIsRejected)
{
    // The other defender is void in diamonds, forcing all three diamonds
    // to the fixed seat -- but the fixed seat holds only one card at the
    // root.
    DefenderPool const pool = pool_of({{Diamonds, 3}, {Diamonds, 5}, {Diamonds, 7}}, 1);

    ConstrainedDecomposition const decomposition =
        decompose_constrained(pool, no_voids(), void_in({Diamonds}));

    EXPECT_EQ(decomposition.status, ConstrainedSpaceStatus::ForcedExceedsFixedSeatCount);
    EXPECT_EQ(suit_rank_pairs(decomposition.forced_to_fixed_seat), suit_rank_pairs(pool.cards));
    EXPECT_TRUE(decomposition.forced_to_other_seat.empty());
    EXPECT_TRUE(decomposition.free_cards.empty());
    EXPECT_EQ(decomposition.fixed_seat_needed, -2);  // 1 needed minus 3 already forced
    EXPECT_EQ(constrained_space_size(decomposition), 0u);
}

TEST_F(ConstrainedDecompositionTest, TheFixedSeatCannotReachItsHandSizeFromWhatIsLeftIsRejected)
{
    // Nothing is forced either way, but the fixed seat's own hand size at
    // the root (4) exceeds the whole pool (2 free cards) -- a shape a real
    // history-versus-root mismatch could produce, exercised here directly
    // since this fixture only exercises this function in isolation.
    DefenderPool const pool = pool_of({{Diamonds, 3}, {Diamonds, 5}}, 4);

    ConstrainedDecomposition const decomposition = decompose_constrained(pool, no_voids(), no_voids());

    EXPECT_EQ(decomposition.status, ConstrainedSpaceStatus::InsufficientFreeCards);
    EXPECT_TRUE(decomposition.forced_to_fixed_seat.empty());
    EXPECT_TRUE(decomposition.forced_to_other_seat.empty());
    EXPECT_EQ(suit_rank_pairs(decomposition.free_cards), suit_rank_pairs(pool.cards));
    EXPECT_EQ(decomposition.fixed_seat_needed, 4);
    EXPECT_EQ(constrained_space_size(decomposition), 0u);
}

// --- the free order is stable across calls -----------------------------------

TEST_F(ConstrainedDecompositionTest, FreeCardOrderIsStableAcrossRepeatedCalls)
{
    DefenderPool const pool = pool_of(
        {{Spades, 2}, {Diamonds, 3}, {Diamonds, 9}, {Clubs, 10}, {Clubs, 11}}, 3);

    ConstrainedDecomposition const first = decompose_constrained(pool, no_voids(), no_voids());
    ConstrainedDecomposition const second = decompose_constrained(pool, no_voids(), no_voids());

    EXPECT_EQ(suit_rank_pairs(first.free_cards), suit_rank_pairs(second.free_cards));
    // And it is exactly pool.cards' own order, not merely internally
    // consistent between the two calls -- the order an index-to-subset
    // unranking over the free cards will need to stay deterministic.
    EXPECT_EQ(suit_rank_pairs(first.free_cards), suit_rank_pairs(pool.cards));
}
