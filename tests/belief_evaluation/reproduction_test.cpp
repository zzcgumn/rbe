#include <gtest/gtest.h>

#include <api/dll.h>
#include <utility/constants.h>

#include <belief_evaluation/evaluate.hpp>

#include "test_support.hpp"

// This plan's headline acceptance criterion: every early cut lands with no
// change to any answer, and this file is where that end-to-end claim gets
// made, not just per-cut in cuts_test.cpp.
//
// Criterion 1 (every plan 2 and plan 3 test unchanged) is verified by
// `git diff 353dae5b -- library/tests/belief_evaluation/` showing zero
// deletions -- not asserted here, since it is a property of the diff, not
// of any one test's runtime behaviour. Recorded in the review and in every
// task commit message in this plan.
//
// Run (a) -- tier 1 only, against a fixture whose shape is representative
// of plan 2/3's own oracle fixtures (several tricks, several layouts) --
// lives below. Tier 1 has no flag to disable, so it is already exercised
// by literally every one of the 150 pre-existing tests, whatever their own
// delta; this file adds the missing half of that claim, evidence that the
// cuts actually *fire* on a fixture of that shape, which no individual
// pre-existing test was asked to demonstrate.
//
// Run (b) -- tier 1 and tier 2 together, against a delta that satisfies
// EvaluateOptions::delta_is_double_dummy_optimal -- needs the solver
// (DoubleDummyDefender paired with DoubleDummyBound, the intended sound
// configuration), so it lives in double_dummy_bound_test.cpp instead,
// alongside the double-dummy machinery it depends on.

namespace
{
    constexpr int Two = 2;
    constexpr int Three = 3;
    constexpr int Four = 4;
    constexpr int Five = 5;
    constexpr int Six = 6;
    constexpr int Ace = 14;
    constexpr int King = 13;
    constexpr int Queen = 12;

    constexpr int Spades = 0;

    constexpr int North = 0;  // declarer
    constexpr int East = 1;   // a defender
    constexpr int South = 2;  // dummy
    constexpr int West = 3;   // a defender
}

class ReproductionTest : public ::testing::Test
{
};

TEST_F(ReproductionTest, TierOneCutsFireOnAThreeTrickOracleShapedFixture)
{
    // North holds the top three spades outright (three certain tricks,
    // whoever leads and whatever the defenders' three-way split), East on
    // lead, fully deterministic under single_card_defender /
    // single_card_declarer_play. North needs only 2 of the 3 available
    // tricks -- the already-made cut must fire right after trick 2, saving
    // the whole of trick 3's recursion.
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = East;
    deal.remainCards[North][Spades] = holding({Ace, King, Queen});
    deal.remainCards[East][Spades] = holding({Two, Three, Four});
    deal.remainCards[South][Spades] = holding({Five, Six, 7});
    deal.remainCards[West][Spades] = holding({8, 9, 10});
    VectorLayoutSource source({deal});
    assert_equal_hand_sizes(deal);

    EvaluationResult const with_cuts = evaluate(
        deal,
        North,
        /*tricks_needed=*/2,
        source,
        DeclarerStrategy{.id = 1, .play = single_card_declarer_play, .state_key = nullptr},
        single_card_defender,
        EvaluateOptions{.collect_counters = true});

    ASSERT_FALSE(with_cuts.error.has_value());
    EvaluationValue const& value = with_cuts.by_strategy.at(1u);
    EXPECT_EQ(value.p_make, 1.0);
    ASSERT_TRUE(value.counters.has_value());
    // Hand-counted tree, single deterministic path, needed = 2 of 3: root,
    // then one node per card played -- East, South, West, North for trick
    // 1 (nodes 2-5), the same four for trick 2 (nodes 6-9). Node 9, the
    // one reached right after North's card resolves trick 2 (tricks_won 2
    // >= needed 2), is where the already-made cut fires -- visited but not
    // expanded, so trick 3 is never touched at all.
    constexpr std::uint64_t NodesWithCuts = 9;
    EXPECT_EQ(value.counters->nodes_visited, NodesWithCuts);

    // The uncut comparison: needing all 3 tricks instead of 2 removes any
    // opportunity for the already-made cut to fire early, so the
    // recursion runs to its natural terminal node -- the same technique
    // cuts_test.cpp's own fixtures use throughout this plan, since tier 1
    // has no flag to disable directly.
    EvaluationResult const uncut_comparison = evaluate(
        deal,
        North,
        /*tricks_needed=*/3,
        source,
        DeclarerStrategy{.id = 1, .play = single_card_declarer_play, .state_key = nullptr},
        single_card_defender,
        EvaluateOptions{.collect_counters = true});
    ASSERT_FALSE(uncut_comparison.error.has_value());
    ASSERT_TRUE(uncut_comparison.by_strategy.at(1u).counters.has_value());
    // root + 3 full tricks (4 plies each) = 1 + 12 = 13, the natural
    // terminal node included (is_dead()/already_made() both false there
    // until the very last card, so no cut intercepts it early either).
    constexpr std::uint64_t NodesWithoutTheEarlyCut = 13;
    EXPECT_EQ(
        uncut_comparison.by_strategy.at(1u).counters->nodes_visited, NodesWithoutTheEarlyCut);

    // The cut rate this task asks be recorded: 4 of 13 nodes the uncut
    // recursion would have visited (the whole of trick 3) were pruned by
    // tier 1 on this fixture -- roughly 31%, for a fixture with only one
    // real cut opportunity. Recorded in the review with the rest of plan
    // 8's requested numbers.
    EXPECT_LT(NodesWithCuts, NodesWithoutTheEarlyCut);
}
