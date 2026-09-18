// The fast correctness guard beside the instrument: only what is
// deterministic. This plan's headline measurements are distributional --
// "the spread is about 0.03" fails on an unlucky run, gets quarantined,
// and stops meaning anything -- so this file asserts none of that. What it
// asserts:
//
//   - the same seed gives the same answer bitwise (both p_make and every
//     counter, not merely p_make);
//   - sample_size >= size() reproduces the exhaustive run's own p_make
//     exactly -- the property that makes sampling a restriction of the
//     exhaustive computation rather than a different one;
//   - the mass-conservation invariants survive real sampling and
//     replenishment on these bigger, less trivial fixtures than the core
//     suite's own small ones -- in the default build, where those asserts
//     are live. In -c opt all 21 are compiled out (-DNDEBUG) and this
//     assertion is vacuous: a passing run there proves nothing about
//     invariants, only that nothing else went wrong. Do not read a green
//     opt run as invariant coverage.
//
// The instrument itself (instrument.cpp) reports the distributional
// numbers and asserts nothing -- that split is deliberate, not an
// oversight; see this plan's own "the plan's first hard problem" for why.
#include <cstdint>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/exhaustive_layout_source.hpp>
#include <belief_evaluation/validation.hpp>

#include "fixtures.hpp"
#include "strategies.hpp"

namespace be = dds::belief_evaluation;
namespace bench = dds::belief_evaluation::benchmarks;

using be::ExhaustiveLayoutSource;

namespace
{
    auto evaluate_fixture(
        bench::RungFixture const& fixture, std::uint64_t seed, be::EvaluateOptions const& options)
        -> be::EvaluationResult
    {
        ExhaustiveLayoutSource const source(
            fixture.root, fixture.declarer, seed, fixture.history, fixture.opening_leader);
        return be::evaluate(
            fixture.root, fixture.declarer, fixture.tricks_needed, source, bench::scripted_strategy(),
            bench::scripted_defender_play, options);
    }

    auto sampling_options(
        std::optional<std::uint64_t> sample_size, std::optional<std::uint64_t> scan_budget,
        std::optional<std::uint64_t> replenish_below) -> be::EvaluateOptions
    {
        be::EvaluateOptions options{};
        options.collect_counters = true;
        options.sampling.sample_size = sample_size;
        options.sampling.scan_budget = scan_budget;
        options.sampling.replenish_below = replenish_below;
        return options;
    }
}  // namespace

// --- same seed gives the same answer bitwise --------------------------

class DeterminismTest : public testing::TestWithParam<bench::Rung>
{
};

TEST_P(DeterminismTest, SampledReplenishingRunIsBitwiseIdenticalAcrossTwoSeparateCalls)
{
    bench::Rung const rung = GetParam();
    be::EvaluateOptions const options =
        sampling_options(/*sample_size=*/6u, /*scan_budget=*/500u, /*replenish_below=*/3u);

    be::EvaluationResult const first = evaluate_fixture(rung.with_history, /*seed=*/11u, options);
    be::EvaluationResult const second = evaluate_fixture(rung.with_history, /*seed=*/11u, options);

    ASSERT_FALSE(first.error.has_value());
    ASSERT_FALSE(second.error.has_value());
    be::EvaluationValue const& a = first.by_strategy.at(bench::scripted_strategy().id);
    be::EvaluationValue const& b = second.by_strategy.at(bench::scripted_strategy().id);

    EXPECT_EQ(a.p_make, b.p_make) << rung.name;
    ASSERT_TRUE(a.counters.has_value());
    ASSERT_TRUE(b.counters.has_value());
    EXPECT_EQ(a.counters->nodes_visited, b.counters->nodes_visited) << rung.name;
    EXPECT_EQ(a.counters->tier1_made_cuts, b.counters->tier1_made_cuts) << rung.name;
    EXPECT_EQ(a.counters->tier1_dead_cuts, b.counters->tier1_dead_cuts) << rung.name;
    EXPECT_EQ(a.counters->tier2_cuts, b.counters->tier2_cuts) << rung.name;
    ASSERT_EQ(a.counters->sample_size_by_depth.size(), b.counters->sample_size_by_depth.size())
        << rung.name;
    for (std::size_t depth = 0; depth < a.counters->sample_size_by_depth.size(); ++depth)
    {
        be::DepthSampleStats const& sa = a.counters->sample_size_by_depth[depth];
        be::DepthSampleStats const& sb = b.counters->sample_size_by_depth[depth];
        EXPECT_EQ(sa.nodes, sb.nodes) << rung.name << " depth " << depth;
        EXPECT_EQ(sa.layout_sum, sb.layout_sum) << rung.name << " depth " << depth;
        EXPECT_EQ(sa.layout_min, sb.layout_min) << rung.name << " depth " << depth;
    }
    ASSERT_EQ(a.counters->replenishment_by_depth.size(), b.counters->replenishment_by_depth.size())
        << rung.name;
    for (std::size_t depth = 0; depth < a.counters->replenishment_by_depth.size(); ++depth)
    {
        be::DepthReplenishmentStats const& ra = a.counters->replenishment_by_depth[depth];
        be::DepthReplenishmentStats const& rb = b.counters->replenishment_by_depth[depth];
        EXPECT_EQ(ra.attempted, rb.attempted) << rung.name << " depth " << depth;
        EXPECT_EQ(ra.succeeded, rb.succeeded) << rung.name << " depth " << depth;
        EXPECT_EQ(ra.layouts_added, rb.layouts_added) << rung.name << " depth " << depth;
        EXPECT_EQ(ra.at_calls, rb.at_calls) << rung.name << " depth " << depth;
    }
}

