#include <gtest/gtest.h>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/defender_heuristic.hpp>
#include <belief_evaluation/defender_heuristic_chain.hpp>
#include <belief_evaluation/types.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;

namespace
{
    constexpr int Ace = 14;
    constexpr int King = 13;
    constexpr int Spades = 0;
    constexpr int East = 1;

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
