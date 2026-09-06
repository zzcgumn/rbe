#include <gtest/gtest.h>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/node.hpp>

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
using be::RootConstructionResult;
using be::RootFailure;
using be::RootOptions;
using be::ScanOutcome;
using be::StrategyId;
using be::VectorLayoutSource;
using be::assert_forms_one_belief_node;
using be::assert_pool_matches;
using be::evaluate;
using be::holding;
using be::make_root;
using be::node_mass;
using be::single_card_declarer_play;
using be::single_card_defender;

// Root sampling: make_root scans source from index 0 and takes the first
// (up to) RootOptions::sample_size consistent layouts, rather than every
// one -- the first place anything in this module samples. Every fixture
// below asserts layouts.size() directly:
// make_root now has two reasons to return fewer layouts than a fixture
// author intended (the consistency filter and the cap), and a fixture that
// does not pin the count cannot tell them apart.

namespace
{
    constexpr int Two = 2;
    constexpr int Three = 3;
    constexpr int Four = 4;
    constexpr int Five = 5;
    constexpr int Jack = 11;
    constexpr int Queen = 12;
    constexpr int King = 13;
    constexpr int Ace = 14;

    constexpr int Spades = 0;
    constexpr int Clubs = 3;

    constexpr int North = 0;  // declarer
    constexpr int East = 1;   // a defender
    constexpr int South = 2;  // dummy
    constexpr int West = 3;   // a defender

    auto strategy(StrategyId id) -> DeclarerStrategy
    {
        return DeclarerStrategy{.id = id, .play = single_card_declarer_play, .state_key = nullptr};
    }

    /// `count` layouts, North holding the top two spades outright (a
    /// certain trick, however East and West's own spades split) so the
    /// contract's fate never depends on the club filler below -- an
    /// untouched suit that lets several otherwise-identical layouts
    /// coexist in one source and one belief node (mirroring
    /// cuts_test.cpp's own club-filler fixtures, reused in spirit rather
    /// than re-derived verbatim). The club filler is a fixed `count`-card
    /// pool (ranks Two, Two+1, ..., Two+count-1), the same union pool in
    /// every layout, split a different way each time: layout `i` gives
    /// West the pool's `i`-th card alone and East every other one -- `count`
    /// distinct splits of the identical pool, so every layout survives
    /// make_root's consistency filter into the same node.
    auto make_layouts_with_distinct_fillers(int count) -> std::vector<Deal>
    {
        std::vector<int> pool;
        pool.reserve(count);
        for (int i = 0; i < count; ++i)
        {
            pool.push_back(Two + i);
        }

        std::vector<Deal> layouts;
        layouts.reserve(count);
        for (int west_index = 0; west_index < count; ++west_index)
        {
            Deal deal{};
            deal.trump = DDS_NOTRUMP;
            deal.first = East;
            deal.remainCards[North][Spades] = holding({Ace, King});
            deal.remainCards[East][Spades] = holding({Queen, Jack});
            deal.remainCards[South][Spades] = holding({Two, Three});
            deal.remainCards[West][Spades] = holding({Four, Five});
            deal.remainCards[West][Clubs] = holding({pool[west_index]});
            unsigned east_clubs = 0;
            for (int j = 0; j < count; ++j)
            {
                if (j != west_index)
                {
                    east_clubs |= holding({pool[j]});
                }
            }
            deal.remainCards[East][Clubs] = east_clubs;
            layouts.push_back(deal);
        }
        return layouts;
    }
}

class SamplingTest : public ::testing::Test
{
};

TEST_F(SamplingTest, StopsAfterTheRequestedNumberOfConsistentLayoutsAndMarksIsSample)
{
    std::vector<Deal> const layouts = make_layouts_with_distinct_fillers(5);
    assert_pool_matches(layouts);
    assert_forms_one_belief_node(layouts, North);
    VectorLayoutSource const source(layouts);

    RootConstructionResult const result =
        make_root(layouts.front(), North, /*tricks_needed=*/1, source, RootOptions{.sample_size = 2u});

    ASSERT_TRUE(result.node.has_value());
    EXPECT_EQ(result.node->layouts.size(), 2u);
    EXPECT_DOUBLE_EQ(result.node->kappa, 0.5);
    // The scan stopped short of the 5 consistent layouts actually
    // available -- a genuine sample of a larger space, not the whole of
    // it.
    EXPECT_TRUE(result.node->is_sample);
}

