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

// Tests for HeuristicDefender end to end against real solve_board calls:
// each built-in rule, the fallback, caller rules and solver failures.

namespace be = dds::belief_evaluation;

namespace
{
    constexpr int Two = 2;
    constexpr int Three = 3;
    constexpr int Four = 4;
    constexpr int Five = 5;
    constexpr int Nine = 9;
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
    // West has led a spade. North, second to act, holds the suit's only
    // other remaining card, so there is no choice, and which rule produces it
    // does not matter. defender_heuristic_test.cpp tells the rules apart with
    // hand-built contexts. This shows the pipeline (real solve, real context,
    // real chain) reaches the card and returns it.
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
    // South leads and West (dummy) follows. North, third to act, holds the
    // suit's only remaining low card, so the play is forced. This exercises
    // the shared third-seat gate (high_in_third and third_seat_low), not which
    // of the two answers; defender_heuristic_test.cpp tells them apart.
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
    // East leads, and South and West have played. North, fourth to act,
    // holds the suit's only remaining card.
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
    // Hearts are trumps. West leads a spade. North, void in spades, holds one
    // heart: a forced ruff, its only legal card. East has not played yet and
    // holds a spade, so it cannot overruff.
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
    // Notrump. West leads a heart. North, void in hearts, holds one club: a
    // forced discard, its only legal card.
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
    // The queried seat (East) is declarer. Every built-in rule needs a
    // defending seat, and ruff_small also needs a trump contract, which this
    // is not. So no rule fires, and the result falls through to spread(). It
    // must equal DoubleDummyDefender's output for the same deal and policy.
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

TEST_F(HeuristicDefenderTest, FallsBackToSpreadWhenAChainRuleReturnsACardOutsideTheOptimalSet)
{
    // A chain is a sequence of plain callables that the caller assembles; no
    // interface limits what a rule may return. A faulty caller rule might
    // always return a card the solve never reported: not held by the queried
    // seat, and not double-dummy-optimal. That card must never reach the
    // caller. The spec says no rule adds a card outside the solved set, and
    // that must hold even when a rule tries to.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = West;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[West][Hearts] = be::holding({Two});  // West's one card left, having led its spade
    deal.remainCards[North][Spades] = be::holding({Two, King});
    deal.remainCards[East][Spades] = be::holding({Four});
    deal.remainCards[East][Hearts] = be::holding({Three});
    deal.remainCards[South][Hearts] = be::holding({Four, Five});

    be::ObservationState state{};
    state.declarer = East;

    be::DefenderHeuristicChain chain;
    chain.add([](be::DefenderHeuristicContext const&, std::vector<be::Card> const&) -> std::optional<be::Card> {
        return be::Card{Spades, Nine};  // North holds no such card; not a solved candidate either
    });

    SolverContext ctx;
    be::HeuristicDefender heuristic_defender(ctx, chain);
    std::vector<be::WeightedCard> const heuristic_result =
        heuristic_defender.as_strategy()(be::DefenderQuery{deal, North, state});

    be::DoubleDummyDefender double_dummy_defender(ctx);
    std::vector<be::WeightedCard> const double_dummy_result =
        double_dummy_defender.as_strategy()(be::DefenderQuery{deal, North, state});

    ASSERT_EQ(heuristic_result.size(), double_dummy_result.size());
    for (std::size_t i = 0; i < heuristic_result.size(); ++i)
    {
        EXPECT_EQ(heuristic_result[i].card.suit, double_dummy_result[i].card.suit);
        EXPECT_EQ(heuristic_result[i].card.rank, double_dummy_result[i].card.rank);
        EXPECT_DOUBLE_EQ(heuristic_result[i].probability, double_dummy_result[i].probability);
    }
}

TEST_F(HeuristicDefenderTest, FallsBackToSpreadWhenAChainRuleReturnsAnOutOfRangeRank)
{
    // Nothing stops a caller rule from returning a rank outside 2..14
    // (Python's Card constructor accepts any int). The validation above must
    // reject it before it shifts equals's bits, not only when the rank looks
    // like a card. The test tries a negative and an oversized rank: either is
    // undefined behaviour for an unchecked `1u << rank`.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = West;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[West][Hearts] = be::holding({Two});
    deal.remainCards[North][Spades] = be::holding({Two, King});
    deal.remainCards[East][Spades] = be::holding({Four});
    deal.remainCards[East][Hearts] = be::holding({Three});
    deal.remainCards[South][Hearts] = be::holding({Four, Five});

    be::ObservationState state{};
    state.declarer = East;

    SolverContext ctx;
    be::DoubleDummyDefender double_dummy_defender(ctx);
    std::vector<be::WeightedCard> const double_dummy_result =
        double_dummy_defender.as_strategy()(be::DefenderQuery{deal, North, state});

    for (int const bad_rank : {-1, 32, 1000})
    {
        be::DefenderHeuristicChain chain;
        chain.add([bad_rank](be::DefenderHeuristicContext const&,
                              std::vector<be::Card> const&) -> std::optional<be::Card> {
            return be::Card{Spades, bad_rank};
        });

        be::HeuristicDefender heuristic_defender(ctx, chain);
        std::vector<be::WeightedCard> const heuristic_result =
            heuristic_defender.as_strategy()(be::DefenderQuery{deal, North, state});

        ASSERT_EQ(heuristic_result.size(), double_dummy_result.size());
        for (std::size_t i = 0; i < heuristic_result.size(); ++i)
        {
            EXPECT_EQ(heuristic_result[i].card.suit, double_dummy_result[i].card.suit);
            EXPECT_EQ(heuristic_result[i].card.rank, double_dummy_result[i].card.rank);
            EXPECT_DOUBLE_EQ(heuristic_result[i].probability, double_dummy_result[i].probability);
        }
    }
}

TEST_F(HeuristicDefenderTest, ANonDefaultChainBehavesExactlyAsItsOwnSelectCardSays)
{
    // A hand-built chain, not the default one: two built-ins in non-default
    // order (high_in_third before second_seat_low, the reverse of
    // make_default_defender_heuristics), with a caller rule between them.
    // North, second seat, holds two spades that do not touch. second_seat_low
    // would pick the low one, and the custom rule picks the high one. So the
    // returned card proves the custom rule fired and won, not just that it
    // was accepted.
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
    EXPECT_EQ(result[0].card.rank, King);  // the custom rule's answer, not second_seat_low's
    EXPECT_DOUBLE_EQ(result[0].probability, 1.0);
    EXPECT_TRUE(custom_rule_called);

    // The composability guarantee: HeuristicDefender's answer must equal this
    // chain's select_card() called directly on the same solved position. So
    // HeuristicDefender has no special "default chain" to diverge from.
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
    // The malformed deal (the ace of spades in two hands) from
    // double_dummy_defender_test.cpp's matching test. solve_board rejects it,
    // and HeuristicDefender returns an empty distribution instead of adding a
    // second error path.
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
