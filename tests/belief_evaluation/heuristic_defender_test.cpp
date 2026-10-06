#include <gtest/gtest.h>

#include <algorithm>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>
#include <api/solve_board.hpp>
#include <solver_context/solver_context.hpp>

#include <belief_evaluation/default_defender_heuristics.hpp>
#include <belief_evaluation/defender_heuristic.hpp>
#include <belief_evaluation/defender_heuristic_chain.hpp>
#include <belief_evaluation/double_dummy_defender.hpp>
#include <belief_evaluation/fourth_seat_low.hpp>
#include <belief_evaluation/heuristic_defender.hpp>
#include <belief_evaluation/high_in_third.hpp>
#include <belief_evaluation/second_seat_low.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;

namespace
{
    constexpr int Two = 2;
    constexpr int Three = 3;
    constexpr int Four = 4;
    constexpr int Five = 5;
    constexpr int King = 13;
    constexpr int Ace = 14;

    constexpr int Spades = 0;
    constexpr int Hearts = 1;
    constexpr int Clubs = 3;
    constexpr int NoTrump = DDS_NOTRUMP;

    constexpr int North = 0;
    constexpr int East = 1;
    constexpr int South = 2;
    constexpr int West = 3;
}

class HeuristicDefenderTest : public ::testing::Test
{
};

// --- one TEST_F per built-in rule, against a real solve_board call -------

TEST_F(HeuristicDefenderTest, SecondSeatLowFiresEndToEnd)
{
    // West leads a spade (already played, consumed from West's hand);
    // North, second to act, holds the suit's only other remaining card --
    // no choice at all, so the exact rule that produces it is moot here
    // (already distinguished against hand-built contexts in its own
    // task); this proves the pipeline -- real solve, real context
    // construction, real chain -- reaches and returns it correctly.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = West;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Ace;
    deal.remainCards[North][Spades] = be::holding({Two});
    deal.remainCards[East][Spades] = be::holding({Three});
    deal.remainCards[South][Spades] = be::holding({Four});

    be::ObservationState state{};
    state.declarer = East;

    SolverContext ctx;
    be::HeuristicDefender defender(ctx, be::make_default_defender_heuristics(NoTrump));
    std::vector<be::WeightedCard> const distribution =
        defender.as_strategy()(be::DefenderQuery{deal, North, state});

    ASSERT_EQ(distribution.size(), 1u);
    EXPECT_EQ(distribution[0].card.rank, Two);
    EXPECT_DOUBLE_EQ(distribution[0].probability, 1.0);
}

TEST_F(HeuristicDefenderTest, ThirdSeatGateFiresEndToEndWhenDummysCardIsBeatable)
{
    // South leads, West (dummy) follows -- both already played and
    // consumed. North, third to act, holds the suit's only remaining low
    // card: forced, so this exercises the shared third-seat gate
    // (high_in_third / third_seat_low), not which of the two specifically
    // answers -- already distinguished against hand-built contexts in
    // defender_heuristic_test.cpp.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = South;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.currentTrickSuit[1] = Spades;
    deal.currentTrickRank[1] = Four;
    deal.remainCards[North][Spades] = be::holding({Two});
    deal.remainCards[East][Spades] = be::holding({Ace});

    be::ObservationState state{};
    state.declarer = East;

    SolverContext ctx;
    be::HeuristicDefender defender(ctx, be::make_default_defender_heuristics(NoTrump));
    std::vector<be::WeightedCard> const distribution =
        defender.as_strategy()(be::DefenderQuery{deal, North, state});

    ASSERT_EQ(distribution.size(), 1u);
    EXPECT_EQ(distribution[0].card.rank, Two);
    EXPECT_DOUBLE_EQ(distribution[0].probability, 1.0);
}

TEST_F(HeuristicDefenderTest, FourthSeatLowFiresEndToEnd)
{
    // East leads, South and West already played and consumed. North,
    // fourth to act, holds the suit's only remaining card.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = East;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.currentTrickSuit[1] = Hearts;
    deal.currentTrickRank[1] = Four;
    deal.currentTrickSuit[2] = Hearts;
    deal.currentTrickRank[2] = Three;
    deal.remainCards[North][Spades] = be::holding({Two});

    be::ObservationState state{};
    state.declarer = East;

    SolverContext ctx;
    be::HeuristicDefender defender(ctx, be::make_default_defender_heuristics(NoTrump));
    std::vector<be::WeightedCard> const distribution =
        defender.as_strategy()(be::DefenderQuery{deal, North, state});

    ASSERT_EQ(distribution.size(), 1u);
    EXPECT_EQ(distribution[0].card.rank, Two);
    EXPECT_DOUBLE_EQ(distribution[0].probability, 1.0);
}