TEST_F(SamplingTest, MEqualToNLeavesIsSampleFalseAndMatchesExhaustiveBitwise)
{
    std::vector<Deal> const layouts = make_layouts_with_distinct_fillers(3);
    assert_pool_matches(layouts);
    assert_forms_one_belief_node(layouts, North);
    VectorLayoutSource const source(layouts);

    // Read directly on the node, not only inferred from p_make matching
    // below: is_sample = options.sample_size.has_value() alone would pass
    // the bitwise check further down while still being wrong here, since
    // kappa and the layouts are unaffected by the mistake -- see
    // make_root's own doxygen.
    RootConstructionResult const capped =
        make_root(layouts.front(), North, /*tricks_needed=*/1, source, RootOptions{.sample_size = 3u});
    ASSERT_TRUE(capped.node.has_value());
    EXPECT_EQ(capped.node->layouts.size(), 3u);
    EXPECT_FALSE(capped.node->is_sample);

    EvaluationResult const exhaustive =
        evaluate(layouts.front(), North, /*tricks_needed=*/1, source, strategy(1), single_card_defender);
    EvaluationResult const sampled = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.sample_size = 3u});

    ASSERT_FALSE(exhaustive.error.has_value());
    ASSERT_FALSE(sampled.error.has_value());
    // Bitwise, not EXPECT_DOUBLE_EQ -- the criterion is byte-for-byte
    // reproduction, not mere agreement within tolerance.
    EXPECT_EQ(exhaustive.by_strategy.at(1u).p_make, sampled.by_strategy.at(1u).p_make);
    ASSERT_EQ(
        exhaustive.by_strategy.at(1u).root_children.size(),
        sampled.by_strategy.at(1u).root_children.size());
    for (std::size_t i = 0; i < exhaustive.by_strategy.at(1u).root_children.size(); ++i)
    {
        EXPECT_EQ(
            exhaustive.by_strategy.at(1u).root_children[i].value,
            sampled.by_strategy.at(1u).root_children[i].value);
    }
}

TEST_F(SamplingTest, MGreaterThanNLeavesIsSampleFalseAndMatchesExhaustiveBitwise)
{
    // Same fixture and same proof as the M == N test above, at the other
    // side of the M vs. N boundary: a cap that is never reached at all,
    // not one that is reached exactly as the source runs out.
    std::vector<Deal> const layouts = make_layouts_with_distinct_fillers(3);
    assert_pool_matches(layouts);
    assert_forms_one_belief_node(layouts, North);
    VectorLayoutSource const source(layouts);

    RootConstructionResult const capped = make_root(
        layouts.front(), North, /*tricks_needed=*/1, source, RootOptions{.sample_size = 1000u});
    ASSERT_TRUE(capped.node.has_value());
    EXPECT_EQ(capped.node->layouts.size(), 3u);
    EXPECT_FALSE(capped.node->is_sample);

    EvaluationResult const exhaustive =
        evaluate(layouts.front(), North, /*tricks_needed=*/1, source, strategy(1), single_card_defender);
    EvaluationResult const sampled = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.sample_size = 1000u});

    ASSERT_FALSE(exhaustive.error.has_value());
    ASSERT_FALSE(sampled.error.has_value());
    EXPECT_EQ(exhaustive.by_strategy.at(1u).p_make, sampled.by_strategy.at(1u).p_make);
    ASSERT_EQ(
        exhaustive.by_strategy.at(1u).root_children.size(),
        sampled.by_strategy.at(1u).root_children.size());
    for (std::size_t i = 0; i < exhaustive.by_strategy.at(1u).root_children.size(); ++i)
    {
        EXPECT_EQ(
            exhaustive.by_strategy.at(1u).root_children[i].value,
            sampled.by_strategy.at(1u).root_children[i].value);
    }
}