INSTANTIATE_TEST_SUITE_P(
    AllRungs, DeterminismTest, testing::ValuesIn(bench::all_rungs()),
    [](testing::TestParamInfo<bench::Rung> const& info) { return info.param.name; });

// --- sample_size >= size() reproduces the exhaustive run exactly -------

class SampleAtLeastSizeReproducesExhaustiveTest : public testing::TestWithParam<bench::Rung>
{
};

TEST_P(SampleAtLeastSizeReproducesExhaustiveTest, WithoutHistory)
{
    bench::Rung const rung = GetParam();
    std::uint64_t const size = rung.without_history.expected_size;

    be::EvaluationResult const exhaustive = evaluate_fixture(
        rung.without_history, /*seed=*/5u, sampling_options(std::nullopt, std::nullopt, std::nullopt));
    be::EvaluationResult const sampled = evaluate_fixture(
        rung.without_history, /*seed=*/5u, sampling_options(size, std::nullopt, std::nullopt));

    ASSERT_FALSE(exhaustive.error.has_value());
    ASSERT_FALSE(sampled.error.has_value());
    EXPECT_EQ(
        exhaustive.by_strategy.at(bench::scripted_strategy().id).p_make,
        sampled.by_strategy.at(bench::scripted_strategy().id).p_make)
        << rung.name;
}

TEST_P(SampleAtLeastSizeReproducesExhaustiveTest, WithHistory)
{
    bench::Rung const rung = GetParam();
    std::uint64_t const size = rung.with_history.expected_size;

    be::EvaluationResult const exhaustive = evaluate_fixture(
        rung.with_history, /*seed=*/5u, sampling_options(std::nullopt, std::nullopt, std::nullopt));
    be::EvaluationResult const sampled = evaluate_fixture(
        rung.with_history, /*seed=*/5u, sampling_options(size, std::nullopt, std::nullopt));

    ASSERT_FALSE(exhaustive.error.has_value());
    ASSERT_FALSE(sampled.error.has_value());
    EXPECT_EQ(
        exhaustive.by_strategy.at(bench::scripted_strategy().id).p_make,
        sampled.by_strategy.at(bench::scripted_strategy().id).p_make)
        << rung.name;
}

INSTANTIATE_TEST_SUITE_P(
    AllRungs, SampleAtLeastSizeReproducesExhaustiveTest, testing::ValuesIn(bench::all_rungs()),
    [](testing::TestParamInfo<bench::Rung> const& info) { return info.param.name; });

// --- mass-conservation invariants survive real sampling and
// replenishment on the ladder's own fixtures -- an internal assert firing
// aborts the process (visible as this test crashing), in the default
// build where those asserts are live. Vacuous in -c opt; see this file's
// own header comment. ---------------------------------------------------

class InvariantsSurviveSamplingAndReplenishmentTest : public testing::TestWithParam<bench::Rung>
{
};

