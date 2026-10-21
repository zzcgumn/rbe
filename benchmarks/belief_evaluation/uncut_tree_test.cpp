// count_uncut_nodes() is benchmark-only code and needs its own tests. Two
// properties, checked two ways:
// - a real evaluate() call, with both tiers live, visits no more nodes
//   than the walker. Compared against evaluate() on every fixture below.
// - on a hand-counted declarer-led root, which no fixture has, the walker's
//   count matches exactly (UncutTreeExpandsEveryLegalRootCardTest).
//
// **The first property does not hold on every ladder.** The pool and
// realistic ladders fail it: the real evaluator revisits sibling subtrees
// (see DeclarerStrategy::play), while the walker visits each node once
// (see uncut_tree.hpp). It is checked only on the finesse and solver
// ladders, where no revisiting was seen. A failure there would mean
// revisiting has started there too.
#include <cstdint>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/exhaustive_layout_source.hpp>
#include <belief_evaluation/play_record.hpp>

#include "fixtures.hpp"
#include "strategies.hpp"
#include "uncut_tree.hpp"

namespace be = dds::belief_evaluation;
namespace bench = dds::belief_evaluation::benchmarks;

using be::ExhaustiveLayoutSource;
using be::PlayRecord;

namespace
{
    auto record_of(bench::RungFixture const& fixture) -> PlayRecord
    {
        return *PlayRecord::create(fixture.history, fixture.opening_leader).first;
    }

    auto cut_nodes_visited(bench::RungFixture const& fixture, std::uint64_t seed) -> std::uint64_t
    {
        ExhaustiveLayoutSource const source(fixture.root, fixture.declarer, seed, record_of(fixture));
        be::EvaluateOptions options{};
        options.collect_counters = true;
        be::EvaluationResult const result = be::evaluate(
            fixture.root, fixture.declarer, fixture.tricks_needed, source, bench::scripted_strategy(),
            bench::scripted_defender_play, options);
        return result.by_strategy.at(bench::scripted_strategy().id).counters->nodes_visited;
    }
}  // namespace

class UncutTreeIsAtLeastAsLargeAsTheCutOneTest : public testing::TestWithParam<bench::RungFixture>
{
};

TEST_P(UncutTreeIsAtLeastAsLargeAsTheCutOneTest, Holds)
{
    bench::RungFixture const fixture = GetParam();
    ExhaustiveLayoutSource const source(fixture.root, fixture.declarer, /*seed=*/1u, record_of(fixture));

    std::optional<std::uint64_t> const uncut = bench::count_uncut_nodes(
        fixture.root, fixture.declarer, fixture.tricks_needed, source, bench::scripted_strategy(),
        bench::scripted_defender_play);
    ASSERT_TRUE(uncut.has_value()) << "suits=" << fixture.tricks_needed;

    std::uint64_t const cut = cut_nodes_visited(fixture, /*seed=*/1u);
    EXPECT_GE(*uncut, cut) << "suits=" << fixture.tricks_needed;
}

INSTANTIATE_TEST_SUITE_P(
    AllFinesseRungs, UncutTreeIsAtLeastAsLargeAsTheCutOneTest, testing::ValuesIn(bench::all_finesse_rungs()),
    [](testing::TestParamInfo<bench::RungFixture> const& info) {
        return "suits" + std::to_string(info.param.tricks_needed);
    });

class UncutTreeIsAtLeastAsLargeAsTheCutOneSolverLadderTest
    : public testing::TestWithParam<bench::RungFixture>
{
};

TEST_P(UncutTreeIsAtLeastAsLargeAsTheCutOneSolverLadderTest, Holds)
{
    bench::RungFixture const fixture = GetParam();
    ExhaustiveLayoutSource const source(fixture.root, fixture.declarer, /*seed=*/1u, record_of(fixture));

    std::optional<std::uint64_t> const uncut = bench::count_uncut_nodes(
        fixture.root, fixture.declarer, fixture.tricks_needed, source, bench::scripted_strategy(),
        bench::scripted_defender_play);
    ASSERT_TRUE(uncut.has_value()) << "tricks_needed=" << fixture.tricks_needed;

    std::uint64_t const cut = cut_nodes_visited(fixture, /*seed=*/1u);
    EXPECT_GE(*uncut, cut) << "tricks_needed=" << fixture.tricks_needed << " uncut=" << *uncut
                           << " cut=" << cut;
}

INSTANTIATE_TEST_SUITE_P(
    SolverRungs, UncutTreeIsAtLeastAsLargeAsTheCutOneSolverLadderTest,
    testing::Values(bench::make_solver_rung_a(), bench::make_solver_rung_b()),
    [](testing::TestParamInfo<bench::RungFixture> const& info) {
        return "tricks" + std::to_string(info.param.tricks_needed);
    });

// A declarer-led root needs every legal root card expanded, not only pi's
// choice (evaluate.cpp does this to fill root_children). Every fixture
// above has a defender on lead, so none exercises this. This case is
// counted by hand, not compared with evaluate(), because of the revisiting
// noted above. North (declarer) holds the Two and Ace of Spades, is on
// lead and needs one trick. Every other hand is empty, so there is one
// layout and no defender uncertainty.
//
//   root:                                          1 node
//   North plays the Two (pi's chosen card):         South is on play
//     next and holds nothing. Not is_terminal()     1 node
//     (North still holds the Ace), but a
//     structural boundary the walker stops at
//   North plays the Ace (the other legal root
//     card, reached only by expanding every legal
//     root card):                                   1 node
//     same boundary
//
// Total: 3. Without the "expand every other legal root card" branch, the
// count is 2: the Ace branch is missing.
TEST(UncutTreeExpandsEveryLegalRootCardTest, DeclarerLedRootCountsAllRootAlternatives)
{
    constexpr int Spades = 0;
    constexpr int North = 0;

    Deal root{};
    root.trump = DDS_NOTRUMP;
    root.first = North;
    root.remainCards[North][Spades] = (1u << 2) | (1u << 14);

    ExhaustiveLayoutSource const source(root, North, /*seed=*/1u);
    std::optional<std::uint64_t> const uncut = bench::count_uncut_nodes(
        root, North, /*tricks_needed=*/1, source, bench::scripted_strategy(), bench::scripted_defender_play);
    ASSERT_TRUE(uncut.has_value());
    EXPECT_EQ(*uncut, 3u);
}

// A fixture where tier 1 never fires would make the counts equal, a
// stronger check than >=. Neither ladder here has one: every finesse
// declarer needs every remaining trick, so is_dead() fires once a trick is
// lost, and the solver ladder is built the same way. So this stays a >=
// check.
//
// The pool and realistic ladders are not checked; see the header comment.