TEST_F(HeuristicDefenderTest, RuffSmallFiresEndToEnd)
{
    // Hearts is trump. West leads a spade (consumed). North, void in
    // spades, holds exactly one heart -- a forced ruff, its only legal
    // card. East (not yet played) holds a spade, so cannot overruff.
    Deal deal{};
    deal.trump = Hearts;
    deal.first = West;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Ace;
    deal.remainCards[North][Hearts] = be::holding({Two});
    deal.remainCards[East][Spades] = be::holding({Three});
    deal.remainCards[South][Spades] = be::holding({Four});  // also follows suit; balances hand sizes

    be::ObservationState state{};
    state.declarer = East;

    SolverContext ctx;
    be::HeuristicDefender defender(ctx, be::make_default_defender_heuristics(Hearts));
    std::vector<be::WeightedCard> const distribution =
        defender.as_strategy()(be::DefenderQuery{deal, North, state});

    ASSERT_EQ(distribution.size(), 1u);
    EXPECT_EQ(distribution[0].card.suit, Hearts);
    EXPECT_EQ(distribution[0].card.rank, Two);
    EXPECT_DOUBLE_EQ(distribution[0].probability, 1.0);
}

TEST_F(HeuristicDefenderTest, DiscardKeepWinnersFiresEndToEnd)
{
    // No trump. West leads a heart (consumed). North, void in hearts,
    // holds exactly one club -- a forced discard, its only legal card.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = West;
    deal.currentTrickSuit[0] = Hearts;
    deal.currentTrickRank[0] = Ace;
    deal.remainCards[North][Clubs] = be::holding({Two});
    deal.remainCards[East][Hearts] = be::holding({Three});
    deal.remainCards[South][Hearts] = be::holding({Four});  // balances hand sizes

    be::ObservationState state{};
    state.declarer = East;

    SolverContext ctx;
    be::HeuristicDefender defender(ctx, be::make_default_defender_heuristics(NoTrump));
    std::vector<be::WeightedCard> const distribution =
        defender.as_strategy()(be::DefenderQuery{deal, North, state});

    ASSERT_EQ(distribution.size(), 1u);
    EXPECT_EQ(distribution[0].card.suit, Clubs);
    EXPECT_EQ(distribution[0].card.rank, Two);
    EXPECT_DOUBLE_EQ(distribution[0].probability, 1.0);
}

// --- fallback and composability --------------------------------------

TEST_F(HeuristicDefenderTest, FallsBackToSpreadWhenNoRuleFires)
{
    // The queried seat (East) is the declarer itself: every built-in
    // rule's own gate requires a defending seat (ruff_small aside, which
    // requires a trump contract -- absent here), so none fire and the
    // result must fall through to spread(), identical to
    // DoubleDummyDefender's own output for the same deal and policy.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = East;
    deal.remainCards[North][Spades] = be::holding({Ace});
    deal.remainCards[East][Spades] = be::holding({King});
    deal.remainCards[South][Spades] = be::holding({Three});
    deal.remainCards[West][Spades] = be::holding({Two});

    be::ObservationState state{};
    state.declarer = East;

    SolverContext ctx;
    be::HeuristicDefender heuristic_defender(ctx, be::make_default_defender_heuristics(NoTrump));
    std::vector<be::WeightedCard> const heuristic_result =
        heuristic_defender.as_strategy()(be::DefenderQuery{deal, East, state});

    be::DoubleDummyDefender double_dummy_defender(ctx);
    std::vector<be::WeightedCard> const double_dummy_result =
        double_dummy_defender.as_strategy()(be::DefenderQuery{deal, East, state});

    ASSERT_EQ(heuristic_result.size(), double_dummy_result.size());
    for (std::size_t i = 0; i < heuristic_result.size(); ++i)
    {
        EXPECT_EQ(heuristic_result[i].card.suit, double_dummy_result[i].card.suit);
        EXPECT_EQ(heuristic_result[i].card.rank, double_dummy_result[i].card.rank);
        EXPECT_DOUBLE_EQ(heuristic_result[i].probability, double_dummy_result[i].probability);
    }
}

