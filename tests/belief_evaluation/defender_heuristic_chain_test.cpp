#include <gtest/gtest.h>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/defender_heuristic.hpp>
#include <belief_evaluation/defender_heuristic_chain.hpp>
#include <belief_evaluation/second_seat_low.hpp>
#include <belief_evaluation/types.hpp>

#include "test_support.hpp"

// Tests for how DefenderHeuristicChain composes rules: order, deferral
// and caller-supplied rules.

namespace be = dds::belief_evaluation;

namespace
{
    constexpr int Two = 2;
    constexpr int Queen = 12;
    constexpr int Ace = 14;
    constexpr int King = 13;
    constexpr int Spades = 0;
    constexpr int North = 0;
    constexpr int East = 1;
    constexpr int West = 3;

    // A minimal, fixed context. Every stub rule here is unconditional, so no
    // test reads it. This file tests how the chain composes rules; each rule's
    // bridge condition is tested where the rule is defined.
    auto make_fixed_context(be::ObservationState const& state, FutureTricks const& fut)
        -> be::DefenderHeuristicContext
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[East][Spades] = be::holding({Ace});
        return be::make_defender_heuristic_context(deal, state, East, fut);
    }
}

class DefenderHeuristicChainTest : public ::testing::Test
{
};

TEST_F(DefenderHeuristicChainTest, FirstNonNulloptWins)
{
    be::ObservationState state{};
    FutureTricks fut{};
    be::DefenderHeuristicContext const ctx = make_fixed_context(state, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Ace}};

    int third_calls = 0;
    int fourth_calls = 0;

    be::DefenderHeuristicChain chain;
    chain.add([](be::DefenderHeuristicContext const&, std::vector<be::Card> const&) -> std::optional<be::Card> { return std::nullopt; });
    chain.add([](be::DefenderHeuristicContext const&, std::vector<be::Card> const&) -> std::optional<be::Card> { return std::nullopt; });
    chain.add([&third_calls](be::DefenderHeuristicContext const&, std::vector<be::Card> const&) {
        ++third_calls;
        return std::optional<be::Card>{be::Card{Spades, King}};
    });
    chain.add([&fourth_calls](be::DefenderHeuristicContext const&, std::vector<be::Card> const&) {
        ++fourth_calls;
        return std::optional<be::Card>{be::Card{Spades, Ace}};
    });

    std::optional<be::Card> const result = chain.select_card(ctx, best_cards);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->rank, King);
    EXPECT_EQ(third_calls, 1);
    EXPECT_EQ(fourth_calls, 0);  // never reached once the third rule fires
}

TEST_F(DefenderHeuristicChainTest, AllDeferReturnsNullopt)
{
    be::ObservationState state{};
    FutureTricks fut{};
    be::DefenderHeuristicContext const ctx = make_fixed_context(state, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Ace}};

    be::DefenderHeuristicChain chain;
    chain.add([](be::DefenderHeuristicContext const&, std::vector<be::Card> const&) -> std::optional<be::Card> { return std::nullopt; });
    chain.add([](be::DefenderHeuristicContext const&, std::vector<be::Card> const&) -> std::optional<be::Card> { return std::nullopt; });

    EXPECT_FALSE(chain.select_card(ctx, best_cards).has_value());
}

TEST_F(DefenderHeuristicChainTest, ReorderingChangesTheResult)
{
    be::ObservationState state{};
    FutureTricks fut{};
    be::DefenderHeuristicContext const ctx = make_fixed_context(state, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Ace}};

    auto const rule_a = [](be::DefenderHeuristicContext const&, std::vector<be::Card> const&) {
        return std::optional<be::Card>{be::Card{Spades, Ace}};
    };
    auto const rule_b = [](be::DefenderHeuristicContext const&, std::vector<be::Card> const&) {
        return std::optional<be::Card>{be::Card{Spades, King}};
    };

    be::DefenderHeuristicChain chain_ab;
    chain_ab.add(rule_a);
    chain_ab.add(rule_b);
    EXPECT_EQ(chain_ab.select_card(ctx, best_cards)->rank, Ace);

    be::DefenderHeuristicChain chain_ba;
    chain_ba.add(rule_b);
    chain_ba.add(rule_a);
    EXPECT_EQ(chain_ba.select_card(ctx, best_cards)->rank, King);
}

TEST_F(DefenderHeuristicChainTest, OmittingARuleRemovesItsEffectEntirely)
{
    be::ObservationState state{};
    FutureTricks fut{};
    be::DefenderHeuristicContext const ctx = make_fixed_context(state, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Ace}};

    int rule_a_calls = 0;
    int rule_b_calls = 0;
    auto const rule_a = [&rule_a_calls](be::DefenderHeuristicContext const&, std::vector<be::Card> const&) {
        ++rule_a_calls;
        return std::optional<be::Card>{be::Card{Spades, Ace}};
    };
    auto const rule_b = [&rule_b_calls](be::DefenderHeuristicContext const&, std::vector<be::Card> const&) {
        ++rule_b_calls;
        return std::optional<be::Card>{be::Card{Spades, King}};
    };

    // Only rule_a is add()-ed. rule_b would fire here if it were in the
    // chain.
    be::DefenderHeuristicChain chain;
    chain.add(rule_a);

    std::optional<be::Card> const result = chain.select_card(ctx, best_cards);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->rank, Ace);
    EXPECT_EQ(rule_a_calls, 1);
    EXPECT_EQ(rule_b_calls, 0);
    (void)rule_b;
}

