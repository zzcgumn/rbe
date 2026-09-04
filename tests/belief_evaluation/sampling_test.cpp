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
using be::RootOptions;
using be::StrategyId;
using be::VectorLayoutSource;
using be::assert_forms_one_belief_node;
using be::assert_pool_matches;
using be::evaluate;
using be::holding;
using be::make_root;
using be::single_card_declarer_play;
using be::single_card_defender;

// Root sampling: make_root scans source from index 0 and takes the first
// (up to) RootOptions::sample_size consistent layouts, rather than every
// one -- the plan's headline change, and the first place anything in this
// module samples. Every fixture below asserts layouts.size() directly:
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
    // side of the boundary the plan calls out explicitly: a cap that is
    // never reached at all, not one that is reached exactly as the source
    // runs out.
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