TEST_P(InvariantsSurviveSamplingAndReplenishmentTest, WithoutHistory)
{
    bench::Rung const rung = GetParam();
    be::EvaluationResult const result = evaluate_fixture(
        rung.without_history, /*seed=*/13u,
        sampling_options(/*sample_size=*/4u, /*scan_budget=*/1000u, /*replenish_below=*/3u));
    ASSERT_FALSE(result.error.has_value()) << rung.name;
    double const p_make = result.by_strategy.at(bench::scripted_strategy().id).p_make;
    EXPECT_GE(p_make, -be::ProbabilitySumTolerance) << rung.name;
    EXPECT_LE(p_make, 1.0 + be::ProbabilitySumTolerance) << rung.name;
}

TEST_P(InvariantsSurviveSamplingAndReplenishmentTest, WithHistory)
{
    bench::Rung const rung = GetParam();
    be::EvaluationResult const result = evaluate_fixture(
        rung.with_history, /*seed=*/13u,
        sampling_options(/*sample_size=*/4u, /*scan_budget=*/1000u, /*replenish_below=*/3u));
    ASSERT_FALSE(result.error.has_value()) << rung.name;
    double const p_make = result.by_strategy.at(bench::scripted_strategy().id).p_make;
    EXPECT_GE(p_make, -be::ProbabilitySumTolerance) << rung.name;
    EXPECT_LE(p_make, 1.0 + be::ProbabilitySumTolerance) << rung.name;
}

INSTANTIATE_TEST_SUITE_P(
    AllRungs, InvariantsSurviveSamplingAndReplenishmentTest, testing::ValuesIn(bench::all_rungs()),
    [](testing::TestParamInfo<bench::Rung> const& info) { return info.param.name; });

// --- the same three properties, on the finesse ladder -- a materially
// different construction (genuine per-layout uncertainty, several tricks,
// a defender leading) from the pool/realistic rungs above, so its own
// M>=N and determinism properties are confirmed independently rather
// than assumed to carry over. No history form, so no With/Without split
// here. ---------------------------------------------------------------

class FinesseLadderDeterminismTest : public testing::TestWithParam<bench::RungFixture>
{
};

TEST_P(FinesseLadderDeterminismTest, SampledRunIsBitwiseIdenticalAcrossTwoSeparateCalls)
{
    bench::RungFixture const fixture = GetParam();
    be::EvaluateOptions const options =
        sampling_options(/*sample_size=*/4u, /*scan_budget=*/1000u, /*replenish_below=*/3u);

    be::EvaluationResult const first = evaluate_fixture(fixture, /*seed=*/11u, options);
    be::EvaluationResult const second = evaluate_fixture(fixture, /*seed=*/11u, options);

    ASSERT_FALSE(first.error.has_value());
    ASSERT_FALSE(second.error.has_value());
    be::EvaluationValue const& a = first.by_strategy.at(bench::scripted_strategy().id);
    be::EvaluationValue const& b = second.by_strategy.at(bench::scripted_strategy().id);
    std::string const label = "suits=" + std::to_string(fixture.tricks_needed);

    // Every counter, not only p_make -- matching DeterminismTest's own
    // comparison above exactly (this file's own module comment claims
    // "both p_make and every counter, not merely p_make" as a blanket
    // property; this test's own first cut checked only p_make, which
    // left the finesse ladder specifically -- the only ladder with a
    // real dead-cut rate -- untested for a seed-dependent counter
    // regression the pool/realistic version would have caught).
    EXPECT_EQ(a.p_make, b.p_make) << label;
    ASSERT_TRUE(a.counters.has_value());
    ASSERT_TRUE(b.counters.has_value());
    EXPECT_EQ(a.counters->nodes_visited, b.counters->nodes_visited) << label;
    EXPECT_EQ(a.counters->tier1_made_cuts, b.counters->tier1_made_cuts) << label;
    EXPECT_EQ(a.counters->tier1_dead_cuts, b.counters->tier1_dead_cuts) << label;
    EXPECT_EQ(a.counters->tier2_cuts, b.counters->tier2_cuts) << label;
    ASSERT_EQ(a.counters->sample_size_by_depth.size(), b.counters->sample_size_by_depth.size()) << label;
    for (std::size_t depth = 0; depth < a.counters->sample_size_by_depth.size(); ++depth)
    {
        be::DepthSampleStats const& sa = a.counters->sample_size_by_depth[depth];
        be::DepthSampleStats const& sb = b.counters->sample_size_by_depth[depth];
        EXPECT_EQ(sa.nodes, sb.nodes) << label << " depth " << depth;
        EXPECT_EQ(sa.layout_sum, sb.layout_sum) << label << " depth " << depth;
        EXPECT_EQ(sa.layout_min, sb.layout_min) << label << " depth " << depth;
    }
    ASSERT_EQ(a.counters->replenishment_by_depth.size(), b.counters->replenishment_by_depth.size())
        << label;
    for (std::size_t depth = 0; depth < a.counters->replenishment_by_depth.size(); ++depth)
    {
        be::DepthReplenishmentStats const& ra = a.counters->replenishment_by_depth[depth];
        be::DepthReplenishmentStats const& rb = b.counters->replenishment_by_depth[depth];
        EXPECT_EQ(ra.attempted, rb.attempted) << label << " depth " << depth;
        EXPECT_EQ(ra.succeeded, rb.succeeded) << label << " depth " << depth;
        EXPECT_EQ(ra.layouts_added, rb.layouts_added) << label << " depth " << depth;
        EXPECT_EQ(ra.at_calls, rb.at_calls) << label << " depth " << depth;
    }
}