TEST_F(DefenderHeuristicChainTest, CallerSuppliedRuleInterleavedWithBuiltinsFiresInItsPlace)
{
    be::ObservationState state{};
    FutureTricks fut{};
    be::DefenderHeuristicContext const ctx = make_fixed_context(state, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Ace}};

    bool caller_rule_should_fire = true;
    int caller_rule_calls = 0;
    int rule_b_calls = 0;

    auto const rule_a_defers =
        [](be::DefenderHeuristicContext const&, std::vector<be::Card> const&) -> std::optional<be::Card> { return std::nullopt; };
    auto const caller_rule = [&](be::DefenderHeuristicContext const&, std::vector<be::Card> const&) {
        ++caller_rule_calls;
        if (caller_rule_should_fire) {
            return std::optional<be::Card>{be::Card{Spades, King}};
        }
        return std::optional<be::Card>{};
    };
    auto const rule_b = [&rule_b_calls](be::DefenderHeuristicContext const&, std::vector<be::Card> const&) {
        ++rule_b_calls;
        return std::optional<be::Card>{be::Card{Spades, Ace}};
    };

    be::DefenderHeuristicChain chain;
    chain.add(rule_a_defers);
    chain.add(caller_rule);
    chain.add(rule_b);

    // The caller's rule fires: its card wins, and rule_b is never reached.
    caller_rule_should_fire = true;
    std::optional<be::Card> const fired = chain.select_card(ctx, best_cards);
    ASSERT_TRUE(fired.has_value());
    EXPECT_EQ(fired->rank, King);
    EXPECT_EQ(caller_rule_calls, 1);
    EXPECT_EQ(rule_b_calls, 0);

    // The caller's rule defers: it was still tried, but rule_b's card wins.
    caller_rule_should_fire = false;
    std::optional<be::Card> const deferred = chain.select_card(ctx, best_cards);
    ASSERT_TRUE(deferred.has_value());
    EXPECT_EQ(deferred->rank, Ace);
    EXPECT_EQ(caller_rule_calls, 2);
    EXPECT_EQ(rule_b_calls, 1);
}

TEST_F(DefenderHeuristicChainTest, ALaterRuleAtTheSameDecisionPointDefeatsSecondSeatLowsDeferral)
{
    // second_seat_low(true) documents a caller obligation that the chain
    // cannot enforce: put second_seat_low last among rules that can fire at
    // the same point. select_card() has one nullopt, meaning "try the next
    // rule" (see FirstNonNulloptWins and AllDeferReturnsNullopt above). So
    // second_seat_low's "defer so the fallback spread randomises this pair"
    // looks the same as "I don't apply". This test pins the result: in a
    // chain that breaks the obligation, a later rule that fires in second
    // seat silently wins over the deferral. The stub stands for any such
    // rule, built in or supplied by the caller.
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = West;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Two;
    deal.remainCards[North][Spades] = be::holding({Queen, Ace});
    be::ObservationState state{};
    state.declarer = East;

    // The touching-pair shape from second_seat_low's
    // DefersWhenTheLowestTouchesAnotherCandidateAndRandomizingIsOn test: the
    // queen (the rule's lowest pick) touches the ace. The deferral is meant
    // for the chain's fallback spread, which randomises between the two, not
    // for a later rule in the chain.
    FutureTricks fut{};
    fut.cards = 2;
    fut.suit[0] = Spades;
    fut.rank[0] = Queen;
    fut.equals[0] = 0;
    fut.suit[1] = Spades;
    fut.rank[1] = Ace;
    fut.equals[1] = 1 << Queen;

    be::DefenderHeuristicContext const ctx = be::make_defender_heuristic_context(deal, state, North, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Queen}, be::Card{Spades, Ace}};

    int later_rule_calls = 0;
    auto const later_rule_at_the_same_decision_point =
        [&later_rule_calls](be::DefenderHeuristicContext const& rule_ctx, std::vector<be::Card> const&) {
            ++later_rule_calls;
            EXPECT_EQ(rule_ctx.position_in_trick, 1) << "fires at the exact decision point second_seat_low does";
            return std::optional<be::Card>{be::Card{Spades, King}};
        };

    be::DefenderHeuristicChain chain;
    chain.add(be::second_seat_low(/*randomize_touching_honors=*/true));
    chain.add(later_rule_at_the_same_decision_point);

    std::optional<be::Card> const result = chain.select_card(ctx, best_cards);

    // The documented, unenforced behaviour: the later rule's card wins
    // outright, and the deferral never reaches the fallback spread.
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->rank, King);
    EXPECT_EQ(later_rule_calls, 1);
}
