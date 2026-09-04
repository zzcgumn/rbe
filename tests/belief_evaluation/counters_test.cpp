#include <gtest/gtest.h>

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
using be::StrategyId;
using be::VectorLayoutSource;
using be::evaluate;
using be::holding;
using be::single_card_declarer_play;
using be::single_card_defender;

// The module's instrumentation mechanism: EvaluationCounters, populated only
// behind EvaluateOptions::collect_counters, and today carrying nothing more
// than nodes_visited -- there are no cuts yet for it to report on.

namespace
{
    constexpr int Two = 2;
    constexpr int Three = 3;
    constexpr int Jack = 11;
    constexpr int Ace = 14;

    constexpr int Spades = 0;

    constexpr int North = 0;  // declarer
    constexpr int East = 1;   // a defender
    constexpr int South = 2;  // dummy
    constexpr int West = 3;   // a defender

    /// A single full trick, exactly one legal card at every seat's turn:
    /// North's ace always wins regardless of what anyone plays, so the
    /// whole recursion has one path through it and node count is fully
    /// determined by the fixture's shape rather than by any strategy.
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

    // The tree (single layout, exactly one legal card at every step, so
    // there is exactly one path and every node below the root has exactly
    // one child):
    //   root: North to lead, declarer node                       -- node 1
    //     North plays SA -> East to play, defender node           -- node 2
    //       East plays S2 -> South to play, declarer/dummy node   -- node 3
    //         South plays S3 -> West to play, defender node       -- node 4
    //           West plays SJ -> trick resolves, hand empty,
    //                            terminal, not expanded further    -- node 5
    // nodes_visited counts every node reached and evaluated for a value,
    // terminal or not, including the root -- 5 total.
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

// This is the check that would otherwise go unenforced silently: every
// test either turns counters on or leaves them off, never both, so nothing
// forces the two runs to actually agree unless a test does so directly.
// Bitwise, not EXPECT_DOUBLE_EQ -- a counters flag that perturbs the last
// bit of p_make is exactly the kind of bug this test exists to catch.
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

// tier1_made_cuts, tier1_dead_cuts and tier2_cuts, alongside nodes_visited:
// each counts its own tier's cut and no other's. This fixture's own
// already-made cut (see NodesVisitedMatchesTheHandCountedTree's hand-counted
// tree above -- node 5, the instant tricks_won_by_declarer first reaches
// tricks_needed) fires through p_make()'s own write site, not the mirrored
// one in evaluate()'s root-handling block, since the root here (node 1) has
// not yet won any tricks. Neither of the other two cuts is ever in a
// position to fire on this fixture: nothing here is ever dead, and no bound
// is supplied. cuts_test.cpp's DeadCutTest has the "vice versa" half of this
// proof (a fixture where the dead cut fires and the made cut does not), and
// its own tests for both cuts firing through evaluate()'s root-handling
// block instead of p_make() -- the site a sweep of p_make() alone would
// miss.
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

// sample_size_by_depth: a per-depth aggregate of how many layouts a node
// held, root at depth 0 -- the number that says how fast a sample
// collapses as defenders play.

TEST_F(CountersTest, SampleSizeByDepthMatchesTheHandCountedTreeOnTheExhaustivePath)
{
    // Same fixture, same hand-counted tree as
    // NodesVisitedMatchesTheHandCountedTree above -- one layout, one path,
    // five nodes at depths 0 through 4 (root at depth 0; the root's own
    // contribution is written from evaluate()'s root-handling block, not
    // from p_make(), the same asymmetry nodes_visited and the tier
    // counters already have). Every node holds
    // exactly the fixture's single layout throughout, so every depth's
    // entry is {nodes=1, layout_sum=1, layout_min=1} -- asserted per field,
    // per depth, not just the vector's length or its last element: an
    // aggregate this uniform could hide an off-by-one in which depth a
    // node landed at just as easily as it could hide a wrong count.
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

    /// North holds a certain trick (Ace/King of spades) regardless of what
    /// anyone else plays; South (dummy) holds two low spades unrelated to
    /// any layout's identity (Five, Six); East holds exactly one of a
    /// three-card pool {Two, Three, Four}, West the other two -- so three
    /// layouts share the same union pool (and so are all mutually
    /// consistent, surviving make_root's filter together) but each gives
    /// East a genuinely different single legal card. East on lead, forced
    /// (single_card_defender): each layout's own card is the only one it
    /// has, so expand_defender_node's own grouping-by-card splits the
    /// three layouts into three distinct singleton children the instant
    /// East plays -- the collapse this file exists to measure, made to
    /// happen predictably rather than left to a solver or a script.
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
    // Three consistent layouts exist (make_layout_with_easts_card(Two),
    // (Three), (Four)); sample_size = 2 draws the first two in source
    // order (Two then Three) -- a genuine sample, is_sample true at the
    // root. East's forced play immediately splits those two sampled
    // layouts apart (see make_layout_with_easts_card's own comment): by
    // depth 1 there are two singleton nodes, one per branch, and nothing
    // further splits them (South's and West's own plays are each forced
    // too, within a branch already down to one layout). North's ace/king
    // then wins trick 1 in both branches, meeting tricks_needed = 1, so
    // the already-made cut fires at depth 4 in both branches (the same
    // node-4-away-from-root shape NodesVisitedMatchesTheHandCountedTree's
    // fixture has) -- depths 1 through 4 all read {nodes=2, layout_sum=2,
    // layout_min=1}, depth 0 alone reads {nodes=1, layout_sum=2,
    // layout_min=2}. The collapse this counter exists to measure is
    // exactly that depth-0-to-depth-1 drop, read directly off the vector.
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
        EvaluateOptions{.collect_counters = true, .sample_size = 2u});

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