INSTANTIATE_TEST_SUITE_P(
    AllFinesseRungs, FinesseLadderDeterminismTest, testing::ValuesIn(bench::all_finesse_rungs()),
    [](testing::TestParamInfo<bench::RungFixture> const& info) {
        return "suits" + std::to_string(info.param.tricks_needed);
    });

class FinesseLadderSampleAtLeastSizeReproducesExhaustiveTest
    : public testing::TestWithParam<bench::RungFixture>
{
};

TEST_P(FinesseLadderSampleAtLeastSizeReproducesExhaustiveTest, ReproducesExactly)
{
    bench::RungFixture const fixture = GetParam();
    std::uint64_t const size = fixture.expected_size;

    be::EvaluationResult const exhaustive = evaluate_fixture(
        fixture, /*seed=*/5u, sampling_options(std::nullopt, std::nullopt, std::nullopt));
    be::EvaluationResult const sampled =
        evaluate_fixture(fixture, /*seed=*/5u, sampling_options(size, std::nullopt, std::nullopt));

    ASSERT_FALSE(exhaustive.error.has_value());
    ASSERT_FALSE(sampled.error.has_value());
    EXPECT_EQ(
        exhaustive.by_strategy.at(bench::scripted_strategy().id).p_make,
        sampled.by_strategy.at(bench::scripted_strategy().id).p_make)
        << "suits=" << fixture.tricks_needed;
}

INSTANTIATE_TEST_SUITE_P(
    AllFinesseRungs, FinesseLadderSampleAtLeastSizeReproducesExhaustiveTest,
    testing::ValuesIn(bench::all_finesse_rungs()),
    [](testing::TestParamInfo<bench::RungFixture> const& info) {
        return "suits" + std::to_string(info.param.tricks_needed);
    });

class FinesseLadderInvariantsSurviveSamplingAndReplenishmentTest
    : public testing::TestWithParam<bench::RungFixture>
{
};

TEST_P(FinesseLadderInvariantsSurviveSamplingAndReplenishmentTest, Holds)
{
    bench::RungFixture const fixture = GetParam();
    be::EvaluationResult const result = evaluate_fixture(
        fixture, /*seed=*/13u,
        sampling_options(/*sample_size=*/4u, /*scan_budget=*/1000u, /*replenish_below=*/3u));
    ASSERT_FALSE(result.error.has_value()) << "suits=" << fixture.tricks_needed;
    double const p_make = result.by_strategy.at(bench::scripted_strategy().id).p_make;
    EXPECT_GE(p_make, -be::ProbabilitySumTolerance) << "suits=" << fixture.tricks_needed;
    EXPECT_LE(p_make, 1.0 + be::ProbabilitySumTolerance) << "suits=" << fixture.tricks_needed;
}

INSTANTIATE_TEST_SUITE_P(
    AllFinesseRungs, FinesseLadderInvariantsSurviveSamplingAndReplenishmentTest,
    testing::ValuesIn(bench::all_finesse_rungs()),
    [](testing::TestParamInfo<bench::RungFixture> const& info) {
        return "suits" + std::to_string(info.param.tricks_needed);
    });
