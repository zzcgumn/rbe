#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

#include <belief_evaluation/evaluate.hpp>

#include "test_support.hpp"

// A namespace alias plus targeted `using` declarations, not `using namespace
// dds::belief_evaluation;` -- this file includes api/dds_data_types.hpp
// directly (declaring global ::Card), so a `using namespace` here would make
// any future bare `Card` reference ambiguous between ::Card and
// dds::belief_evaluation::Card rather than a clear compile error naming
// which one was meant.
namespace be = dds::belief_evaluation;
using be::DeclarerStrategy;
using be::EvaluateOptions;
using be::EvaluationResult;
using be::EvaluationValue;
using be::VectorLayoutSource;
using be::assert_equal_hand_sizes;
using be::assert_forms_one_belief_node;
using be::assert_pool_matches;
using be::evaluate;
using be::holding;
using be::single_card_declarer_play;
using be::single_card_defender;

// The headline acceptance criterion for early cuts: every cut lands with
// no change to any answer, and this file is where that end-to-end claim
// gets made, not just per-cut in cuts_test.cpp.
//
// Every pre-existing test in this suite passing unchanged is verified by
// diffing this directory against the commit this work branched from and
// confirming zero deletions -- not asserted here, since it is a property
// of the diff, not of any one test's runtime behaviour. Recorded in the
// commit history for this work.
//
// Run (a) -- tier 1 only, against a fixture whose shape is representative
// of this suite's own oracle-style fixtures (several tricks, several
// layouts) -- lives below. Tier 1 has no flag to disable, so it is already
// exercised by literally every one of the 150 pre-existing tests, whatever
// their own delta; this file adds the missing half of that claim, evidence
// that the cuts actually *fire* on a fixture of that shape, which no
// individual pre-existing test was asked to demonstrate.
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
    constexpr int Seven = 7;
    constexpr int Eight = 8;
    constexpr int Nine = 9;
    constexpr int Ten = 10;
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
    // then one node per card played -- East, South, West, North for trick 1
    // (nodes 2-5). North's queen/king/ace always beats the defenders' low
    // cards, so North wins trick 1 and leads trick 2 -- North, East, South,
    // West this time (nodes 6-9). Node 9, the one reached right after
    // West's card resolves trick 2 (tricks_won 2 >= needed 2), is where the
    // already-made cut fires -- visited but not expanded, so trick 3 is
    // never touched at all.
    constexpr std::uint64_t NodesWithCuts = 9;
    EXPECT_EQ(value.counters->nodes_visited, NodesWithCuts);

    // The uncut comparison: needing all 3 tricks instead of 2 removes any
    // opportunity for the already-made cut to fire early, so the
    // recursion runs to its natural terminal node -- the same technique
    // cuts_test.cpp's own fixtures use throughout, since tier 1 has no flag
    // to disable directly.
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

    // The cut rate: 4 of 13 nodes the uncut recursion would have visited
    // (the whole of trick 3) were pruned by tier 1 on this fixture --
    // roughly 31%, for a fixture with only one real cut opportunity.
    EXPECT_LT(NodesWithCuts, NodesWithoutTheEarlyCut);
}

// Convergence toward the exhaustive answer as the sample size M rises, on a
// fixture whose exhaustive answer is neither 0 nor 1 -- required, since a
// fixture that always makes or always fails would satisfy "converges" by
// luck, proving nothing about M actually mattering.

