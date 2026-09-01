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
