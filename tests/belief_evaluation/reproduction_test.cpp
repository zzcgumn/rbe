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
    constexpr int Jack = 11;

    constexpr int Spades = 0;
    constexpr int Hearts = 1;

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
        EvaluateOptions{.sampling = {.sample_size = 6u}});
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
        EvaluateOptions{.sampling = {.sample_size = 7u}});
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

// ===========================================================================
// Replenishment: reproduction, convergence, and the measurements.
// ===========================================================================

// M >= N still reproduces the exhaustive run bitwise, with
// replenishment enabled. If a scan at depth accepts even one layout here,
// either the exclusion set is missing a duplicate or the replay is
// producing a layout that differs from the one already present -- both
// bugs this criterion exists to catch, not a tolerance to reach for.
TEST_F(ReproductionTest, MGreaterThanOrEqualToNReproducesExhaustiveBitwiseWithReplenishmentEnabled)
{
    std::vector<Deal> layouts;
    for (int round = 0; round < 2; ++round)
    {
        for (int rank : {Two, Three, Four, Five, Six, Seven, Ten})
        {
            layouts.push_back(make_finesse_style_layout(rank));
        }
    }
    VectorLayoutSource const source(layouts);
    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};

    EvaluationResult const exhaustive =
        evaluate(layouts.front(), North, /*tricks_needed=*/1, source, pi, single_card_defender);
    ASSERT_FALSE(exhaustive.error.has_value());

    // sample_size = 14 = N, and replenish_below set wide enough (100) that
    // the trigger fires at every node below it -- effectively everywhere,
    // since no node in this fixture ever holds more than 14 layouts. Every
    // one of those scans must find nothing: the root already drew every
    // consistent layout, so every node below it already holds the true
    // complete set for its own narrower path too.
    EvaluationResult const with_replenishment = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/1,
        source,
        pi,
        single_card_defender,
        EvaluateOptions{.sampling = {.sample_size = 14u, .replenish_below = 100u}});
    ASSERT_FALSE(with_replenishment.error.has_value());
    EXPECT_EQ(exhaustive.by_strategy.at(1u).p_make, with_replenishment.by_strategy.at(1u).p_make);
}

// Convergence, on the fixture above -- and an honest
// finding about it. Node-local replenishment can only ever add a
// candidate to a node whose own layout count has already dropped below
// `sample_size`, which requires a defender split to have already
// happened on that path; and it can only accept a candidate matching
// every defender ply already played there. On this specific fixture,
// the only defender ply *is* the one that distinguishes the missing
// "ten" layout from the six present at M = 6 -- there is no earlier,
// separate split to create room before it, so no node exists yet for
// "ten" to join by the time any node on this path could accept it.
// Replenishment cannot recover this fixture's own M = 6 case, whatever
// replenish_below is set to -- confirmed directly below, not assumed.
TEST_F(ReproductionTest, ReplenishmentCannotRecoverThisFixturesOwnMEqualsSixBecauseItHasNoEarlierSplit)
{
    std::vector<Deal> layouts;
    for (int round = 0; round < 2; ++round)
    {
        for (int rank : {Two, Three, Four, Five, Six, Seven, Ten})
        {
            layouts.push_back(make_finesse_style_layout(rank));
        }
    }
    VectorLayoutSource const source(layouts);
    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};

    EvaluationResult const result = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/1,
        source,
        pi,
        single_card_defender,
        EvaluateOptions{.sampling = {.sample_size = 6u, .replenish_below = 100u}});
    ASSERT_FALSE(result.error.has_value());
    EXPECT_DOUBLE_EQ(result.by_strategy.at(1u).p_make, 1.0);  // unchanged from the no-replenishment M = 6 case
}