namespace
{
    /// North holds a single middling card (Nine); South (dummy) holds a
    /// single safely-lower card (Eight, never the trick's highest);
    /// East and West share a seven-card pool {Two..Seven, Ten} -- six
    /// ranks below North's Nine and exactly one (Ten) above it. East on
    /// lead, forced (single_card_defender: its only card), West forced
    /// too (its own lowest remaining, which is always one of the six low
    /// ranks regardless of which one East took, since removing one low
    /// rank still leaves several lower than Ten in West's hand). Whoever
    /// holds Ten decides the trick: if East does, East's own led card
    /// (10) already exceeds North's Nine before anyone else plays, and
    /// nothing West or North holds beats it -- declarer's side loses this
    /// trick outright. If West holds Ten instead (every other `east_rank`
    /// choice), West's forced *lowest* card is never the Ten it happens to
    /// also hold, so nothing ever beats North's Nine -- declarer's side
    /// wins. Six of the seven pool assignments to East are winning, one is
    /// not: the true, hand-derived answer is 6/7, confirmed independently
    /// by the exhaustive run below rather than assumed.
    auto make_finesse_style_layout(int east_rank) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[North][Spades] = holding({Nine});
        deal.remainCards[South][Spades] = holding({Eight});
        deal.remainCards[East][Spades] = holding({east_rank});
        unsigned west_mask = 0;
        for (int rank : {Two, Three, Four, Five, Six, Seven, Ten})
        {
            if (rank != east_rank)
            {
                west_mask |= holding({rank});
            }
        }
        deal.remainCards[West][Spades] = west_mask;
        return deal;
    }
}

TEST_F(ReproductionTest, ConvergesTowardTheExhaustiveAnswerAsMRises)
{
    // Two identical rounds of the seven pool assignments, Ten last in
    // each round, for 14 layouts total -- larger than any fixture this
    // module has built so far, since a sample of 3 from a space of 4
    // shows nothing. The two rounds
    // being identical is deliberate: it keeps the true ratio exactly 6/7
    // at M = 7 as well as at M = 14, so the comparison below is against a
    // value pinned two independent ways, not just the one full run.
    std::vector<Deal> layouts;
    for (int round = 0; round < 2; ++round)
    {
        for (int rank : {Two, Three, Four, Five, Six, Seven, Ten})
        {
            layouts.push_back(make_finesse_style_layout(rank));
        }
    }
    assert_pool_matches(layouts);
    assert_forms_one_belief_node(layouts, North);
    ASSERT_EQ(layouts.size(), 14u);
    VectorLayoutSource const source(layouts);
    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};

    EvaluationResult const exhaustive =
        evaluate(layouts.front(), North, /*tricks_needed=*/1, source, pi, single_card_defender);
    ASSERT_FALSE(exhaustive.error.has_value());
    double const true_p_make = exhaustive.by_strategy.at(1u).p_make;
    // The hand-derived answer, confirmed rather than assumed: 6/7 was
    // reasoned out above from the fixture's own construction, not read
    // off this run and pasted back.
    EXPECT_NEAR(true_p_make, 6.0 / 7.0, 1e-9);

    // M = 6: source order puts Ten last in each round, so the first six
    // layouts scanned are exactly the six winning assignments -- the
    // sample has not yet seen the one losing case, and reports total
    // (false) certainty.
    EvaluationResult const m6 = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/1,
        source,
        pi,
        single_card_defender,
        EvaluateOptions{.sample_size = 6u});
    ASSERT_FALSE(m6.error.has_value());
    EXPECT_DOUBLE_EQ(m6.by_strategy.at(1u).p_make, 1.0);

    // M = 7: exactly one full round, so the sample now contains the one
    // losing assignment in the same proportion the whole 14-layout space
    // does -- the estimate lands on the true ratio, not merely closer to
    // it.
    EvaluationResult const m7 = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/1,
        source,
        pi,
        single_card_defender,
        EvaluateOptions{.sample_size = 7u});
    ASSERT_FALSE(m7.error.has_value());
    EXPECT_NEAR(m7.by_strategy.at(1u).p_make, true_p_make, 1e-9);

    // Convergence, stated as a distance that strictly decreases: M = 6's
    // estimate is 1/7 away from the truth (a false certainty caused by
    // missing the one rare bad case entirely); M = 7's is 0. This is the
    // number that matters for a future replenishment scheme to size
    // itself against -- how far down a small sample can be from the
    // truth, not just that a large enough one eventually matches it.
    double const error_at_six = std::abs(m6.by_strategy.at(1u).p_make - true_p_make);
    double const error_at_seven = std::abs(m7.by_strategy.at(1u).p_make - true_p_make);
    EXPECT_LT(error_at_seven, error_at_six);
}
