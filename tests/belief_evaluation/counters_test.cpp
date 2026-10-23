#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/evaluate.hpp>

#include "test_support.hpp"

// EvaluationCounters, filled only when EvaluateOptions::collect_counters is
// set. This file covers nodes_visited, the per-tier cut counters,
// sample_size_by_depth and replenishment_by_depth.

// A namespace alias and targeted `using` declarations, not `using
// namespace dds::belief_evaluation;`. This file includes
// api/dds_data_types.hpp, which declares a global ::Card, so a bare `Card`
// would be ambiguous instead of a clear compile error.
namespace be = dds::belief_evaluation;
using be::DeclarerStrategy;
using be::EvaluateOptions;
using be::EvaluationResult;
using be::EvaluationValue;
using be::StrategyId;
using be::VectorLayoutSource;
using be::evaluate;
using be::holding;
using be::single_card_declarer_play;
using be::single_card_defender;

namespace
{
    constexpr int Two = 2;
    constexpr int Three = 3;
    constexpr int Jack = 11;
    constexpr int Ace = 14;

    constexpr int Spades = 0;
    constexpr int Hearts = 1;

    constexpr int North = 0;  // declarer
    constexpr int East = 1;   // a defender
    constexpr int South = 2;  // dummy
    constexpr int West = 3;   // a defender

    /// One full trick with one legal card at every turn. North's ace wins
    /// whatever anyone plays, so there is one path, and the node count depends
    /// only on the fixture's shape, not on any strategy.
    auto make_one_trick_one_legal_card_each() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = North;
        deal.remainCards[North][Spades] = holding({Ace});
        deal.remainCards[East][Spades] = holding({Two});
        deal.remainCards[South][Spades] = holding({Three});
        deal.remainCards[West][Spades] = holding({Jack});
        return deal;
    }

    auto strategy(StrategyId id) -> DeclarerStrategy
    {
        return DeclarerStrategy{.id = id, .play = single_card_declarer_play, .state_key = nullptr};
    }
}

class CountersTest : public ::testing::Test
{
};

TEST_F(CountersTest, NodesVisitedMatchesTheHandCountedTree)
{
    Deal const root_layout = make_one_trick_one_legal_card_each();
    VectorLayoutSource source({root_layout});

    // The tree (one layout, one legal card at every step, so one path and one
    // child per node):
    //   root: North to lead, declarer node                       -- node 1
    //     North plays SA -> East to play, defender node           -- node 2
    //       East plays S2 -> South to play, declarer/dummy node   -- node 3
    //         South plays S3 -> West to play, defender node       -- node 4
    //           West plays SJ -> trick resolves, hand empty,
    //                            terminal, not expanded further    -- node 5
    // nodes_visited counts every node evaluated, terminal or not, including
    // the root: 5.
    constexpr std::uint64_t ExpectedNodesVisited = 5;

    EvaluationResult const result = evaluate(
        root_layout,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.collect_counters = true});

    ASSERT_FALSE(result.error.has_value());
    EvaluationValue const& value = result.by_strategy.at(1u);
    ASSERT_TRUE(value.counters.has_value());
    EXPECT_EQ(value.counters->nodes_visited, ExpectedNodesVisited);
}

TEST_F(CountersTest, CountersAreAbsentByDefault)
{
    Deal const root_layout = make_one_trick_one_legal_card_each();
    VectorLayoutSource source({root_layout});

    EvaluationResult const result =
        evaluate(root_layout, North, /*tricks_needed=*/1, source, strategy(1), single_card_defender);

    ASSERT_FALSE(result.error.has_value());
    EXPECT_FALSE(result.by_strategy.at(1u).counters.has_value());
}