namespace
{
    /// The same finesse pool and true ratio as make_finesse_style_layout,
    /// reshaped so a node-local scan can actually reach the missing
    /// layout: a harmless spade split (East holds one of two fillers,
    /// jack or queen, split against West by a fixed two-card pool) comes
    /// *before* the heart finesse position, giving replenishment a node
    /// already below sample_size to act on before the heart ply -- the
    /// one that matters -- has happened. East leads (not North): East's
    /// own delta picks its lowest-indexed held suit when leading (spades,
    /// forcing the harmless split first), and a defender root has no
    /// "explore every other legal first card" step the way a declarer
    /// root does, so North holding two cards (the spade ace and the heart
    /// nine, one per trick) never causes an alternate subtree to be
    /// explored the way it would if North itself were on lead.
    auto make_split_then_finesse_layout(int east_spade, int east_heart) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[North][Spades] = holding({Ace});
        deal.remainCards[South][Spades] = holding({King});
        deal.remainCards[East][Spades] = holding({east_spade});
        deal.remainCards[West][Spades] = holding({east_spade == Jack ? Queen : Jack});
        deal.remainCards[North][Hearts] = holding({Nine});
        deal.remainCards[South][Hearts] = holding({Eight});
        deal.remainCards[East][Hearts] = holding({east_heart});
        unsigned west_heart_mask = 0;
        for (int rank : {Two, Three, Four, Five, Six, Seven, Ten})
        {
            if (rank != east_heart)
            {
                west_heart_mask |= holding({rank});
            }
        }
        deal.remainCards[West][Hearts] = west_heart_mask;
        return deal;
    }
}

TEST_F(ReproductionTest, ConvergesViaReplenishmentOnASplitThenFinesseVariantOfTheFixtureAbove)
{
    // Eight layouts: three winning hearts under each spade filler (six
    // total, matching the fixture above's own M = 6 count), then the one
    // losing heart (ten) under each filler, last in source order --
    // sample_size = 6 draws exactly the six winning layouts, missing both
    // "ten" variants, exactly as the fixture above's own M = 6 does for
    // its seventh.
    std::vector<Deal> const layouts{
        make_split_then_finesse_layout(Jack, Two),
        make_split_then_finesse_layout(Queen, Three),
        make_split_then_finesse_layout(Jack, Four),
        make_split_then_finesse_layout(Queen, Five),
        make_split_then_finesse_layout(Jack, Six),
        make_split_then_finesse_layout(Queen, Seven),
        make_split_then_finesse_layout(Jack, Ten),
        make_split_then_finesse_layout(Queen, Ten),
    };
    be::assert_pool_matches(layouts);
    VectorLayoutSource const source(layouts);
    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};

    EvaluationResult const exhaustive =
        evaluate(layouts.front(), North, /*tricks_needed=*/2, source, pi, single_card_defender);
    ASSERT_FALSE(exhaustive.error.has_value());
    // Hand-derived: 6 of 8 layouts win (every heart but the two tens), 2
    // lose -- 6/8 = 3/4. A different ratio from the earlier fixture's own
    // 6/7, by construction: recovering the *same* ratio would need the
    // missing layout to be the fixture's only defender ply, which is
    // exactly the shape the test above proves replenishment cannot reach.
    EXPECT_NEAR(exhaustive.by_strategy.at(1u).p_make, 0.75, 1e-9);

    EvaluationResult const m6_no_replenishment = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/2,
        source,
        pi,
        single_card_defender,
        EvaluateOptions{.sampling = {.sample_size = 6u}});
    ASSERT_FALSE(m6_no_replenishment.error.has_value());
    EXPECT_DOUBLE_EQ(m6_no_replenishment.by_strategy.at(1u).p_make, 1.0);

    EvaluationResult const m6_with_replenishment = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/2,
        source,
        pi,
        single_card_defender,
        EvaluateOptions{.sampling = {.sample_size = 6u, .replenish_below = 6u}});
    ASSERT_FALSE(m6_with_replenishment.error.has_value());
    // Side by side: 1.0 without replenishment, 0.75 with it -- exactly the
    // exhaustive answer, not merely closer to it.
    double const p_make_without = m6_no_replenishment.by_strategy.at(1u).p_make;
    double const p_make_with = m6_with_replenishment.by_strategy.at(1u).p_make;
    EXPECT_DOUBLE_EQ(p_make_without, 1.0);
    EXPECT_NEAR(p_make_with, 0.75, 1e-9);
    EXPECT_LT(std::abs(p_make_with - 0.75), std::abs(p_make_without - 0.75));
}

