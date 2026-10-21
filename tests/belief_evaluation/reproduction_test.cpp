#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/evaluate.hpp>

#include "test_support.hpp"

// A namespace alias and targeted `using` declarations, not `using
// namespace dds::belief_evaluation;`. This file includes
// api/dds_data_types.hpp, which declares a global ::Card, so a bare `Card`
// would be ambiguous instead of a clear compile error.
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

// End-to-end evidence that early cuts change no answer; cuts_test.cpp
// tests each cut on its own.
//
// Tier 1 has no off switch, so every test in this suite already runs with
// it. This file adds the other half: evidence that the cuts fire on a
// fixture shaped like the oracle fixtures (several tricks, several
// layouts).
//
// The run with tier 1 and tier 2 together needs the solver
// (DoubleDummyDefender with DoubleDummyBound), so it is in
// double_dummy_bound_test.cpp.

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
    // North holds the top three spades: three certain tricks, whatever the
    // defenders hold. East on lead, deterministic under the scripted
    // strategies. North needs only 2 of the 3 tricks, so the already-made cut
    // fires right after trick 2 and skips trick 3.
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
    // Hand-counted tree, one deterministic path, needing 2 of 3: the root,
    // then one node per card. Trick 1 is East, South, West, North (nodes
    // 2-5). North's honours beat the defenders' low cards, so North wins and
    // leads trick 2: North, East, South, West (nodes 6-9). At node 9, after
    // trick 2 resolves (tricks_won 2 >= needed 2), the already-made cut fires:
    // visited but not expanded, so trick 3 is never touched.
    constexpr std::uint64_t NodesWithCuts = 9;
    EXPECT_EQ(value.counters->nodes_visited, NodesWithCuts);

    // The uncut comparison: needing all 3 tricks gives the already-made cut no
    // chance to fire early, so the recursion runs to its terminal node. Tier 1
    // has no off switch, so cuts_test.cpp uses the same technique.
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
    // root + 3 tricks of 4 plies = 1 + 12 = 13, including the terminal node
    // (is_dead() and already_made() stay false until the last card).
    constexpr std::uint64_t NodesWithoutTheEarlyCut = 13;
    EXPECT_EQ(
        uncut_comparison.by_strategy.at(1u).counters->nodes_visited, NodesWithoutTheEarlyCut);

    // The cut removed 4 of the 13 nodes (all of trick 3), about 31%, on a
    // fixture with one cut opportunity.
    EXPECT_LT(NodesWithCuts, NodesWithoutTheEarlyCut);
}

// Convergence towards the exhaustive answer as the sample size M rises.
// The exhaustive answer must be neither 0 nor 1: a fixture that always
// makes or always fails would "converge" by luck.

namespace
{
    /// North holds the Nine and South (dummy) the Eight, never the highest
    /// card. East and West share the pool {Two..Seven, Ten}: six ranks below
    /// the Nine and one (Ten) above it. East leads its only card. West plays
    /// its lowest card, always one of the low ranks.
    ///
    /// Whoever holds the Ten decides the trick. If East holds it, East's lead
    /// beats the Nine and nothing beats it: declarer loses. If West holds it,
    /// West's lowest card is never the Ten, so the Nine wins: declarer wins.
    /// Six of the seven assignments to East win, so the true answer is 6/7.
    /// The exhaustive run below confirms it.
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
    // Two identical rounds of the seven assignments, Ten last in each: 14
    // layouts. (A sample of 3 from a space of 4 would show nothing.) The
    // rounds are identical so the true ratio is exactly 6/7 at M = 7 and at
    // M = 14, pinning the comparison two ways.
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
    // The hand-derived answer: 6/7 was reasoned from the construction above,
    // not read off this run.
    EXPECT_NEAR(true_p_make, 6.0 / 7.0, 1e-9);

    // M = 6: Ten is last in each round, so the first six layouts are the six
    // winners. The sample has not seen the losing case and reports false
    // certainty.
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

    // M = 7: one full round, so the sample holds the losing assignment in the
    // same proportion as the whole space. The estimate is exactly the true
    // ratio.
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

    // Convergence as a strictly decreasing distance: M = 6 is 1/7 from the
    // truth (false certainty from missing the one losing case), M = 7 is 0.
    // This is how far a small sample can be from the truth, not only that a
    // large enough one matches it.
    double const error_at_six = std::abs(m6.by_strategy.at(1u).p_make - true_p_make);
    double const error_at_seven = std::abs(m7.by_strategy.at(1u).p_make - true_p_make);
    EXPECT_LT(error_at_seven, error_at_six);
}

// ===========================================================================
// Replenishment: reproduction, convergence, and the measurements.
// ===========================================================================

// M >= N still reproduces the exhaustive run bitwise with replenishment
// enabled. If a scan accepts even one layout here, either the exclusion
// set misses a duplicate or the replay produces a layout different from
// the one present. Both are bugs; no tolerance applies.
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

    // sample_size = 14 = N, and replenish_below = 100, so the trigger fires at
    // every node (no node holds more than 14 layouts). Every scan must find
    // nothing: the root drew every consistent layout, so each node already
    // holds the complete set for its path.
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