// Every other test turns counters on or off, never both, so only a direct
// test checks that the two runs agree. Bitwise, not EXPECT_DOUBLE_EQ: a counters
// flag that moves the last bit of p_make is the bug this catches.
TEST_F(CountersTest, CollectingCountersDoesNotChangeTheAnswerEitherDirection)
{
    Deal const root_layout = make_one_trick_one_legal_card_each();
    VectorLayoutSource source({root_layout});

    EvaluationResult const without_counters = evaluate(
        root_layout, North, /*tricks_needed=*/1, source, strategy(1), single_card_defender);
    EvaluationResult const with_counters = evaluate(
        root_layout,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.collect_counters = true});

    ASSERT_FALSE(without_counters.error.has_value());
    ASSERT_FALSE(with_counters.error.has_value());
    EXPECT_EQ(
        without_counters.by_strategy.at(1u).p_make, with_counters.by_strategy.at(1u).p_make);
    EXPECT_EQ(
        without_counters.by_strategy.at(1u).root_children.size(),
        with_counters.by_strategy.at(1u).root_children.size());
    for (std::size_t i = 0; i < without_counters.by_strategy.at(1u).root_children.size(); ++i)
    {
        EXPECT_EQ(
            without_counters.by_strategy.at(1u).root_children[i].value,
            with_counters.by_strategy.at(1u).root_children[i].value);
    }
}

// tier1_made_cuts, tier1_dead_cuts and tier2_cuts each count only their own
// tier. This fixture's already-made cut (node 5 in the tree of
// NodesVisitedMatchesTheHandCountedTree, where tricks_won_by_declarer
// first reaches tricks_needed) is counted in p_make(), not in evaluate()'s
// root handling, since the root has won no tricks. Neither other cut can
// fire: nothing is dead, and no bound is supplied. cuts_test.cpp's
// DeadCutTest has the other half (the dead cut fires, the made cut does
// not), and tests both cuts firing in evaluate()'s root handling, which a
// counter in p_make() alone would miss.
TEST_F(CountersTest, TierCutCountersDistinguishTheAlreadyMadeCutFromTheOthers)
{
    Deal const root_layout = make_one_trick_one_legal_card_each();
    VectorLayoutSource source({root_layout});

    EvaluationResult const result = evaluate(
        root_layout,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.collect_counters = true});

    ASSERT_FALSE(result.error.has_value());
    EvaluationValue const& value = result.by_strategy.at(1u);
    ASSERT_TRUE(value.counters.has_value());
    EXPECT_EQ(value.counters->tier1_made_cuts, 1u);
    EXPECT_EQ(value.counters->tier1_dead_cuts, 0u);
    EXPECT_EQ(value.counters->tier2_cuts, 0u);
}

// sample_size_by_depth: per-depth totals of how many layouts a node held,
// root at depth 0. It shows how fast a sample collapses as defenders
// play.

TEST_F(CountersTest, SampleSizeByDepthMatchesTheHandCountedTreeOnTheExhaustivePath)
{
    // The fixture and tree of NodesVisitedMatchesTheHandCountedTree: one
    // layout, one path, five nodes at depths 0 to 4. The root's entry is
    // written in evaluate()'s root handling, not p_make(), as for
    // nodes_visited and the tier counters. Every node holds the one layout, so
    // every depth reads {nodes=1, layout_sum=1, layout_min=1}. Each field is
    // asserted at each depth, not only the length or last entry: a uniform
    // total could hide a node counted at the wrong depth as easily as a wrong
    // count.
    Deal const root_layout = make_one_trick_one_legal_card_each();
    VectorLayoutSource source({root_layout});

    EvaluationResult const result = evaluate(
        root_layout,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.collect_counters = true});

    ASSERT_FALSE(result.error.has_value());
    EvaluationValue const& value = result.by_strategy.at(1u);
    ASSERT_TRUE(value.counters.has_value());
    ASSERT_EQ(value.counters->sample_size_by_depth.size(), 5u);
    for (std::size_t depth = 0; depth < 5; ++depth)
    {
        be::DepthSampleStats const& stats = value.counters->sample_size_by_depth[depth];
        EXPECT_EQ(stats.nodes, 1u) << "depth " << depth;
        EXPECT_EQ(stats.layout_sum, 1u) << "depth " << depth;
        EXPECT_EQ(stats.layout_min, 1u) << "depth " << depth;
    }
}

namespace
{
    constexpr int Four = 4;
    constexpr int Five = 5;
    constexpr int Six = 6;
    constexpr int King = 13;

