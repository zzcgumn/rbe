#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>
#include <api/solve_board.hpp>
#include <solver_context/solver_context.hpp>

#include <belief_evaluation/brute_force_declarer.hpp>
#include <belief_evaluation/double_dummy_bound.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/rank_map.hpp>
#include <belief_evaluation/trick.hpp>
#include <belief_evaluation/types.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;

namespace
{
    constexpr int Two = 2;
    constexpr int Three = 3;
    constexpr int Four = 4;
    constexpr int Five = 5;
    constexpr int Six = 6;
    constexpr int Seven = 7;
    constexpr int Eight = 8;
    constexpr int Nine = 9;
    constexpr int King = 13;

    constexpr int Spades = 0;
    constexpr int Clubs = 3;

    constexpr int North = 0;  // declarer
    constexpr int East = 1;
    constexpr int South = 2;  // dummy
    constexpr int West = 3;

    // North (declarer) on lead, one legal card -- enough to prove play()
    // returns a legal card and that state_key is wired up, without
    // depending on any search logic yet.
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

// --- terminal and depth-cutoff leaves -------------------------------------

TEST_F(BruteForceDeclarerTest, TerminalLeafUsesMassDirectlyForProbabilityToMake)
{
    be::BeliefNode node{};
    node.p = {0.3, 0.7};
    node.kappa = 1.0;
    node.state.tricks_needed = 2;
    node.state.tricks_won_by_declarer = 2;  // made, in every layout -- common knowledge at a terminal node

    EXPECT_DOUBLE_EQ(
        be::terminal_leaf_value(node, be::DeclarerObjective::MaximiseProbabilityToMake), 1.0);
}

TEST_F(BruteForceDeclarerTest, TerminalLeafGivesTrickCountForExpectedTricks)
{
    be::BeliefNode node{};
    node.p = {0.3, 0.7};
    node.kappa = 1.0;
    node.state.tricks_won_by_declarer = 5;

    EXPECT_DOUBLE_EQ(
        be::terminal_leaf_value(node, be::DeclarerObjective::MaximiseExpectedTricks), 5.0);
}

TEST_F(BruteForceDeclarerTest, CutoffLeafUsesPerLayoutBoundsNotANodeWideNumber)
{
    // North (declarer) holds King, Eight, Five of spades. In deal_a, East
    // alone holds the one dangerous spade (the Nine) and is forced to
    // spend it on North's first-round King lead, going void before
    // North's Eight is ever at risk (the same mechanism
    // suit_top_tricks_test.cpp's own
    // UndercountsWhenTheShorterOpponentHoldsTheOnlyDangerousCard fixture
    // describes). In deal_b, the Nine moves to the *longer* defending hand
    // (West, holding Nine and Two), which can duck the first round with
    // its Two and keep the Nine in reserve to beat North's Eight on the
    // second round instead. Same North/South spade holdings, a different
    // defender split: the two deals' own double-dummy bounds are read off
    // the solver directly below, not asserted by hand, and need only
    // differ from each other for this test to exercise per-layout
    // branching at all.
    // Every hand must hold the same total card count for solve_board to
    // report a real score rather than its own failure sentinel (measured
    // directly against this solver build, not assumed -- see
    // double_dummy_bound_test.cpp's own "filler suit" note); North's own
    // 3-card spade holding sets that count, so South/East/West are padded
    // with filler clubs nobody has a real choice over, identically in both
    // deals, so any filler-suit effect on the result is the same constant
    // in both and isolates the spades difference between them.
    Deal deal_a{};
    deal_a.trump = DDS_NOTRUMP;
    deal_a.first = North;
    deal_a.remainCards[North][Spades] = be::holding({King, Eight, Five});
    deal_a.remainCards[East][Spades] = be::holding({Nine});
    deal_a.remainCards[West][Spades] = be::holding({Two, Three});
    deal_a.remainCards[South][Clubs] = be::holding({Two, Three, Four});
    deal_a.remainCards[East][Clubs] = be::holding({Five, Six});
    deal_a.remainCards[West][Clubs] = be::holding({Seven});

    Deal deal_b{};
    deal_b.trump = DDS_NOTRUMP;
    deal_b.first = North;
    deal_b.remainCards[North][Spades] = be::holding({King, Eight, Five});
    deal_b.remainCards[East][Spades] = be::holding({Three});
    deal_b.remainCards[West][Spades] = be::holding({Nine, Two});
    deal_b.remainCards[South][Clubs] = be::holding({Two, Three, Four});
    deal_b.remainCards[East][Clubs] = be::holding({Five, Six});
    deal_b.remainCards[West][Clubs] = be::holding({Seven});

    SolverContext ctx;
    be::DoubleDummyBound bound(ctx, /*declarer=*/North);

    // Confirmed by a direct solve_board call -- not assumed from the
    // comment above -- per the per-layout bound each deal actually
    // yields.
    int const bound_a = bound.as_bound()(deal_a);
    int const bound_b = bound.as_bound()(deal_b);
    ASSERT_NE(bound_a, 14);  // not the solver-failure sentinel
    ASSERT_NE(bound_b, 14);
    ASSERT_NE(bound_a, bound_b)
        << "fixture must give the two layouts genuinely different bounds for this test to "
           "exercise per-layout branching at all";

    be::BeliefNode node{};
    node.layouts = {deal_a, deal_b};
    node.p = {0.4, 0.6};
    node.kappa = 1.0;
    node.state.declarer = North;
    node.state.tricks_won_by_declarer = 0;
    // The higher of the two bounds, so the layout with that bound makes
    // and the other does not -- a P(make) fixture that actually
    // discriminates between the two layouts, whatever their real bounds
    // turn out to be.
    node.state.tricks_needed = std::max(bound_a, bound_b);

    double const expected_expected_tricks = 0.4 * (0 + bound_a) + 0.6 * (0 + bound_b);
    EXPECT_DOUBLE_EQ(
        be::cutoff_leaf_value(node, bound.as_bound(), be::DeclarerObjective::MaximiseExpectedTricks),
        expected_expected_tricks);

    double const expected_p_make =
        0.4 * ((0 + bound_a) >= 3 ? 1.0 : 0.0) + 0.6 * ((0 + bound_b) >= 3 ? 1.0 : 0.0);
    EXPECT_DOUBLE_EQ(
        be::cutoff_leaf_value(node, bound.as_bound(), be::DeclarerObjective::MaximiseProbabilityToMake),
        expected_p_make);
}
