#include <gtest/gtest.h>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/defender_heuristic.hpp>
#include <belief_evaluation/defender_heuristic_chain.hpp>
#include <belief_evaluation/second_seat_low.hpp>
#include <belief_evaluation/types.hpp>

#include "test_support.hpp"

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

    // A minimal, fixed context -- no test in this file needs the chain to
    // actually read it, since every stub rule here is unconditional (the
    // whole point: this file tests the chain's own composition logic, not
    // any rule's bridge-domain condition, which is tested where each rule
    // is defined instead).
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

    // Only rule_a is ever add()-ed; rule_b would fire here if it were, but
    // it is never given the chance.
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

    // The caller's own rule fires: its card wins, rule_b is never reached.
    caller_rule_should_fire = true;
    std::optional<be::Card> const fired = chain.select_card(ctx, best_cards);
    ASSERT_TRUE(fired.has_value());
    EXPECT_EQ(fired->rank, King);
    EXPECT_EQ(caller_rule_calls, 1);
    EXPECT_EQ(rule_b_calls, 0);

    // The caller's own rule defers: it was still tried, but rule_b's card
    // wins.
    caller_rule_should_fire = false;
    std::optional<be::Card> const deferred = chain.select_card(ctx, best_cards);
    ASSERT_TRUE(deferred.has_value());
    EXPECT_EQ(deferred->rank, Ace);
    EXPECT_EQ(caller_rule_calls, 2);
    EXPECT_EQ(rule_b_calls, 1);
}

TEST_F(DefenderHeuristicChainTest, ALaterRuleAtTheSameDecisionPointDefeatsSecondSeatLowsDeferral)
{
    // second_seat_low(true)'s own doxygen documents a caller obligation
    // this type cannot enforce: "Put second_seat_low last among any rules
    // that could fire at the same decision point when
    // randomize_touching_honors is relied upon." The reason is structural,
    // not a bug in second_seat_low itself -- select_card()'s nullopt is
    // single-valued (see FirstNonNulloptWins/AllDeferReturnsNullopt
    // above): there is no way for second_seat_low's "defer specifically so
    // the fallback spread randomises this pair" to be distinguished from
    // an ordinary "I don't apply, try the next rule". This test pins that
    // documented-but-previously-unverified behaviour down as a known,
    // tested property: a chain built in the one order the composability
    // guarantee (Composability points 1 and 3 of this plan) explicitly
    // licenses a caller to build, but which second_seat_low's own doxygen
    // warns against, silently loses the deferral to whatever fires next --
    // here, a stub standing in for any later rule or caller-supplied
    // heuristic that also fires in second seat.
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = West;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Two;
    deal.remainCards[North][Spades] = be::holding({Queen, Ace});
    be::ObservationState state{};
    state.declarer = East;

    // The same touching-pair shape second_seat_low's own
    // DefersWhenTheLowestTouchesAnotherCandidateAndRandomizingIsOn test
    // uses: the queen (this rule's own "lowest" pick) touches the ace.
    // Deferring here is meant to hand the decision to the chain's
    // fallback spread so it randomises between the two -- not to any
    // later rule in the same chain.
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

    // today's actual, documented-but-unenforced behaviour: the later
    // rule's card wins outright, not the deferral second_seat_low meant to
    // reach the fallback spread with.
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->rank, King);
    EXPECT_EQ(later_rule_calls, 1);
}