// Replenishment cannot recover this fixture's M = 6 case, whatever
// replenish_below is. A node can only gain a layout once its count has
// dropped below `sample_size`, which needs an earlier defender split on
// its path, and it accepts only candidates that match every defender ply
// played there. Here the only defender ply is the one that separates the
// missing Ten layout from the six present. No earlier split creates a node
// the Ten layout could join. The test below confirms this.
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
    EXPECT_DOUBLE_EQ(result.by_strategy.at(1u).p_make, 1.0);  // the same as M = 6 without replenishment
}

namespace
{
    /// The finesse pool and ratio of make_finesse_style_layout, reshaped so a
    /// node-local scan can reach the missing layout. A harmless spade split
    /// (East holds the jack or the queen, West the other) comes before the
    /// heart finesse. That gives replenishment a node below sample_size before
    /// the heart ply, the one that matters.
    ///
    /// East leads, not North. East's delta leads its lowest-indexed suit
    /// (spades), forcing the split first. A defender root does not explore
    /// every other legal first card, as a declarer root does, so North's two
    /// cards (the spade ace and heart nine) open no extra subtree.
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
    // Eight layouts: three winning hearts under each spade filler (six,
    // matching the M = 6 count above), then the losing heart (Ten) under each
    // filler, last in source order. sample_size = 6 draws the six winners and
    // misses both Ten layouts, as M = 6 did above.
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
    // Hand-derived: 6 of 8 layouts win (every heart but the two Tens), so
    // 6/8 = 3/4. The ratio differs from 6/7 by construction: the same ratio
    // would need the missing layout to sit at the only defender ply, the shape
    // the test above shows replenishment cannot reach.
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
    // 1.0 without replenishment, 0.75 with it: exactly the exhaustive answer.
    double const p_make_without = m6_no_replenishment.by_strategy.at(1u).p_make;
    double const p_make_with = m6_with_replenishment.by_strategy.at(1u).p_make;
    EXPECT_DOUBLE_EQ(p_make_without, 1.0);
    EXPECT_NEAR(p_make_with, 0.75, 1e-9);
    EXPECT_LT(std::abs(p_make_with - 0.75), std::abs(p_make_without - 0.75));
}

// EXPECT_EQ on a double on purpose, not EXPECT_NEAR or EXPECT_DOUBLE_EQ
// (which allows 4 ULP). The fixture samples and replenishes, so it runs
// the kappa rescale, the node-local scan and the trigger. Its true answer,
// 6 of 8 = 0.75, is exactly representable, so bitwise equality is the
// right bar. It shows that the default build and -c opt agree exactly on a
// path where an NDEBUG-only difference (an assert argument with a side
// effect, an NDEBUG-conditional path) could show up. If it fails under
// -c opt but passes in the default build, that is the bug it exists to
// catch. Do not loosen it to EXPECT_NEAR.
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

// Mass is conserved when replenishment runs more than once: both spade
// branches above replenish once each. kappa times the layout count is not
// observable from outside, but the agreement with the exhaustive answer
// above certifies it. A wrongly rescaled kappa in either branch would break
// that agreement.
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

    // Both spade branches replenish: 2 attempts, both succeed.
    std::uint64_t total_attempted = 0;
    std::uint64_t total_succeeded = 0;
    for (be::DepthReplenishmentStats const& stats : result.by_strategy.at(1u).counters->replenishment_by_depth)
    {
        total_attempted += stats.attempted;
        total_succeeded += stats.succeeded;
    }
    EXPECT_EQ(total_attempted, 2u);
    EXPECT_EQ(total_succeeded, 2u);

    // The exhaustive agreement above is the strongest mass check: a kappa
    // rescaled even one ULP wrong in either branch would move p_make off the
    // true 3/4. Restated here: p_make is a mass (node_mass summed up the
    // tree), and it equals the hand-derived value.
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

    // Scan-to-hit by depth: at() calls per layout added, from the stored
    // counts, not timed. Both scans happen at depth 1: East, on lead at the
    // root, leads the spade split, and evaluate()'s root handling dispatches
    // it. Each scan examines all 8 source entries: its own layouts, the other
    // filler's layouts (rejected on the recorded spade ply, with no delta
    // call), and the matching Ten layout (accepted). So 8 at() calls each,
    // 16 in total, 2 layouts added: a scan-to-hit of 8.
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
    EXPECT_DOUBLE_EQ(scan_to_hit, 8.0);  // the one derived (divided) quantity here

    // Delta calls, hand-derived. The two-trick ending has four defender
    // plies: East's spade lead (history 0), West's spade follow (history 2),
    // East's heart follow (history 5) and West's heart follow, ending trick 2
    // (history 7). North and South never call delta.
    // - Expansion: East's spade lead queries delta once per root layout (6).
    //   Each of the other three plies queries it once per layout in each
    //   branch: 4 per branch after replenishment, since both Ten layouts join
    //   before West's spade ply. 3 plies * 4 layouts * 2 branches = 24.
    // - Replay: each replenished Ten layout replays delta at every defender ply
    //   already crossed where it joins (depth 1, after East's spade lead). That
    //   is one ply, so 1 call per layout, 2 in total.
    // Total: 6 + 24 + 2 = 32.
    EXPECT_EQ(delta_calls, 32u);
}