    /// North holds a certain trick (spade Ace and King); South (dummy) holds
    /// two low spades (Five, Six) that play no part. East holds one card of
    /// the pool {Two, Three, Four} and West the other two. So three layouts
    /// share one pool, all survive make_root's filter together, and each gives
    /// East a different single card. East leads its only card
    /// (single_card_defender), so expand_defender_node's grouping by card
    /// splits the three layouts into three singleton children at once: a
    /// collapse on demand, with no solver or script.
    auto make_layout_with_easts_card(int east_rank) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[North][Spades] = holding({Ace, King});
        deal.remainCards[South][Spades] = holding({Five, Six});
        deal.remainCards[East][Spades] = holding({east_rank});
        unsigned west_mask = 0;
        for (int rank : {Two, Three, Four})
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

TEST_F(CountersTest, SampleSizeByDepthShowsTheCollapseFromASampledRootAcrossAPly)
{
    // Three consistent layouts exist (East holding Two, Three or Four).
    // sample_size = 2 draws the first two in source order (Two, Three): a real
    // sample, so is_sample is true at the root. East's forced lead splits
    // them, so depth 1 has two singleton nodes, one per branch. Nothing splits
    // them further: South's and West's plays are forced too. North's ace wins
    // trick 1 in both branches, meeting tricks_needed = 1, so the already-made
    // cut fires at depth 4 in both (the shape of
    // NodesVisitedMatchesTheHandCountedTree).
    //
    // So depths 1 to 4 read {nodes=2, layout_sum=2, layout_min=1}, and depth
    // 0 reads {nodes=1, layout_sum=2, layout_min=2}. The collapse is the drop
    // from depth 0 to depth 1.
    Deal const layout_two = make_layout_with_easts_card(Two);
    Deal const layout_three = make_layout_with_easts_card(Three);
    Deal const layout_four = make_layout_with_easts_card(Four);
    be::assert_pool_matches({layout_two, layout_three, layout_four});
    be::assert_forms_one_belief_node({layout_two, layout_three, layout_four}, North);
    VectorLayoutSource source({layout_two, layout_three, layout_four});

    EvaluationResult const result = evaluate(
        layout_two,
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.collect_counters = true, .sampling = {.sample_size = 2u}});

    ASSERT_FALSE(result.error.has_value());
    EvaluationValue const& value = result.by_strategy.at(1u);
    ASSERT_TRUE(value.counters.has_value());
    std::vector<be::DepthSampleStats> const& by_depth = value.counters->sample_size_by_depth;
    ASSERT_EQ(by_depth.size(), 5u);

    EXPECT_EQ(by_depth[0].nodes, 1u);
    EXPECT_EQ(by_depth[0].layout_sum, 2u);
    EXPECT_EQ(by_depth[0].layout_min, 2u);

    for (std::size_t depth = 1; depth < 5; ++depth)
    {
        EXPECT_EQ(by_depth[depth].nodes, 2u) << "depth " << depth;
        EXPECT_EQ(by_depth[depth].layout_sum, 2u) << "depth " << depth;
        EXPECT_EQ(by_depth[depth].layout_min, 1u) << "depth " << depth;
    }
}

// replenishment_by_depth: per-depth totals of the node-local replenishment
// scans: attempted, succeeded, layouts added, at() calls.
//
// A node's layout count drops only at a defender split (a declarer ply
// keeps its parent's count), so replenishment needs a split first.
// sample_size sets both the root draw and the top-up target, so a node
// still holding sample_size layouts wants zero. replenishment_test.cpp's
// no-more-available fixture has the same split-then-replenish shape.

namespace
{
    /// East holds one of two spade fillers (three or jack), split against West
    /// from a fixed pool. North holds only the spade ace, so evaluate()'s root
    /// has no other first card to explore. A third source entry (spade three
    /// again, another heart filler) is a new layout the "three" branch can
    /// find; the source has nothing similar for the "jack" branch.
    auto make_split_layout(int east_spade, int east_heart_filler) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = North;
        deal.remainCards[North][Spades] = holding({Ace});
        deal.remainCards[South][Spades] = holding({Two});
        deal.remainCards[East][Spades] = holding({east_spade});
        deal.remainCards[West][Spades] = holding({east_spade == Three ? Jack : Three});
        deal.remainCards[East][Hearts] = holding({east_heart_filler});  // never played
        deal.remainCards[West][Hearts] = holding({east_heart_filler == Two ? Three : Two});
        return deal;
    }
}

