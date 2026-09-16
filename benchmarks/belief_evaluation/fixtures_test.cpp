// The size guard the ladder's own doxygen promises: a fixture whose size
// drifts silently invalidates every number the tasks after this one
// report, so this is not a courtesy check.
#include <cstdint>
#include <optional>
#include <set>

#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/exhaustive_layout_source.hpp>
#include <belief_evaluation/history_verification.hpp>
#include <belief_evaluation/validation.hpp>

#include "fixtures.hpp"
#include "strategies.hpp"

namespace be = dds::belief_evaluation;
namespace bench = dds::belief_evaluation::benchmarks;

using be::ExhaustiveLayoutSource;
using be::HistoryVerdict;

namespace
{
    auto size_of(bench::RungFixture const& fixture, std::uint64_t seed) -> std::optional<std::uint64_t>
    {
        ExhaustiveLayoutSource const source(
            fixture.root, fixture.declarer, seed, fixture.history, fixture.opening_leader);
        return source.size();
    }
}  // namespace

// --- every rung's own size() matches what it claims, both forms ----------

class FixtureLadderTest : public testing::TestWithParam<bench::Rung>
{
};

TEST_P(FixtureLadderTest, WithoutHistorySizeMatchesExpected)
{
    bench::Rung const rung = GetParam();
    EXPECT_EQ(size_of(rung.without_history, /*seed=*/1u), rung.without_history.expected_size)
        << rung.name << " without history";
}

TEST_P(FixtureLadderTest, WithHistorySizeMatchesExpected)
{
    bench::Rung const rung = GetParam();
    ExhaustiveLayoutSource const source(
        rung.with_history.root, rung.with_history.declarer, /*seed=*/1u, rung.with_history.history,
        rung.with_history.opening_leader);
    ASSERT_EQ(source.history_verdict(), HistoryVerdict::Consistent) << rung.name << " with history";
    EXPECT_EQ(source.size(), rung.with_history.expected_size) << rung.name << " with history";
}

TEST_P(FixtureLadderTest, HistoryFormIsStrictlySmallerThanUnconstrained)
{
    // The pairing exists to compare the two -- if a rung's history did not
    // actually narrow anything, it would not be testing what the plan's
    // background says it must: "the constrained space is smaller and
    // denser in consistent layouts".
    bench::Rung const rung = GetParam();
    ASSERT_TRUE(rung.without_history.expected_size > 0);
    EXPECT_LT(rung.with_history.expected_size, rung.without_history.expected_size) << rung.name;
}

TEST_P(FixtureLadderTest, WithoutHistoryFormAppliesNoConstraintAtAll)
{
    // The without-history form is the same root with history simply
    // omitted, not a separately-constructed "equivalent" position -- so
    // its own verdict is Consistent trivially (nothing to reject), the
    // same guarantee every call site written before the history parameter
    // existed still relies on.
    bench::Rung const rung = GetParam();
    ExhaustiveLayoutSource const source(
        rung.without_history.root, rung.without_history.declarer, /*seed=*/1u);
    EXPECT_EQ(source.history_verdict(), HistoryVerdict::Consistent) << rung.name;
}

INSTANTIATE_TEST_SUITE_P(
    AllRungs, FixtureLadderTest, testing::ValuesIn(bench::all_rungs()),
    [](testing::TestParamInfo<bench::Rung> const& info) { return info.param.name; });

// --- the ladder itself: enough rungs, spanning enough range ---------------

TEST(FixtureLadderShapeTest, AtLeastFiveRungs)
{
    EXPECT_GE(bench::all_rungs().size(), 5u);
}

TEST(FixtureLadderShapeTest, SpansAtLeastTwoOrdersOfMagnitude)
{
    std::vector<bench::Rung> const rungs = bench::all_rungs();
    ASSERT_FALSE(rungs.empty());
    std::uint64_t smallest = rungs.front().without_history.expected_size;
    std::uint64_t largest = rungs.front().without_history.expected_size;
    for (bench::Rung const& rung : rungs)
    {
        smallest = std::min(smallest, rung.without_history.expected_size);
        largest = std::max(largest, rung.without_history.expected_size);
    }
    EXPECT_GE(largest, smallest * 100);
}

TEST(FixtureLadderShapeTest, EveryRungNameIsDistinct)
{
    std::set<std::string> names;
    for (bench::Rung const& rung : bench::all_rungs())
    {
        EXPECT_TRUE(names.insert(rung.name).second) << "duplicate name: " << rung.name;
    }
}

// --- the bottom two rungs are exhaustively evaluable in a test-cycle time
// budget: evaluate() over the *whole* space (no sampling), both forms,
// with strategies.hpp's own scripted, deterministic strategy pair --
// mirroring library/tests/belief_evaluation/test_support.hpp's own
// precedent for why a scripted strategy (not the solver) is enough to
// exercise this. ---------

namespace
{
    auto exhaustively_evaluate(bench::RungFixture const& fixture) -> be::EvaluationResult
    {
        ExhaustiveLayoutSource const source(
            fixture.root, fixture.declarer, /*seed=*/1u, fixture.history, fixture.opening_leader);
        return be::evaluate(
            fixture.root, fixture.declarer, fixture.tricks_needed, source, bench::scripted_strategy(),
            bench::scripted_defender_play);
    }
}  // namespace

class BottomTwoRungsAreExhaustivelyEvaluableTest : public testing::TestWithParam<bench::Rung>
{
};

// be::ProbabilitySumTolerance (1e-6), not a bare 1.0: a Kahan-summed
// p_make over a several-hundred-layout node can land a float epsilon
// above 1.0 (observed on pool6's with-history form, 210 layouts,
// 1.0000000000000002) -- validation.hpp's own tolerance for exactly this
// class of aggregate floating-point slack, not a bespoke one.
TEST_P(BottomTwoRungsAreExhaustivelyEvaluableTest, WithoutHistoryCompletesAndReportsAProbability)
{
    bench::Rung const rung = GetParam();
    be::EvaluationResult const result = exhaustively_evaluate(rung.without_history);
    ASSERT_FALSE(result.error.has_value());
    EXPECT_GE(result.by_strategy.at(1u).p_make, -be::ProbabilitySumTolerance);
    EXPECT_LE(result.by_strategy.at(1u).p_make, 1.0 + be::ProbabilitySumTolerance);
}

TEST_P(BottomTwoRungsAreExhaustivelyEvaluableTest, WithHistoryCompletesAndReportsAProbability)
{
    bench::Rung const rung = GetParam();
    be::EvaluationResult const result = exhaustively_evaluate(rung.with_history);
    ASSERT_FALSE(result.error.has_value());
    EXPECT_GE(result.by_strategy.at(1u).p_make, -be::ProbabilitySumTolerance);
    EXPECT_LE(result.by_strategy.at(1u).p_make, 1.0 + be::ProbabilitySumTolerance);
}

INSTANTIATE_TEST_SUITE_P(
    BottomTwo, BottomTwoRungsAreExhaustivelyEvaluableTest,
    testing::Values(bench::make_pool4_rung(), bench::make_pool5_rung()),
    [](testing::TestParamInfo<bench::Rung> const& info) { return info.param.name; });
