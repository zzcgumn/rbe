#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/node.hpp>

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

// Root sampling: make_root scans the source from index 0 and takes the
// first RootOptions::sample_size consistent layouts (or fewer), not all of
// them. Every fixture asserts layouts.size() directly. make_root has two
// reasons to return fewer layouts than intended (the consistency filter
// and the cap), and only a pinned count tells them apart.

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

    /// `count` layouts. North holds the top two spades, a certain trick however
    /// the spades split, so the result never depends on the club filler. The
    /// filler is an unplayed suit that lets otherwise identical layouts share
    /// one source and one node (like the club-filler fixtures in
    /// cuts_test.cpp). It is a fixed `count`-card pool (ranks Two, Two+1, ...,
    /// Two+count-1), the same in every layout, split differently each time:
    /// layout `i` gives West the pool's `i`-th card and East the rest. So there
    /// are `count` splits of one pool, and every layout survives make_root's
    /// consistency filter into the same node.
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
    // The scan stopped before the 5 consistent layouts available: a real
    // sample of a larger space.
    EXPECT_TRUE(result.node->is_sample);
}

TEST_F(SamplingTest, MEqualToNLeavesIsSampleFalseAndMatchesExhaustiveBitwise)
{
    std::vector<Deal> const layouts = make_layouts_with_distinct_fillers(3);
    assert_pool_matches(layouts);
    assert_forms_one_belief_node(layouts, North);
    VectorLayoutSource const source(layouts);

    // Read on the node, not only inferred from p_make. Setting is_sample
    // whenever options.sample_size is present would pass the bitwise check
    // below but be wrong here, since kappa and the layouts are unaffected (see
    // make_root).
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
        EvaluateOptions{.sampling = {.sample_size = 3u}});

    ASSERT_FALSE(exhaustive.error.has_value());
    ASSERT_FALSE(sampled.error.has_value());
    // Bitwise, not EXPECT_DOUBLE_EQ: the requirement is byte-for-byte
    // reproduction.
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
    // The fixture and check of the M == N test, on the other side of the
    // boundary: a cap that is never reached, not one reached exactly as the
    // source runs out.
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
        EvaluateOptions{.sampling = {.sample_size = 1000u}});

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
    // sample_size = 0 must not look like "the source was checked and held
    // nothing consistent". The source holds three consistent layouts (the
    // tests above show make_root accepts them); they are never inspected,
    // because the request itself is degenerate.
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
    // A bound scripted to claim every layout dead (0 tricks), which is false
    // here (North's AK is a certain trick). The claim need not be true; it
    // only gives tier 2 a reason to fire unless gated.
    // - Run A (exhaustive, no sample_size): is_sample is false, so the bound is
    //   trusted, the cut fires, and the wrong claim wins: p_make = 0.0.
    // - Run B (sample_size < N, is_sample true): the cut is gated off.
    //   tier2_cuts stays 0, the recursion runs, and the true value (1.0) comes
    //   back despite the bound.
    // So sampling protects the answer from an over-eager bound; it does not
    // merely coincide with a cut that would not have fired.
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
            .sampling = {.sample_size = 2u}});
    ASSERT_FALSE(sampled.error.has_value());
    ASSERT_TRUE(sampled.by_strategy.at(1u).counters.has_value());
    EXPECT_EQ(sampled.by_strategy.at(1u).counters->tier2_cuts, 0u);
    EXPECT_EQ(sampled.by_strategy.at(1u).p_make, 1.0);  // the true value, reached despite the bound
}

// The scan budget caps calls to source.at() (RootOptions or
// EvaluateOptions::scan_budget), independently of sample_size. When it
// binds and finds something, the answer is degraded but usable. When it
// binds and finds nothing, it is a real failure, distinct from
// NoLayoutSurvived.

TEST_F(SamplingTest, ScanOutcomeReflectsWhyTheScanStopped)
{
    // Three runs over the same 5-layout, all-consistent source, one per
    // ScanOutcome:
    // - no caps: SourceExhausted (the exhaustive case, which is why the enum
    //   has no separate "not sampling" value);
    // - a sample_size that binds before the source ends: SampleFilled;
    // - a scan_budget that binds before sample_size and before the source
    //   ends: BudgetExhausted.
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
    // Catches kappa computed as 1/scan_budget's target instead of 1/(layouts
    // drawn). node_mass = kappa * sum(p_i), so a wrong denominator would let
    // mass fall silently below 1 (see make_root). Bitwise, not
    // EXPECT_DOUBLE_EQ.
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
    /// `base` with declarer's holding changed, so it fails is_consistent()
    /// against any root with `base`'s declarer holding. The scan spends a
    /// source.at() call on it and adds nothing.
    auto make_inconsistent_variant(Deal base) -> Deal
    {
        base.remainCards[North][Spades] = holding({Two, Three});
        return base;
    }
}

TEST_F(SamplingTest, BudgetExhaustedBeforeAnyLayoutSurvivesIsAFailureDistinctFromNoLayoutSurvived)
{
    // The budget covers only the first two source entries, both
    // inconsistent. The third, past the budget, is consistent. So this is "the
    // budget ran out before finding one", not "the whole source was rejected".
    // The two must differ: a caller told NoLayoutSurvived would fix the source,
    // which would not help here.
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
        EvaluateOptions{.sampling = {.sample_size = 1000u, .scan_budget = 1000u}});

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
    // A sample of 3 from a space of 4 shows nothing, so this uses 25
    // consistent layouts (the club-filler pattern of this file), with an M too
    // large to bind. The property is that of the smaller M > N tests above, on
    // a larger fixture.
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
        EvaluateOptions{.sampling = {.sample_size = 1000u}});

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

    /// North holds a certain winner in each of two suits (the spade and heart
    /// aces), so both tricks go North's way whatever the split. East's card in
    /// each suit is independently Two or Three: two bits, four layouts. East
    /// plays its only card (single_card_defender), so the tree splits at two
    /// different depths: the spade bit at the root (East leads trick 1), the
    /// heart bit after trick 1 resolves and North leads trick 2. South holds a
    /// fixed low card (Four) in each suit; West holds whichever of {Two, Three}
    /// East does not.
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
    // No sample_size: the counter is filled on the exhaustive path too, where
    // it describes the tree's shape, not a sample (as
    // RootConstructionResult::outcome says of SourceExhausted). Four layouts,
    // with two splitting bits at two depths:
    //
    //   depth 0: the root, East on lead for trick 1. 1 node, all 4 layouts
    //     (the spade bit is not resolved yet).
    //   depth 1-3: South, West and North play trick 1. 2 nodes (the spade bit
    //     split when East's root play was dispatched), 2 layouts each (the
    //     heart bit is unresolved in each branch).
    //   depth 4: North leads trick 2. Still 2 nodes, 2 layouts each.
    //   depth 5: East on play for trick 2. Still 2 nodes (the split happens on
    //     the way to depth 6).
    //   depth 6-8: South and West play, then the node where West's card
    //     resolves the trick and tricks_won reaches tricks_needed (2). 4
    //     nodes, 1 layout each. The already-made cut fires at depth 8, after
    //     the node is visited and recorded.
    //
    // So layout_min goes 4 -> 2 -> 2 -> 1: from one root node to four
    // singletons, over two different plies. Derived from the construction,
    // not read off a run.
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
    EXPECT_EQ(value.p_make, 1.0);  // both tricks certain, no real choice anywhere
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