TEST_F(CountersTest, CollectingCountersDoesNotChangeTheAnswerEitherDirectionWithReplenishment)
{
    // The paired-run check of CollectingCountersDoesNotChangeTheAnswer-
    // EitherDirection, on a fixture where replenishment fires. Collecting
    // counters must not change the answer here either.
    std::vector<Deal> const layouts{
        make_split_layout(Three, Two), make_split_layout(Jack, Two), make_split_layout(Three, Three)};
    VectorLayoutSource source(layouts);
    EvaluateOptions const without_counters_options{.sampling = {.sample_size = 2u, .replenish_below = 2u}};
    EvaluateOptions const with_counters_options{
        .collect_counters = true, .sampling = {.sample_size = 2u, .replenish_below = 2u}};

    EvaluationResult const without_counters = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        without_counters_options);
    EvaluationResult const with_counters = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        with_counters_options);

    ASSERT_FALSE(without_counters.error.has_value());
    ASSERT_FALSE(with_counters.error.has_value());
    EXPECT_EQ(without_counters.by_strategy.at(1u).p_make, with_counters.by_strategy.at(1u).p_make);
}

TEST_F(CountersTest, ReplenishmentByDepthMatchesTheHandDerivedVector)
{
    // sample_size = 2 draws one layout of each spade filler at the root
    // (source order: three, jack). East's spade ply (depth 1, still 2
    // layouts, and 2 < replenish_below(2) is false, so no attempt) splits them
    // into two one-layout branches, both at depth 2.
    //
    // The "three" branch: 1 < 2 triggers, wanting 1. Its scan examines all 3
    // source entries: "three, two" is present (1 at() call), "jack, two" lacks
    // the recorded spade (1 at() call, no delta), and "three, three" is new
    // and matches (1 at() call, delta certain). 3 at() calls, 1 layout added,
    // exhausted (end of source).
    //
    // The "jack" branch: 1 < 2 triggers, wanting 1. Its scan also examines all
    // 3: "three, two" and "three, three" fail the recorded-spade check (1 at()
    // call each, no delta), and "jack, two" is present (1 at() call). 3 at()
    // calls, nothing added, exhausted.
    //
    // Both attempts are at depth 2: attempted = 2, succeeded = 1,
    // layouts_added = 1, at_calls = 3 + 3 = 6.
    std::vector<Deal> const layouts{
        make_split_layout(Three, Two), make_split_layout(Jack, Two), make_split_layout(Three, Three)};
    VectorLayoutSource source(layouts);

    EvaluationResult const result = evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/1,
        source,
        strategy(1),
        single_card_defender,
        EvaluateOptions{.collect_counters = true, .sampling = {.sample_size = 2u, .replenish_below = 2u}});

    ASSERT_FALSE(result.error.has_value());
    EvaluationValue const& value = result.by_strategy.at(1u);
    ASSERT_TRUE(value.counters.has_value());
    std::vector<be::DepthReplenishmentStats> const& by_depth = value.counters->replenishment_by_depth;

    // Grown only to the one depth that attempted a scan (depth 2). Depths 0
    // and 1 are zero entries that are never written, as sample_size_by_depth
    // documents for the same reason.
    ASSERT_EQ(by_depth.size(), 3u);
    EXPECT_EQ(by_depth[0].attempted, 0u);
    EXPECT_EQ(by_depth[1].attempted, 0u);

    EXPECT_EQ(by_depth[2].attempted, 2u);
    EXPECT_EQ(by_depth[2].succeeded, 1u);
    EXPECT_EQ(by_depth[2].layouts_added, 1u);
    EXPECT_EQ(by_depth[2].at_calls, 6u);

    // sample_size_by_depth shows the count after replenishment at depth 2:
    // the "three" branch grew to 2 and the "jack" branch stayed at 1. So
    // layout_min is 1, but layout_sum is 3, not the 1 + 1 = 2 before the
    // top-up.
    ASSERT_GT(value.counters->sample_size_by_depth.size(), 2u);
    EXPECT_EQ(value.counters->sample_size_by_depth[2].layout_sum, 3u);
    EXPECT_EQ(value.counters->sample_size_by_depth[2].layout_min, 1u);
}
