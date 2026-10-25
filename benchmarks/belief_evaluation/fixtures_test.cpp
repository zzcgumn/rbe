// The size guard for the fixture ladder. If a fixture's size drifts, every
// benchmark number measured on it is silently wrong.
#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <api/solve_board.hpp>
#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/exhaustive_layout_source.hpp>
#include <belief_evaluation/history_verification.hpp>
#include <belief_evaluation/play_record.hpp>
#include <belief_evaluation/validation.hpp>
#include <solver_context/solver_context.hpp>

#include "fixtures.hpp"
#include "strategies.hpp"

namespace be = dds::belief_evaluation;
namespace bench = dds::belief_evaluation::benchmarks;

using be::ExhaustiveLayoutSource;
using be::HistoryVerdict;
using be::PlayRecord;

namespace
{
    auto size_of(bench::RungFixture const& fixture, std::uint64_t seed) -> std::optional<std::uint64_t>
    {
        PlayRecord const record = *PlayRecord::create(fixture.history, fixture.opening_leader).first;
        ExhaustiveLayoutSource const source(fixture.root, fixture.declarer, seed, record);
        return source.size();
    }
}  // namespace

// --- every rung's size() matches its claim, in both forms ---------------

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
    PlayRecord const record =
        *PlayRecord::create(rung.with_history.history, rung.with_history.opening_leader).first;
    ExhaustiveLayoutSource const source(
        rung.with_history.root, rung.with_history.declarer, /*seed=*/1u, record);
    ASSERT_EQ(source.history_verdict(), HistoryVerdict::Consistent) << rung.name << " with history";
    EXPECT_EQ(source.size(), rung.with_history.expected_size) << rung.name << " with history";
}

TEST_P(FixtureLadderTest, HistoryFormIsStrictlySmallerThanUnconstrained)
{
    // The pairing exists to compare the two forms. If the history narrowed
    // nothing, the rung would not have the property it is built for: a
    // smaller constrained space, denser in consistent layouts.
    bench::Rung const rung = GetParam();
    ASSERT_TRUE(rung.without_history.expected_size > 0);
    EXPECT_LT(rung.with_history.expected_size, rung.without_history.expected_size) << rung.name;
}

TEST_P(FixtureLadderTest, WithoutHistoryFormAppliesNoConstraintAtAll)
{
    // The without-history form is the same root with the history omitted,
    // not a separately built position. So its verdict is trivially
    // Consistent, as for every caller that passes no history.
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

// --- The bottom two rungs are exhaustively evaluable within a test-cycle
// budget: evaluate() over the whole space, both forms, with the scripted
// strategies of strategies.hpp. As in tests/belief_evaluation/
// test_support.hpp, a scripted strategy is enough; no solver is needed.
// ---------

namespace
{
    auto exhaustively_evaluate(bench::RungFixture const& fixture) -> be::EvaluationResult
    {
        PlayRecord const record = *PlayRecord::create(fixture.history, fixture.opening_leader).first;
        ExhaustiveLayoutSource const source(fixture.root, fixture.declarer, /*seed=*/1u, record);
        return be::evaluate(
            fixture.root, fixture.declarer, fixture.tricks_needed, source, bench::scripted_strategy(),
            bench::scripted_defender_play);
    }
}  // namespace

class BottomTwoRungsAreExhaustivelyEvaluableTest : public testing::TestWithParam<bench::Rung>
{
};

// be::ProbabilitySumTolerance (1e-6), not a bare 1.0. A Kahan-summed
// p_make over a few hundred layouts can land a float epsilon away from
// 1.0. One run of pool6 with history (210 layouts) gave
// 1.0000000000000002; other platforms and builds may differ. validation.hpp
// defines this tolerance for exactly this kind of slack.
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

// --- The finesse ladder: size, and real (non-degenerate) uncertainty.
// Unlike the pool and realistic rungs, whose p_make is exactly 1 in every
// layout, these fixtures exist because their p_make is not trivial. A
// convergence study needs something to converge on. All four are also
// exhaustively evaluable within a test-cycle budget (finesse4, the
// largest, measured under 20ms). -------------------------------------

TEST(FinesseLadderTest, SizeMatchesExpected)
{
    for (bench::RungFixture const& fixture : bench::all_finesse_rungs())
    {
        EXPECT_EQ(size_of(fixture, /*seed=*/1u), fixture.expected_size)
            << "suits=" << fixture.tricks_needed;
    }
}

TEST(FinesseLadderTest, SpansAtLeastOneOrderOfMagnitude)
{
    std::vector<bench::RungFixture> const rungs = bench::all_finesse_rungs();
    ASSERT_FALSE(rungs.empty());
    std::uint64_t const smallest = rungs.front().expected_size;
    std::uint64_t const largest = rungs.back().expected_size;
    ASSERT_LT(smallest, largest) << "expects all_finesse_rungs() in ascending N order";
    EXPECT_GE(largest, smallest * 10);
}

TEST(FinesseLadderTest, EveryRungExhaustivelyEvaluatesToAGenuinelyNonDegenerateProbability)
{
    // Strictly between 0 and 1, with margin. A plain [0, 1] check would also
    // accept the pool ladder's p_make == 1. An edit that makes this ladder
    // degenerate again (declarer always wins, or always loses) fails here,
    // not silently in a convergence sweep.
    for (bench::RungFixture const& fixture : bench::all_finesse_rungs())
    {
        be::EvaluationResult const result = exhaustively_evaluate(fixture);
        ASSERT_FALSE(result.error.has_value()) << "suits=" << fixture.tricks_needed;
        double const p_make = result.by_strategy.at(1u).p_make;
        EXPECT_GT(p_make, 0.05) << "suits=" << fixture.tricks_needed;
        EXPECT_LT(p_make, 0.95) << "suits=" << fixture.tricks_needed;
    }
}

// --- The solver ladder: size, and that solve_board accepts it. That is why
// the ladder exists; fixtures.hpp explains the two traps. ---------------

TEST(SolverLadderTest, SizeMatchesExpected)
{
    for (bench::RungFixture const& fixture : {bench::make_solver_rung_a(), bench::make_solver_rung_b()})
    {
        EXPECT_EQ(size_of(fixture, /*seed=*/1u), fixture.expected_size)
            << "tricks_needed=" << fixture.tricks_needed;
    }
}

TEST(SolverLadderTest, SolveBoardAcceptsTheRootLayout)
{
    SolverContext ctx;
    for (bench::RungFixture const& fixture : {bench::make_solver_rung_a(), bench::make_solver_rung_b()})
    {
        FutureTricks fut{};
        int const status = solve_board(ctx, fixture.root, /*target=*/-1, /*solutions=*/2, /*mode=*/0, &fut);
        EXPECT_EQ(status, RETURN_NO_FAULT) << "tricks_needed=" << fixture.tricks_needed;
    }
}