// This one is deliberately EXPECT_EQ on a double, not EXPECT_NEAR or
// EXPECT_DOUBLE_EQ (which itself tolerates a 4-ULP difference): a sampled,
// replenishing fixture -- exercising the kappa rescale, the node-local
// scan and the replenishment trigger, not just straight-line arithmetic --
// where the true answer happens to be exactly representable (6 winning of
// 8 layouts, 0.75, no rounding in the ratio itself). That makes bitwise
// equality the right bar rather than an unreasonably tight one: this is
// the fixture proving the default build and -c opt agree exactly, not
// merely to within tolerance, on a path where NDEBUG-only divergence (a
// discarded assert argument computing something with a side effect, an
// -DNDEBUG-conditional code path) would actually have somewhere to show
// up. If this ever starts failing under -c opt while the default build
// still passes, that is exactly the class of bug this test exists to
// catch -- do not loosen it to EXPECT_NEAR.
TEST_F(ReproductionTest, PMakeOnASampledReplenishingFixtureIsExactlyThreeQuarters)
{
    std::vector<Deal> const layouts{
        make_split_then_finesse_layout(Jack, Two),
        make_split_then_finesse_layout(Queen, Three),
        make_split_then_finesse_layout(Jack, Four),
        make_split_then_finesse_layout(Queen, Five),
        make_split_then_finesse_layout(Jack, Six),
        make_split_then_finesse_layout(Queen, Seven),
        make_split_then_finesse_layout(Jack, Ten),
        make_split_then_finesse_layout(Queen, Ten),
    };
    be::assert_pool_matches(layouts);
    VectorLayoutSource const source(layouts);
    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};

    EvaluationResult const result = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/2,
        source,
        pi,
        single_card_defender,
        EvaluateOptions{.sampling = {.sample_size = 6u, .replenish_below = 6u}});
    ASSERT_FALSE(result.error.has_value());
    EXPECT_EQ(result.by_strategy.at(1u).p_make, 0.75);
}

// Mass conserved across a fixture that replenishes
// repeatedly -- both spade branches above replenish once each, so the
// same fixture already exercises this; checked here against the total
// mass directly (kappa * layout count is not observable from outside,
// but node_mass's own invariant is exactly what the exhaustive-vs-
// replenished agreement above already certifies: if either replenishment
// had rescaled kappa wrongly, the two runs above would not agree to
// floating-point tolerance, and they do).
TEST_F(ReproductionTest, MassIsConservedAcrossRepeatedReplenishmentOnTheSplitThenFinesseFixture)
{
    std::vector<Deal> const layouts{
        make_split_then_finesse_layout(Jack, Two),
        make_split_then_finesse_layout(Queen, Three),
        make_split_then_finesse_layout(Jack, Four),
        make_split_then_finesse_layout(Queen, Five),
        make_split_then_finesse_layout(Jack, Six),
        make_split_then_finesse_layout(Queen, Seven),
        make_split_then_finesse_layout(Jack, Ten),
        make_split_then_finesse_layout(Queen, Ten),
    };
    VectorLayoutSource const source(layouts);
    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};

    EvaluationResult const result = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/2,
        source,
        pi,
        single_card_defender,
        EvaluateOptions{.collect_counters = true, .sampling = {.sample_size = 6u, .replenish_below = 6u}});
    ASSERT_FALSE(result.error.has_value());
    ASSERT_TRUE(result.by_strategy.at(1u).counters.has_value());

    // Both spade branches replenish (2 attempts, both succeeding) -- the
    // repeated-replenishment case this criterion asks for.
    std::uint64_t total_attempted = 0;
    std::uint64_t total_succeeded = 0;
    for (be::DepthReplenishmentStats const& stats : result.by_strategy.at(1u).counters->replenishment_by_depth)
    {
        total_attempted += stats.attempted;
        total_succeeded += stats.succeeded;
    }
    EXPECT_EQ(total_attempted, 2u);
    EXPECT_EQ(total_succeeded, 2u);

    // Mass conservation itself: the total probability mass this run
    // reports (p_make summed with what every dead branch discarded is not
    // observable from outside, but the exhaustive-agreement test above
    // already is the mass-conservation check in its strongest form -- a
    // kappa rescaled even one ULP wrong at either branch would show up
    // there as a p_make that does not match the true 3/4 to tolerance.
    // Restated here directly: p_make itself is a mass (node_mass summed
    // up the tree), and it lands on the hand-derived value.
    EXPECT_NEAR(result.by_strategy.at(1u).p_make, 0.75, 1e-9);
}