TEST_F(SamplingTest, SampleSizeZeroIsRejectedRatherThanMisreportedAsNoLayoutSurvived)
{
    // sample_size = 0 must not be confused with "the source was checked and
    // had nothing consistent in it" -- the source here genuinely holds
    // three consistent layouts (the fixtures above already prove
    // make_layouts_with_distinct_fillers produces layouts make_root
    // accepts), never inspected because the request itself was degenerate.
    std::vector<Deal> const layouts = make_layouts_with_distinct_fillers(3);
    assert_pool_matches(layouts);
    assert_forms_one_belief_node(layouts, North);
    VectorLayoutSource const source(layouts);

    RootConstructionResult const result =
        make_root(layouts.front(), North, /*tricks_needed=*/1, source, RootOptions{.sample_size = 0u});

    EXPECT_FALSE(result.node.has_value());
    EXPECT_EQ(result.failure, RootFailure::SampleSizeZero);
    EXPECT_NE(result.failure, RootFailure::NoLayoutSurvived);
}

TEST_F(SamplingTest, TierTwoNeverFiresOnceTheRootIsAGenuineSample)
{
    // A bound scripted to claim every layout dead (0 tricks), which is
    // false for this fixture (North's AK is a certain trick) -- the claim
    // does not need to be true to test the mechanism, only to give tier 2
    // a reason to fire if it is not gated off. Run A (exhaustive, no
    // sample_size) shows the cut firing on exactly this input: is_sample
    // is false there, so the bound is trusted and the wrong claim wins,
    // p_make = 0.0. Run B (sample_size < N, is_sample genuinely true)
    // shows the same cut gated off entirely -- tier2_cuts stays 0, the
    // recursion runs for real, and the true value (1.0) is what comes back
    // despite the same fictitious bound, proving sampling protects the
    // answer from an over-aggressive bound rather than merely coinciding
    // with a suppressed cut that would not have fired anyway.
    std::vector<Deal> const layouts = make_layouts_with_distinct_fillers(3);
    assert_pool_matches(layouts);
    assert_forms_one_belief_node(layouts, North);
    VectorLayoutSource const source(layouts);
    auto const claims_every_layout_dead = [](Deal const&) -> int { return 0; };

    EvaluationResult const exhaustive = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{
            .collect_counters = true,
            .bound = claims_every_layout_dead,
            .delta_is_double_dummy_optimal = true});
    ASSERT_FALSE(exhaustive.error.has_value());
    ASSERT_TRUE(exhaustive.by_strategy.at(1u).counters.has_value());
    EXPECT_EQ(exhaustive.by_strategy.at(1u).counters->tier2_cuts, 1u);
    EXPECT_EQ(exhaustive.by_strategy.at(1u).p_make, 0.0);  // the cut trusted the wrong claim

    EvaluationResult const sampled = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{
            .collect_counters = true,
            .bound = claims_every_layout_dead,
            .delta_is_double_dummy_optimal = true,
            .sample_size = 2u});
    ASSERT_FALSE(sampled.error.has_value());
    ASSERT_TRUE(sampled.by_strategy.at(1u).counters.has_value());
    EXPECT_EQ(sampled.by_strategy.at(1u).counters->tier2_cuts, 0u);
    EXPECT_EQ(sampled.by_strategy.at(1u).p_make, 1.0);  // the true value, reached despite the bound
}

// The scan budget: caps RootOptions/EvaluateOptions::scan_budget calls to
// source.at() specifically, independently of sample_size -- a degraded but
// usable answer when it binds and still finds something, a genuine failure
// (distinct from NoLayoutSurvived) when it binds and finds nothing at all.