TEST_F(HeuristicDefenderTest, ANonDefaultChainBehavesExactlyAsItsOwnSelectCardSays)
{
    // A hand-built chain, deliberately not the convenience one: two
    // built-ins in non-default order (high_in_third before
    // second_seat_low, the reverse of make_default_defender_heuristics'
    // own order), with a caller-supplied rule spliced between them.
    // North, second seat, holds two non-touching spades -- if
    // second_seat_low ran, it would pick the low one; the custom rule
    // instead picks the high one, so which card comes back is proof the
    // custom rule actually fired and won, not merely that it was
    // accepted without crashing.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = West;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[West][Hearts] = be::holding({Two});   // West's one card left, having led its spade
    deal.remainCards[North][Spades] = be::holding({Two, King});
    deal.remainCards[East][Spades] = be::holding({Four});
    deal.remainCards[East][Hearts] = be::holding({Three});
    deal.remainCards[South][Hearts] = be::holding({Four, Five});

    be::ObservationState state{};
    state.declarer = East;

    bool custom_rule_called = false;
    be::DefenderHeuristicChain chain;
    chain.add(be::high_in_third());  // position_in_trick == 1 here: defers
    chain.add([&custom_rule_called](
                   be::DefenderHeuristicContext const& ctx,
                   std::vector<be::Card> const& best_cards) -> std::optional<be::Card> {
        custom_rule_called = true;
        auto const highest = std::ranges::max_element(
            best_cards, [](be::Card const& a, be::Card const& b) { return a.rank < b.rank; });
        return ctx.position_in_trick == 1 && highest != best_cards.end() ? std::optional(*highest)
                                                                          : std::nullopt;
    });
    chain.add(be::second_seat_low());  // never reached: the custom rule above already fired

    SolverContext ctx;
    be::HeuristicDefender defender(ctx, chain);
    std::vector<be::WeightedCard> const result =
        defender.as_strategy()(be::DefenderQuery{deal, North, state});

    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].card.rank, King);  // the custom rule's own answer, not second_seat_low's
    EXPECT_DOUBLE_EQ(result[0].probability, 1.0);
    EXPECT_TRUE(custom_rule_called);

    // The composability guarantee itself: HeuristicDefender's own answer
    // must equal calling this exact chain's select_card() directly
    // against the same solved position -- proving it has no special
    // notion of "the default chain" to diverge from, not merely that a
    // custom rule is accepted.
    FutureTricks fut{};
    ASSERT_EQ(solve_board(ctx, deal, /*target=*/-1, /*solutions=*/2, /*mode=*/0, &fut), RETURN_NO_FAULT);
    std::vector<be::Card> best_cards;
    for (int i = 0; i < fut.cards; ++i)
    {
        best_cards.push_back(be::Card{fut.suit[i], fut.rank[i]});
    }
    be::DefenderHeuristicContext const direct_ctx =
        be::make_defender_heuristic_context(deal, state, North, fut);
    std::optional<be::Card> const direct_result = chain.select_card(direct_ctx, best_cards);

    ASSERT_TRUE(direct_result.has_value());
    EXPECT_EQ(direct_result->suit, result[0].card.suit);
    EXPECT_EQ(direct_result->rank, result[0].card.rank);
}

TEST_F(HeuristicDefenderTest, ANonZeroSolveBoardStatusReturnsAnEmptyDistribution)
{
    // The same malformed deal (the ace of spades held by two hands at
    // once) double_dummy_defender_test.cpp uses for the same assertion:
    // solve_board rejects it outright, and HeuristicDefender returns an
    // empty distribution rather than inventing a second error path.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = North;
    deal.remainCards[North][Spades] = be::holding({Ace});
    deal.remainCards[East][Spades] = be::holding({Ace});  // duplicate
    deal.remainCards[South][Spades] = be::holding({Three});
    deal.remainCards[West][Spades] = be::holding({Four});

    be::ObservationState state{};
    state.declarer = East;

    SolverContext ctx;
    be::HeuristicDefender defender(ctx, be::make_default_defender_heuristics(NoTrump));
    std::vector<be::WeightedCard> const distribution =
        defender.as_strategy()(be::DefenderQuery{deal, North, state});

    EXPECT_TRUE(distribution.empty());
}
