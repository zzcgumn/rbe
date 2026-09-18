// count_uncut_nodes() is new benchmark-only code, not a library change --
// its own correctness needs the same TDD discipline as anything else
// here. Two properties checked, in two different ways: a real evaluate()
// call (tier 1 and tier 2 both live) visits fewer or equal nodes than
// this walker, compared directly against evaluate() on every fixture
// below that has one; and, on a hand-derived case none of those fixtures
// exercises (a declarer-led root), the walker's own count matches a
// hand-counted expected total exactly, independent of evaluate() --
// UncutTreeExpandsEveryLegalRootCardTest below.
//
// **Not a universal law, and not checked against every ladder** --
// found directly, not assumed: the pool and realistic ladders
// (fixtures.hpp) fail this property, because the real evaluator "walks
// the tree in its own order and revisits sibling subtrees"
// (DeclarerStrategy::play's own doxygen) for belief-view
// renormalisation, while this walker is a single linear traversal that
// visits each concrete node exactly once -- see uncut_tree.hpp's own
// doxygen for the fuller explanation. Checked here only against the
// finesse and solver ladders, where no such revisiting was observed to
// occur (an assertion failure here on either would be the signal that
// it started happening there too, not proof it cannot).
#include <cstdint>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/exhaustive_layout_source.hpp>

#include "fixtures.hpp"
#include "strategies.hpp"
#include "uncut_tree.hpp"

namespace be = dds::belief_evaluation;
namespace bench = dds::belief_evaluation::benchmarks;

using be::ExhaustiveLayoutSource;

namespace
{
    auto cut_nodes_visited(bench::RungFixture const& fixture, std::uint64_t seed) -> std::uint64_t
    {
        ExhaustiveLayoutSource const source(
            fixture.root, fixture.declarer, seed, fixture.history, fixture.opening_leader);
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
    ExhaustiveLayoutSource const source(
        fixture.root, fixture.declarer, /*seed=*/1u, fixture.history, fixture.opening_leader);

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
    ExhaustiveLayoutSource const source(
        fixture.root, fixture.declarer, /*seed=*/1u, fixture.history, fixture.opening_leader);

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

// A declarer-led root needs every legal root card expanded, not only
// pi's own chosen one (evaluate.cpp's own root-handling block does this,
// to populate root_children) -- every fixture above leads from a
// defender, so none of them exercise that path at all. A hand-derived
// case, small enough to count by hand rather than compared against
// evaluate() (which the pool/realistic ladder's own sibling-revisiting
// note above already rules out as a source of a fixture this small and
// declarer-led): North (declarer) holds two Spades (Two, Ace) and is on
// lead, needing one trick; every other hand holds nothing, so there is
// exactly one layout and zero defender uncertainty.
//
//   root:                                          1 node
//   North plays the Two (pi's chosen card):         South is on play
//     next and holds nothing -- not is_terminal()   1 node
//     (North still holds the Ace), a structural
//     boundary this walker stops at without cutting
//   North plays the Ace (the *other* legal root
//     card, only reachable by expanding every legal
//     root card, not only pi's chosen one):          1 node
//     same boundary
//
// Total: 3. Confirmed as the red/green pair this fix needed: reverting
// just the "expand every other legal root card" branch (verified by
// hand, not committed) reproduces the bug this test guards -- the count
// comes back 2, missing the Ace branch entirely.
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

// A fixture built so tier 1 provably never fires anywhere in it would
// make the walker's count equal the real one exactly, which is a
// stronger check than >= alone -- but no such fixture exists in either
// ladder checked here (every finesse rung's own declarer needs *every*
// remaining trick, and once one is lost tier 1's is_dead() legitimately
// fires; the solver ladder is built the same way). Left as a >= check
// rather than manufacturing an equality case that would not generalise
// past its own construction.
//
// The pool and realistic ladders are not checked here at all -- see this
// file's own header comment for why the property does not hold for them
// (revisited sibling subtrees, not a bug in either side).