TEST_F(SamplingTest, ScanOutcomeReflectsWhyTheScanStopped)
{
    // Three runs against the same 5-layout, all-consistent source, one per
    // ScanOutcome value: no caps at all (SourceExhausted -- the exhaustive
    // case, which is why the enum has no separate "not sampling" value); a
    // sample_size that binds before the source runs out (SampleFilled); a
    // scan_budget that binds before sample_size and before the source runs
    // out (BudgetExhausted).
    std::vector<Deal> const layouts = make_layouts_with_distinct_fillers(5);
    assert_pool_matches(layouts);
    assert_forms_one_belief_node(layouts, North);
    VectorLayoutSource const source(layouts);

    RootConstructionResult const exhaustive =
        make_root(layouts.front(), North, /*tricks_needed=*/1, source);
    ASSERT_TRUE(exhaustive.node.has_value());
    EXPECT_EQ(exhaustive.outcome, ScanOutcome::SourceExhausted);
    EXPECT_FALSE(exhaustive.node->is_sample);

    RootConstructionResult const sample_filled = make_root(
        layouts.front(), North, /*tricks_needed=*/1, source, RootOptions{.sample_size = 2u});
    ASSERT_TRUE(sample_filled.node.has_value());
    EXPECT_EQ(sample_filled.outcome, ScanOutcome::SampleFilled);
    EXPECT_TRUE(sample_filled.node->is_sample);

    RootConstructionResult const budget_exhausted = make_root(
        layouts.front(),
        North,
        /*tricks_needed=*/1,
        source,
        RootOptions{.sample_size = 5u, .scan_budget = 2u});
    ASSERT_TRUE(budget_exhausted.node.has_value());
    EXPECT_EQ(budget_exhausted.outcome, ScanOutcome::BudgetExhausted);
    EXPECT_EQ(budget_exhausted.node->layouts.size(), 2u);
    EXPECT_TRUE(budget_exhausted.node->is_sample);
}

TEST_F(SamplingTest, KappaStaysOneOverLayoutsActuallyDrawnUnderABudget)
{
    // The test that would catch kappa computed as 1/scan_budget's target
    // instead of 1/(layouts actually drawn): node_mass = kappa *
    // sum(p_i), and if kappa used the wrong denominator, mass would fall
    // silently below 1 with nothing to flag it -- see make_root's own
    // doxygen. Bitwise, not EXPECT_DOUBLE_EQ: this is the criterion, not
    // an approximation of it.
    std::vector<Deal> const layouts = make_layouts_with_distinct_fillers(5);
    assert_pool_matches(layouts);
    assert_forms_one_belief_node(layouts, North);
    VectorLayoutSource const source(layouts);

    RootConstructionResult const result = make_root(
        layouts.front(),
        North,
        /*tricks_needed=*/1,
        source,
        RootOptions{.sample_size = 5u, .scan_budget = 2u});

    ASSERT_TRUE(result.node.has_value());
    EXPECT_EQ(result.node->layouts.size(), 2u);
    EXPECT_DOUBLE_EQ(result.node->kappa, 0.5);
    EXPECT_EQ(node_mass(*result.node), 1.0);
}

namespace
{
    /// `base` with declarer's own holding changed so it fails
    /// is_consistent() against any root sharing `base`'s original
    /// declarer holding -- a candidate the scan will burn a source.at()
    /// call on without ever adding a layout.
    auto make_inconsistent_variant(Deal base) -> Deal
    {
        base.remainCards[North][Spades] = holding({Two, Three});
        return base;
    }
}

TEST_F(SamplingTest, BudgetExhaustedBeforeAnyLayoutSurvivesIsAFailureDistinctFromNoLayoutSurvived)
{
    // Budget covers only the first two (both inconsistent) of three source
    // entries; the third, past the budget, is consistent -- so this is
    // genuinely "the budget ran out before finding one", not "the whole
    // source was checked and rejected everything". The two must not be
    // confused: a caller seeing NoLayoutSurvived would go fix their source,
    // which would accomplish nothing here.
    Deal const root_layout = make_layouts_with_distinct_fillers(1).front();
    std::vector<Deal> const source_layouts = {
        make_inconsistent_variant(root_layout),
        make_inconsistent_variant(root_layout),
        root_layout,
    };
    VectorLayoutSource const source(source_layouts);

    RootConstructionResult const result = make_root(
        root_layout, North, /*tricks_needed=*/1, source, RootOptions{.scan_budget = 2u});

    EXPECT_FALSE(result.node.has_value());
    EXPECT_EQ(result.failure, RootFailure::ScanBudgetExhausted);
    EXPECT_NE(result.failure, RootFailure::NoLayoutSurvived);
}

