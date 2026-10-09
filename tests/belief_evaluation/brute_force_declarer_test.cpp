#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>
#include <solver_context/solver_context.hpp>

#include <belief_evaluation/brute_force_declarer.hpp>
#include <belief_evaluation/rank_map.hpp>
#include <belief_evaluation/trick.hpp>
#include <belief_evaluation/types.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;

namespace
{
    constexpr int Eight = 8;
    constexpr int Nine = 9;

    constexpr int Spades = 0;

    constexpr int North = 0;  // declarer
    constexpr int East = 1;
    constexpr int South = 2;  // dummy
    constexpr int West = 3;

    // North (declarer) on lead, one legal card -- enough to prove play()
    // returns a legal card and that state_key is wired up, without
    // depending on any search logic (that is Task 04's own job).
    auto make_one_card_root() -> be::ObservationState
    {
        Deal layout{};
        layout.trump = DDS_NOTRUMP;
        layout.first = North;
        layout.remainCards[North][Spades] = be::holding({Nine});
        layout.remainCards[South][Spades] = be::holding({Eight});
        layout.remainCards[East][Spades] = be::holding({2});
        layout.remainCards[West][Spades] = be::holding({3});

        be::ObservationState state{};
        state.trump = DDS_NOTRUMP;
        state.first = North;
        state.declarer = North;
        state.tricks_needed = 1;
        state.tricks_won_by_declarer = 0;
        state.known_holdings = layout;
        state.ranks = be::make_rank_map(layout);
        return state;
    }

    auto make_entries(std::vector<Deal> const& layouts, std::vector<be::Probability> const& p)
        -> std::vector<be::BeliefEntry>
    {
        std::vector<be::BeliefEntry> entries;
        entries.reserve(layouts.size());
        for (std::size_t i = 0; i < layouts.size(); ++i)
        {
            entries.push_back(be::BeliefEntry{layouts[i], p[i]});
        }
        return entries;
    }
}

class BruteForceDeclarerTest : public ::testing::Test
{
};

TEST_F(BruteForceDeclarerTest, ConstructionWithNoOpponentModelDoesNotThrow)
{
    SolverContext ctx;
    EXPECT_NO_THROW(be::BruteForceDeclarer declarer(ctx));
}

TEST_F(BruteForceDeclarerTest, ConstructionAcceptsACallerSuppliedOpponentModel)
{
    be::DefenderStrategy const spy = [](be::DefenderQuery const&) -> std::vector<be::WeightedCard>
    {
        return {{be::Card{0, 2}, 1.0}};
    };
    SolverContext ctx;
    EXPECT_NO_THROW(
        be::BruteForceDeclarer declarer(ctx, be::DeclarerObjective::MaximiseExpectedTricks, spy));
}

TEST_F(BruteForceDeclarerTest, PlayReturnsALegalCard)
{
    be::ObservationState const state = make_one_card_root();
    std::vector<Deal> const layouts = {state.known_holdings};
    std::vector<be::Probability> const p = {1.0};
    std::vector<be::BeliefEntry> const entries = make_entries(layouts, p);

    SolverContext ctx;
    be::BruteForceDeclarer declarer(ctx);
    be::DeclarerStrategy const pi = declarer.as_strategy();
    be::Card const card = pi.play(state, be::BeliefView{entries, false, entries.size()});

    auto const legal = be::enumerate_legal_cards(state.known_holdings, be::seat_on_play(state.known_holdings));
    EXPECT_NE(
        std::find_if(
            legal.begin(), legal.end(),
            [&](be::Card const& c) { return c.suit == card.suit && c.rank == card.rank; }),
        legal.end());
}

TEST_F(BruteForceDeclarerTest, StateKeyIsStableUnderPermutingTheBeliefSpace)
{
    be::ObservationState const state = make_one_card_root();
    Deal b = state.known_holdings;
    b.remainCards[East][Spades] = be::holding({3});
    b.remainCards[West][Spades] = be::holding({2});

    std::vector<Deal> const layouts = {state.known_holdings, b};
    std::vector<be::Probability> const p = {0.4, 0.6};
    std::vector<be::BeliefEntry> const entries = make_entries(layouts, p);

    std::vector<Deal> const permuted_layouts = {b, state.known_holdings};
    std::vector<be::Probability> const permuted_p = {0.6, 0.4};
    std::vector<be::BeliefEntry> const permuted_entries = make_entries(permuted_layouts, permuted_p);

    SolverContext ctx;
    be::BruteForceDeclarer declarer(ctx);
    be::DeclarerStrategy const pi = declarer.as_strategy();

    be::StateKey const key_1 = pi.state_key(state, be::BeliefView{entries, false, entries.size()});
    be::StateKey const key_2 =
        pi.state_key(state, be::BeliefView{permuted_entries, false, permuted_entries.size()});

    EXPECT_EQ(key_1, key_2);
}

TEST_F(BruteForceDeclarerTest, StateKeyDiffersWhenPosteriorsDiffer)
{
    be::ObservationState const state = make_one_card_root();
    std::vector<Deal> const layouts = {state.known_holdings};

    std::vector<be::BeliefEntry> const entries_1 = make_entries(layouts, {0.5});
    std::vector<be::BeliefEntry> const entries_2 = make_entries(layouts, {0.9});

    SolverContext ctx;
    be::BruteForceDeclarer declarer(ctx);
    be::DeclarerStrategy const pi = declarer.as_strategy();

    be::StateKey const key_1 = pi.state_key(state, be::BeliefView{entries_1, false, entries_1.size()});
    be::StateKey const key_2 = pi.state_key(state, be::BeliefView{entries_2, false, entries_2.size()});

    EXPECT_NE(key_1, key_2);
}
