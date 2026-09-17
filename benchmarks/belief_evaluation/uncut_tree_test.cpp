// count_uncut_nodes() is new benchmark-only code, not a library change --
// its own correctness needs the same TDD discipline as anything else
// here. The one property it must have, always: a real evaluate() call
// (tier 1 and tier 2 both live) can only visit fewer or equal nodes than
// this walker (both tiers, and only they, are removed here) -- never
// more. A cut can only shrink a tree, never grow it.
#include <cstdint>
#include <optional>

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

// A fixture built so tier 1 provably never fires anywhere in it would
// make the walker's count equal the real one exactly, which is a
// stronger check than >= alone -- but no such fixture exists in this
// file's own ladder (every finesse rung's own declarer needs *every*
// remaining trick, and once one is lost tier 1's is_dead() legitimately
// fires). Left as a >= check rather than manufacturing an equality case
// that would not generalise past its own construction.