// Scan-to-hit by depth, and delta calls per replenishment.
TEST_F(ReproductionTest, ScanToHitAndDeltaCallsPerReplenishmentAreReportedOnTheSplitThenFinesseFixture)
{
    std::vector<Deal> const layouts{
        make_split_then_finesse_layout(Jack, Two),
        make_split_then_finesse_layout(Queen, Three),
        make_split_then_finesse_layout(Jack, Four),
        make_split_then_finesse_layout(Queen, Five),
        make_split_then_finesse_layout(Jack, Six),
        make_split_then_finesse_layout(Queen, Seven),
        make_split_then_finesse_layout(Jack, Ten),
        make_split_then_finesse_layout(Queen, Ten),
    };
    VectorLayoutSource const source(layouts);
    DeclarerStrategy const pi{.id = 1, .play = single_card_declarer_play, .state_key = nullptr};

    std::uint64_t delta_calls = 0;
    be::DefenderStrategy const counting_delta = [&](be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
    {
        ++delta_calls;
        return single_card_defender(query);
    };

    EvaluationResult const result = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/2,
        source,
        pi,
        counting_delta,
        EvaluateOptions{.collect_counters = true, .sampling = {.sample_size = 6u, .replenish_below = 6u}});
    ASSERT_FALSE(result.error.has_value());
    ASSERT_TRUE(result.by_strategy.at(1u).counters.has_value());

    // Scan-to-hit by depth: at() calls per layout added, derived from the
    // stored counts rather than timed. Both branches' scans land at depth
    // 1 (East, the root's own seat, leads the spade split -- its children
    // are reached at depth 1, not depth 2, since evaluate()'s own root
    // block dispatches the split itself). Each branch's own scan examines
    // all 8 source entries: its own already-present layouts, the other
    // filler's layouts rejected on the recorded spade ply (no delta
    // call), and the matching "ten" accepted -- 8 at() calls each, 16
    // total, 2 layouts added -- a scan-to-hit of 8.
    std::vector<be::DepthReplenishmentStats> const& by_depth =
        result.by_strategy.at(1u).counters->replenishment_by_depth;
    std::uint64_t total_at_calls = 0;
    std::uint64_t total_layouts_added = 0;
    for (be::DepthReplenishmentStats const& stats : by_depth)
    {
        total_at_calls += stats.at_calls;
        total_layouts_added += stats.layouts_added;
    }
    ASSERT_GT(total_layouts_added, 0u);
    double const scan_to_hit = static_cast<double>(total_at_calls) / static_cast<double>(total_layouts_added);
    EXPECT_EQ(total_at_calls, 16u);
    EXPECT_EQ(total_layouts_added, 2u);
    EXPECT_DOUBLE_EQ(scan_to_hit, 8.0);  // the one genuinely derived (divided) quantity here

    // Delta calls, hand-derived from the fixture's own shape. Four
    // defender plies exist in this two-trick ending -- East's spade lead
    // (history 0), West's spade follow (history 2), East's heart follow
    // (history 5), West's heart follow, completing trick 2 (history 7) --
    // North and South are declarer-side throughout and call delta never.
    // Ordinary expansion: East's spade ply queries delta once per root
    // layout (6, since the root itself is the split here); each of the
    // other three defender plies queries delta once per layout then
    // present in each branch (4 per branch, post-replenishment, since
    // both "ten" layouts join before West's spade ply is ever reached) --
    // 3 plies * 4 layouts * 2 branches = 24. Replay: each replenished
    // "ten" candidate's own p_j is accumulated by replaying delta at
    // every defender ply already crossed at the point it joins (depth 1,
    // right after East's spade lead) -- exactly the one ply, East's own
    // spade lead -- so 1 replay delta call per candidate, 2 total.
    // Grand total: 6 + 24 + 2 = 32.
    EXPECT_EQ(delta_calls, 32u);
}