TEST_F(SamplingTest, ABudgetLargeEnoughNotToBindMatchesExhaustiveBehaviourBitwise)
{
    std::vector<Deal> const layouts = make_layouts_with_distinct_fillers(3);
    assert_pool_matches(layouts);
    assert_forms_one_belief_node(layouts, North);
    VectorLayoutSource const source(layouts);

    EvaluationResult const exhaustive =
        evaluate(layouts.front(), North, /*tricks_needed=*/1, source, strategy(1), single_card_defender);
    EvaluationResult const generously_budgeted = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.sample_size = 1000u, .scan_budget = 1000u});

    ASSERT_FALSE(exhaustive.error.has_value());
    ASSERT_FALSE(generously_budgeted.error.has_value());
    EXPECT_EQ(exhaustive.by_strategy.at(1u).p_make, generously_budgeted.by_strategy.at(1u).p_make);
    ASSERT_EQ(
        exhaustive.by_strategy.at(1u).root_children.size(),
        generously_budgeted.by_strategy.at(1u).root_children.size());
    for (std::size_t i = 0; i < exhaustive.by_strategy.at(1u).root_children.size(); ++i)
    {
        EXPECT_EQ(
            exhaustive.by_strategy.at(1u).root_children[i].value,
            generously_budgeted.by_strategy.at(1u).root_children[i].value);
    }
}

TEST_F(SamplingTest, ALargeSampleWithMGreaterThanNIsStillBitIdenticalToExhaustive)
{
    // A sample of 3 from a space of 4 shows nothing -- this fixture is
    // larger than any this module has built for a sampling test so far:
    // 25 consistent layouts (via the same distinct-club-filler pattern
    // used throughout this file), sampled with an M generous enough never
    // to bind. The claim under test is the same as the smaller M > N
    // tests above; this is a larger fixture, not a materially different
    // property.
    std::vector<Deal> const layouts = make_layouts_with_distinct_fillers(25);
    assert_pool_matches(layouts);
    assert_forms_one_belief_node(layouts, North);
    VectorLayoutSource const source(layouts);

    EvaluationResult const exhaustive =
        evaluate(layouts.front(), North, /*tricks_needed=*/1, source, strategy(1), single_card_defender);
    EvaluationResult const sampled = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.sample_size = 1000u});

    ASSERT_FALSE(exhaustive.error.has_value());
    ASSERT_FALSE(sampled.error.has_value());
    EXPECT_EQ(exhaustive.by_strategy.at(1u).p_make, sampled.by_strategy.at(1u).p_make);
    ASSERT_EQ(
        exhaustive.by_strategy.at(1u).root_children.size(),
        sampled.by_strategy.at(1u).root_children.size());
    for (std::size_t i = 0; i < exhaustive.by_strategy.at(1u).root_children.size(); ++i)
    {
        EXPECT_EQ(
            exhaustive.by_strategy.at(1u).root_children[i].value,
            sampled.by_strategy.at(1u).root_children[i].value);
    }
}

namespace
{
    constexpr int Hearts = 1;

    /// North holds a certain winner in each of two independent suits
    /// (spades' Ace, hearts' Ace) so both tricks always resolve North's
    /// way regardless of the defenders' own split. East's own card in
    /// each suit is independently one of {Two, Three} -- two bits, four
    /// layouts -- so East's forced play (single_card_defender: its only
    /// legal card) splits the tree at two genuinely different depths: the
    /// spades bit at the very root (East leads trick 1), the hearts bit
    /// only after trick 1 has fully resolved and North has led trick 2.
    /// South holds a fixed, irrelevant low card (Four) in each suit; West
    /// holds whichever of {Two, Three} East does not.
    auto make_deep_layout(int east_spade, int east_heart) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[North][Spades] = holding({Ace});
        deal.remainCards[North][Hearts] = holding({Ace});
        deal.remainCards[South][Spades] = holding({Four});
        deal.remainCards[South][Hearts] = holding({Four});
        deal.remainCards[East][Spades] = holding({east_spade});
        deal.remainCards[East][Hearts] = holding({east_heart});
        deal.remainCards[West][Spades] = holding({east_spade == Two ? Three : Two});
        deal.remainCards[West][Hearts] = holding({east_heart == Two ? Three : Two});
        return deal;
    }
}

TEST_F(SamplingTest, SampleSizeByDepthShowsCollapseAcrossSeveralPliesOnADeepExhaustiveTree)
{
    // No sample_size here -- populated on the exhaustive path too, where
    // it is a fact about the tree's own shape rather than about a sample
    // (RootConstructionResult::outcome's own doxygen makes the same point
    // about SourceExhausted). Four layouts, two independent splitting
    // bits at two different depths:
    //
    //   depth 0: root, East on lead for trick 1 -- 1 node, all 4 layouts
    //     merged (the spades bit has not been resolved yet).
    //   depth 1-3: South, West, North play out trick 1 -- 2 nodes (the
    //     spades bit split the moment East's own root-level play was
    //     dispatched), 2 layouts each (the hearts bit is still
    //     unresolved within each branch).
    //   depth 4: North leads trick 2 -- still 2 nodes, 2 layouts each.
    //   depth 5: East on play for trick 2 -- still 2 nodes (the split
    //     happens going *to* depth 6, not here).
    //   depth 6-8: South, West play out trick 2, then the node right
    //     after West's own card resolves the trick and tricks_won reaches
    //     tricks_needed (2) -- 4 nodes, 1 layout each; the already-made
    //     cut fires at depth 8, but the node there is still visited and
    //     recorded before it does.
    //
    // Read together: min goes 4 -> 2 -> 2 -> 1, the collapse from a
    // single root node down to four singletons, spread across two
    // genuinely different plies rather than a single one -- deep enough,
    // and derived from the fixture's own construction above, not read off
    // a run.
    std::vector<Deal> const layouts = {
        make_deep_layout(Two, Two),
        make_deep_layout(Two, Three),
        make_deep_layout(Three, Two),
        make_deep_layout(Three, Three),
    };
    assert_pool_matches(layouts);
    assert_forms_one_belief_node(layouts, North);
    VectorLayoutSource const source(layouts);

    EvaluationResult const result = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/2,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.collect_counters = true});

    ASSERT_FALSE(result.error.has_value());
    EvaluationValue const& value = result.by_strategy.at(1u);
    ASSERT_TRUE(value.counters.has_value());
    EXPECT_EQ(value.p_make, 1.0);  // both tricks certain, no genuine choice anywhere
    std::vector<be::DepthSampleStats> const& by_depth = value.counters->sample_size_by_depth;
    ASSERT_EQ(by_depth.size(), 9u);

    EXPECT_EQ(by_depth[0].nodes, 1u);
    EXPECT_EQ(by_depth[0].layout_sum, 4u);
    EXPECT_EQ(by_depth[0].layout_min, 4u);

    for (std::size_t depth : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{4}, std::size_t{5}})
    {
        EXPECT_EQ(by_depth[depth].nodes, 2u) << "depth " << depth;
        EXPECT_EQ(by_depth[depth].layout_sum, 4u) << "depth " << depth;
        EXPECT_EQ(by_depth[depth].layout_min, 2u) << "depth " << depth;
    }

    for (std::size_t depth : {std::size_t{6}, std::size_t{7}, std::size_t{8}})
    {
        EXPECT_EQ(by_depth[depth].nodes, 4u) << "depth " << depth;
        EXPECT_EQ(by_depth[depth].layout_sum, 4u) << "depth " << depth;
        EXPECT_EQ(by_depth[depth].layout_min, 1u) << "depth " << depth;
    }
}
