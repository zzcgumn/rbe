#include <gtest/gtest.h>

#include <array>
#include <initializer_list>
#include <vector>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>
#include <api/solve_board.hpp>
#include <solver_context/solver_context.hpp>

#include <belief_evaluation/double_dummy_bound.hpp>
#include <belief_evaluation/double_dummy_defender.hpp>
#include <belief_evaluation/evaluate.hpp>

#include "test_support.hpp"

// This file builds a raw SolverContext for the orientation tests, so it
// sees both ::Card from api/dds.h and dds::belief_evaluation::Card. The two
// are layout-identical, so an unqualified `Card` would compile as either
// with no error. Qualifying through this alias, not `using namespace`,
// keeps every module type unambiguous.
namespace be = dds::belief_evaluation;

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

    constexpr int North = 0;  // declarer throughout this file
    constexpr int East = 1;   // a defender
    constexpr int South = 2;  // dummy
    constexpr int West = 3;   // a defender
}

class DoubleDummyBoundTest : public ::testing::Test
{
};

// --- what solve_board's raw score measures ----------------------------------
//
// Measured against the solver, not assumed. A one-trick ending with one
// card per hand, North (declarer) holding the ace: declarer takes 1 trick
// whoever leads, but who is on lead changes what score[0] reports.
//
// One suit with one card per hand is enough here. The fixtures below need
// a second, filler suit (see the note on
// make_declarer_wins_exactly_half_the_tricks).

TEST_F(DoubleDummyBoundTest, ScoreIsForTheSideOnLeadNotForDeclarerAbsolutely)
{
    auto const solve_first = [](int first) -> int
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = first;
        deal.remainCards[North][Spades] = be::holding({Ace});
        deal.remainCards[East][Spades] = be::holding({Two});
        deal.remainCards[South][Spades] = be::holding({Three});
        deal.remainCards[West][Spades] = be::holding({Four});
        SolverContext ctx;
        FutureTricks fut{};
        int const status = solve_board(ctx, deal, /*target=*/-1, /*solutions=*/1, /*mode=*/0, &fut);
        EXPECT_EQ(status, RETURN_NO_FAULT);
        return fut.score[0];
    };

    // North (declarer) on lead: declarer's side is on lead, so both
    // hypotheses ("declarer's tricks" and "the leading side's tricks") say 1.
    // Expected, but not a disambiguator.
    EXPECT_EQ(solve_first(North), 1);

    // East (a defender) on lead, same holdings: declarer's ace still wins, so
    // "declarer's tricks" would still say 1, but "the leading side's tricks"
    // says 0. The result tells the two apart.
    EXPECT_EQ(solve_first(East), 0);
}

namespace
{
    /// East holds the top two spades, North (declarer) and South (dummy) the
    /// bottom two, and West the middle two, so East wins both spade tricks.
    /// North holds the top two clubs and wins both club tricks. Four tricks,
    /// split evenly: declarer's double-dummy bound is exactly 2, a real
    /// in-between value.
    ///
    /// The second suit matters. One measurement of a single suit with more
    /// than one card per hand gave a negative score, while the same holdings
    /// with a filler suit solved correctly. (The "not evaluated" notes below
    /// show that this is not the whole story.) Every fixture here with more
    /// than one card per hand carries a second suit.
    auto make_declarer_wins_exactly_half_the_tricks() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[East][Spades] = be::holding({Ace, King});
        deal.remainCards[West][Spades] = be::holding({Queen, Jack});
        deal.remainCards[North][Spades] = be::holding({Two, Three});
        deal.remainCards[South][Spades] = be::holding({Four, Five});
        deal.remainCards[North][Clubs] = be::holding({Ace, King});
        deal.remainCards[South][Clubs] = be::holding({Queen, Jack});
        deal.remainCards[East][Clubs] = be::holding({Two, Three});
        deal.remainCards[West][Clubs] = be::holding({Four, Five});
        return deal;
    }

    /// North (declarer) holds the top two spades and the top two clubs: four
    /// certain tricks, whoever leads. The clubs are the filler suit described
    /// above; North would win the spade tricks the same way without them.
    auto make_north_wins_every_trick() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = North;
        deal.remainCards[North][Spades] = be::holding({Ace, King});
        deal.remainCards[East][Spades] = be::holding({Queen, Jack});
        deal.remainCards[South][Spades] = be::holding({Two, Three});
        deal.remainCards[West][Spades] = be::holding({Four, Five});
        deal.remainCards[North][Clubs] = be::holding({Ace, King});
        deal.remainCards[East][Clubs] = be::holding({Queen, Jack});
        deal.remainCards[South][Clubs] = be::holding({Two, Three});
        deal.remainCards[West][Clubs] = be::holding({Four, Five});
        return deal;
    }
}

// --- hand-checkable positions ------------------------------------------

TEST_F(DoubleDummyBoundTest, ADeclarerWinOutrightBoundsAtTheFullTrickCount)
{
    Deal const layout = make_north_wins_every_trick();
    SolverContext ctx;
    be::DoubleDummyBound provider(ctx, North);
    be::LayoutBound const bound = provider.as_bound();

    EXPECT_EQ(bound(layout), 4);
}

TEST_F(DoubleDummyBoundTest, ADeclarerLossOutrightBoundsAtZero)
{
    // The shape of make_north_wins_every_trick(), but East holds every honour
    // in both suits, so declarer takes no trick.
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = North;
    deal.remainCards[East][Spades] = be::holding({Ace, King});
    deal.remainCards[West][Spades] = be::holding({Queen, Jack});
    deal.remainCards[North][Spades] = be::holding({Two, Three});
    deal.remainCards[South][Spades] = be::holding({Four, Five});
    deal.remainCards[East][Clubs] = be::holding({Ace, King});
    deal.remainCards[West][Clubs] = be::holding({Queen, Jack});
    deal.remainCards[North][Clubs] = be::holding({Two, Three});
    deal.remainCards[South][Clubs] = be::holding({Four, Five});

    SolverContext ctx;
    be::DoubleDummyBound provider(ctx, North);
    be::LayoutBound const bound = provider.as_bound();

    EXPECT_EQ(bound(deal), 0);
}

TEST_F(DoubleDummyBoundTest, APositionInBetweenBoundsAtExactlyHalf)
{
    Deal const layout = make_declarer_wins_exactly_half_the_tricks();
    SolverContext ctx;
    be::DoubleDummyBound provider(ctx, North);
    be::LayoutBound const bound = provider.as_bound();

    EXPECT_EQ(bound(layout), 2);
}

// --- the orientation conversion, exercised end to end ----------------------

TEST_F(DoubleDummyBoundTest, ADefenderOnLeadStillReportsDeclarersOwnBound)
{
    // make_north_wins_every_trick(), led from East (a defender). The
    // orientation conversion (tricks_remaining(layout) minus the raw score)
    // must still give declarer's bound of 4, not the defenders' raw 0.
    Deal layout = make_north_wins_every_trick();
    layout.first = East;

    SolverContext ctx;
    be::DoubleDummyBound provider(ctx, North);
    be::LayoutBound const bound = provider.as_bound();

    EXPECT_EQ(bound(layout), 4);
}

// --- a solver failure surfaces as the sentinel, not silently ---------------

TEST_F(DoubleDummyBoundTest, AMalformedLayoutSurfacesAsTheSentinelNotARealBound)
{
    // An empty Deal (no cards, first = North by zero-init) is not a legal
    // position, so solve_board should reject it with a non-zero status, the
    // same failure DoubleDummyDefender's tests use.
    Deal const empty_deal{};
    SolverContext ctx;
    be::DoubleDummyBound provider(ctx, North);
    be::LayoutBound const bound = provider.as_bound();

    EXPECT_EQ(bound(empty_deal), 14);
}

// --- tier 1 and tier 2 together, against a delta that satisfies
// EvaluateOptions::delta_is_double_dummy_optimal. The solver-free
// counterpart is in reproduction_test.cpp. DoubleDummyDefender with
// DoubleDummyBound, on one SolverContext, is the sound configuration
// DoubleDummyBound documents.

namespace
{
    constexpr int Six = 6;
    constexpr int Seven = 7;
    constexpr int Eight = 8;
    constexpr int Nine = 9;

    /// North (declarer) holds only the spade queen and jack; East and West
    /// hold the ace and king, so declarer never wins a spade trick. The club
    /// filler (one card each, never a choice) also goes to West, the highest.
    /// So declarer's double-dummy value over the three-trick ending is 0.
    ///
    /// Tier 1's dead cut cannot see this. At the root declarer holds 3 cards,
    /// well above tricks_needed (1): tier 1 counts cards, not winners. Only a
    /// real double-dummy bound catches it, which is the point of the
    /// fixture.
    auto make_declarer_never_wins_a_trick() -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = East;
        deal.remainCards[North][Spades] = be::holding({Queen, Jack});
        deal.remainCards[South][Spades] = be::holding({Two, Three});
        deal.remainCards[East][Spades] = be::holding({Ace, Four});
        deal.remainCards[West][Spades] = be::holding({King, Five});
        deal.remainCards[North][Clubs] = be::holding({Six});
        deal.remainCards[South][Clubs] = be::holding({Seven});
        deal.remainCards[East][Clubs] = be::holding({Eight});
        deal.remainCards[West][Clubs] = be::holding({Nine});
        return deal;
    }
}

TEST_F(DoubleDummyBoundTest, ReproductionRunBTierOneAndTwoTogetherAgainstAQualifyingDelta)
{
    Deal const layout = make_declarer_never_wins_a_trick();
    be::VectorLayoutSource source({layout});
    SolverContext ctx;
    be::DoubleDummyDefender defender(ctx);
    be::DoubleDummyBound provider(ctx, North);
    be::DeclarerStrategy const pi{
        .id = 1, .play = be::single_card_declarer_play, .state_key = nullptr};

    // Baseline: tier 1 only. No bound is supplied, so tier2_dead() never
    // fires. The true value, 0.0, comes from full recursion through both
    // tricks.
    be::EvaluationResult const tier1_only = be::evaluate(
        layout,
        North,
        /*tricks_needed=*/1,
        source,
        pi,
        defender.as_strategy(),
        be::EvaluateOptions{.collect_counters = true});

    // Tier 1 and 2 together. DoubleDummyDefender satisfies
    // delta_is_double_dummy_optimal (target = -1 maximises tricks for both
    // sides; see EvaluateOptions::delta_is_double_dummy_optimal), so this is
    // the sound pairing.
    be::EvaluationResult const tier1_and_2 = be::evaluate(
        layout,
        North,
        /*tricks_needed=*/1,
        source,
        pi,
        defender.as_strategy(),
        be::EvaluateOptions{
            .collect_counters = true,
            .bound = provider.as_bound(),
            .delta_is_double_dummy_optimal = true});

    ASSERT_FALSE(tier1_only.error.has_value());
    ASSERT_FALSE(tier1_and_2.error.has_value());
    // Bitwise: both exactly 0.0, whichever tiers are active.
    EXPECT_EQ(tier1_only.by_strategy.at(1u).p_make, tier1_and_2.by_strategy.at(1u).p_make);
    EXPECT_EQ(tier1_only.by_strategy.at(1u).p_make, 0.0);

    // Tier 2 did work tier 1 could not: fewer nodes visited, because the
    // bound catches the all-dead position at the root, where tier 1's dead
    // cut cannot (see the fixture comment).
    ASSERT_TRUE(tier1_only.by_strategy.at(1u).counters.has_value());
    ASSERT_TRUE(tier1_and_2.by_strategy.at(1u).counters.has_value());
    EXPECT_LT(
        tier1_and_2.by_strategy.at(1u).counters->nodes_visited,
        tier1_only.by_strategy.at(1u).counters->nodes_visited);
    // Fires at the root: exactly 1 node visited with tier 2 enabled.
    EXPECT_EQ(tier1_and_2.by_strategy.at(1u).counters->nodes_visited, 1u);
}

TEST_F(DoubleDummyBoundTest, TierTwoCutRateUnderSamplingIsExactlyZeroBecauseOfTheGateNotByAccident)
{
    // The fixture, bound and defender of the run above, but with more than
    // one consistent layout in the source, so a sample_size can bind. Five
    // identical copies of make_declarer_never_wins_a_trick(). Duplicates are
    // fine: is_consistent compares each candidate with the root layout, and a
    // copy matches. DoubleDummyBound gives 0 tricks for each, since the spades
    // are the same in every copy.
    //
    // A zero tier-2 count proves nothing by itself: no measurement, no bound
    // or a bound that never fires would all give it. Here the unsampled run
    // fires the cut with this bound and delta (tier2_cuts >= 1), and the
    // sampled run, with the same bound, delta and layouts, fires it zero
    // times. The only difference is is_sample.
    std::vector<Deal> layouts;
    for (int i = 0; i < 5; ++i)
    {
        layouts.push_back(make_declarer_never_wins_a_trick());
    }
    be::assert_pool_matches(layouts);
    be::assert_forms_one_belief_node(layouts, North);
    be::VectorLayoutSource source(layouts);
    SolverContext ctx;
    be::DoubleDummyDefender defender(ctx);
    be::DoubleDummyBound provider(ctx, North);
    be::DeclarerStrategy const pi{
        .id = 1, .play = be::single_card_declarer_play, .state_key = nullptr};

    be::EvaluationResult const unsampled = be::evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/1,
        source,
        pi,
        defender.as_strategy(),
        be::EvaluateOptions{
            .collect_counters = true,
            .bound = provider.as_bound(),
            .delta_is_double_dummy_optimal = true});
    ASSERT_FALSE(unsampled.error.has_value());
    ASSERT_TRUE(unsampled.by_strategy.at(1u).counters.has_value());
    EXPECT_GT(unsampled.by_strategy.at(1u).counters->tier2_cuts, 0u);
    EXPECT_EQ(unsampled.by_strategy.at(1u).p_make, 0.0);

    be::EvaluationResult const sampled = be::evaluate(
        layouts.front(),
        North,
        /*tricks_needed=*/1,
        source,
        pi,
        defender.as_strategy(),
        be::EvaluateOptions{
            .collect_counters = true,
            .bound = provider.as_bound(),
            .delta_is_double_dummy_optimal = true,
            .sampling = {.sample_size = 2u}});
    ASSERT_FALSE(sampled.error.has_value());
    ASSERT_TRUE(sampled.by_strategy.at(1u).counters.has_value());
    EXPECT_EQ(sampled.by_strategy.at(1u).counters->tier2_cuts, 0u);
    // The value is 0.0 either way here (declarer never wins a trick), so this
    // is not evidence the gate worked; tier2_cuts is. It is asserted anyway,
    // as for the unsampled run: a plausible wrong number is worse than a
    // visible failure.
    EXPECT_EQ(sampled.by_strategy.at(1u).p_make, 0.0);
}

// --- solve_board's "not evaluated" score, and the guard that contains it
//
// What is established. `as_bound()` asks solve_board for `solutions = 1`
// and reads `score[0]` as a trick count. On positions this evaluator
// reaches, solve_board can return `score[0] == -2` ("not evaluated") with
// `status == RETURN_NO_FAULT`, so the status check misses it.
//
// Unguarded, -2 on the declarer-on-lead branch is a bound below every
// `tricks_needed`: `tier2_dead()` fires on a live node, and `evaluate()`
// reports 0.0 for a contract that makes. So `as_bound()` range-checks the
// raw score before the orientation split. Out of range, it retries at
// `solutions = 3`, which scored every affected position measured. If the
// retry also fails, it returns `SolverFailureSentinel` (14). The sentinel
// is deliberately too *high*: a bound that is too high only loses pruning,
// while one that is too low fires the cut.
//
// **Not a clamp.** Clamping -2 into `[0, tricks_remaining]` gives 0, which
// is below every `tricks_needed >= 1`. The cut would still fire on every
// affected node while the bug looked fixed.
//
// **Not established: which positions answer this way.** The guard does not
// depend on knowing. The table below refutes the obvious explanations:
//
//   - `nodes == 0` occurs with a *correct* score (SingleSuitOneCardEach), so
//     it is a correlate, not the mechanism.
//   - Changing only `first` on the failing fixture makes the same holdings
//     score correctly (SameHoldingsLedFromNorth), so the holdings alone are
//     not the trigger.
//   - A two-suit position also fails (TwoSuitsOneHeartInWest), so a single
//     suit is not necessary.
//   - A forced play scores correctly (TwoSuitsForcedFollow), so being forced
//     is not sufficient.
//
// So the symptom and the guard are pinned, and the trigger is open. Do not
// trust a trigger story, including the filler-suit note on
// make_declarer_wins_exactly_half_the_tricks, which the second bullet
// contradicts as a complete account.
//
// One asymmetry, for anyone probing. On the declarer-on-lead branch
// as_bound() returns score[0] itself, so -2 is a low bound. On the
// defender-on-lead branch it returns `tricks_remaining - score[0]`, so -2
// gives tricks_remaining + 2: too high, and so harmless. Both branches
// read the same raw score, so both are guarded.

namespace
{
    // Every position is fully specified here, so the table can be re-derived
    // from this file alone. The filler ranks matter.
    struct NoSearchCase
    {
        char const* name;
        Deal deal;
        bool score_is_negative;  ///< what solutions = 1 returns
        int tricks;              ///< tricks the position holds, = cards per hand
    };

    auto spades(std::initializer_list<int> ranks) -> unsigned
    {
        unsigned holding = 0;
        for (int const rank : ranks)
        {
            holding |= 1u << rank;
        }
        return holding;
    }

    auto make_deal(int first, std::array<unsigned, 4> const& spade_holdings,
                   std::array<unsigned, 4> const& heart_holdings) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = first;
        for (int hand = 0; hand < 4; ++hand)
        {
            deal.remainCards[hand][Spades] = spade_holdings[static_cast<std::size_t>(hand)];
            deal.remainCards[hand][1] = heart_holdings[static_cast<std::size_t>(hand)];
        }
        return deal;
    }

    auto no_search_cases() -> std::vector<NoSearchCase>
    {
        constexpr unsigned None = 0u;
        // North/East/South/West order throughout.
        return {
            {"TheFailingFixture",
             make_deal(South, {spades({King, Jack}), spades({6, Five}), spades({10, 9}),
                               spades({Queen, Four})},
                       {None, None, None, None}),
             true, 2},
            {"SameHoldingsLedFromNorth",
             make_deal(North, {spades({King, Jack}), spades({6, Five}), spades({10, 9}),
                               spades({Queen, Four})},
                       {None, None, None, None}),
             false, 2},
            {"SingleSuitOneCardEach",
             make_deal(South, {spades({King}), spades({6}), spades({10}), spades({Queen})},
                       {None, None, None, None}),
             false, 1},
            {"TwoSuitsOneHeartInWest",
             make_deal(South, {spades({King, Jack}), spades({6, Five}), spades({10, 9}),
                               spades({Queen})},
                       {None, None, None, 1u << 9}),
             true, 2},
            {"TwoSuitsForcedFollow",
             make_deal(South, {spades({King}), spades({6}), spades({10}), spades({Queen})},
                       {1u << Queen, 1u << Two, 1u << Ace, 1u << 9}),
             false, 2},
        };
    }
}

// Each row asserts what solve_board does at both solutions = 1 and
// solutions = 3, so the table is a measurement a reader can re-run. This
// probes the solver directly, unaffected by as_bound()'s guard. It records
// the behaviour the guard contains, and changes only if dds does.
TEST_F(DoubleDummyBoundTest, SolverCanAnswerNotEvaluatedAtSolutionsOneWithASuccessStatus)
{
    SolverContext ctx;
    bool any_negative = false;
    bool any_zero_nodes_with_a_correct_score = false;

    for (NoSearchCase const& one : no_search_cases())
    {
        FutureTricks one_solution{};
        int const status_one =
            solve_board(ctx, one.deal, /*target=*/-1, /*solutions=*/1, /*mode=*/0, &one_solution);
        FutureTricks three_solutions{};
        int const status_three =
            solve_board(ctx, one.deal, /*target=*/-1, /*solutions=*/3, /*mode=*/0, &three_solutions);

        // The core of it: a *successful* status with a negative score.
        EXPECT_EQ(status_one, RETURN_NO_FAULT) << one.name;
        EXPECT_EQ(status_three, RETURN_NO_FAULT) << one.name;
        EXPECT_EQ(one_solution.score[0] < 0, one.score_is_negative) << one.name;
        EXPECT_GE(three_solutions.score[0], 0) << one.name << ": solutions = 3 always scores";
        EXPECT_LE(three_solutions.score[0], one.tricks)
            << one.name << ": no side takes more tricks than the position holds";

        if (one_solution.score[0] < 0)
        {
            any_negative = true;
            EXPECT_EQ(one_solution.score[0], -2) << one.name;
            EXPECT_EQ(one_solution.cards, 1) << one.name;
            EXPECT_EQ(one_solution.nodes, 0) << one.name;
        }
        else if (one_solution.nodes == 0)
        {
            // Why `nodes == 0` is not the mechanism: here it comes with a
            // correct score.
            any_zero_nodes_with_a_correct_score = true;
            EXPECT_EQ(one_solution.score[0], three_solutions.score[0]) << one.name;
        }
    }

    EXPECT_TRUE(any_negative)
        << "no position here still reproduces the negative score -- if dds now scores all "
           "five, the guard in as_bound() is dead code and this table no longer measures "
           "anything; re-derive it before deleting either";
    EXPECT_TRUE(any_zero_nodes_with_a_correct_score)
        << "nodes == 0 no longer coincides with a correct score anywhere here, so the "
           "refutation this table exists for no longer holds -- re-derive it";
}

TEST_F(DoubleDummyBoundTest, ANotEvaluatedScoreSurfacesAsTheSentinelNotAsANegativeBound)
{
    // The guard's job, on the position that needs it: a raw score of -2 must
    // not leave as_bound() as itself.
    NoSearchCase const failing = no_search_cases().front();
    ASSERT_TRUE(failing.score_is_negative) << failing.name;

    SolverContext ctx;
    be::DoubleDummyBound provider(ctx, North);
    be::LayoutBound const bound = provider.as_bound();

    // The retry at solutions = 3 scores this position correctly, so the
    // bound is the real double-dummy value, not the sentinel: declarer's side
    // takes both tricks. The sentinel is kept for a position where both solves
    // decline to score, though nothing here reaches it.
    EXPECT_EQ(bound(failing.deal), failing.tricks)
        << "expected the true double-dummy value. 14 means the retry is gone and the "
           "bound has fallen back to the sentinel -- sound, but it prunes nothing on "
           "this position. A negative value means the range guard itself is gone and "
           "solve_board's \"not evaluated\" score is leaking into the cut again";
}

TEST_F(DoubleDummyBoundTest, EveryBoundIsAtTheSentinelOrInsideTheTrickRangeNeverBelowIt)
{
    // The guard as an invariant over every position: as_bound() returns a
    // real bound within the tricks the position holds, or the sentinel. Never
    // a value below the range, the only way a bound can fire a wrong cut.
    SolverContext ctx;
    be::DoubleDummyBound provider(ctx, North);
    be::LayoutBound const bound = provider.as_bound();

    for (NoSearchCase const& one : no_search_cases())
    {
        int const value = bound(one.deal);
        // The property the guard exists for, which must survive any change to
        // how the value is obtained: never below the range.
        EXPECT_GE(value, 0) << one.name << ": a bound below zero is below every tricks_needed";
        EXPECT_LE(value, one.tricks)
            << one.name << ": with the retry, every position here scores -- a value of 14 "
                           "means it fell back to the sentinel";
    }
}

TEST_F(DoubleDummyBoundTest, WithTheBoundEvaluateStillReportsTheTrueValueOnAnUnscorablePosition)
{
    // End to end: the same position with and without the bound must give the
    // same answer. Without the guard, it reports 0.0 with the bound and 1.0
    // without.
    //
    // TheFailingFixture, from declarer's side: North (declarer) holds spades
    // K J, South (dummy) T 9, and the defenders Q 6 5 4. North's king beats
    // every defender card, so declarer's side wins at least one of the two
    // tricks. So the true value is 1.0 at tricks_needed = 1, for any pi and
    // delta.
    Deal const layout = no_search_cases().front().deal;
    be::VectorLayoutSource source({layout});
    SolverContext ctx;
    be::DoubleDummyDefender defender(ctx);
    be::DoubleDummyBound provider(ctx, North);
    be::DeclarerStrategy const pi{
        .id = 1, .play = be::single_card_declarer_play, .state_key = nullptr};

    be::EvaluationResult const without_bound = be::evaluate(
        layout, North, /*tricks_needed=*/1, source, pi, defender.as_strategy(),
        be::EvaluateOptions{.collect_counters = true});

    be::EvaluationResult const with_bound = be::evaluate(
        layout, North, /*tricks_needed=*/1, source, pi, defender.as_strategy(),
        be::EvaluateOptions{
            .collect_counters = true,
            .bound = provider.as_bound(),
            .delta_is_double_dummy_optimal = true});

    ASSERT_FALSE(without_bound.error.has_value());
    ASSERT_FALSE(with_bound.error.has_value());
    EXPECT_EQ(without_bound.by_strategy.at(1u).p_make, 1.0);
    EXPECT_EQ(with_bound.by_strategy.at(1u).p_make, without_bound.by_strategy.at(1u).p_make)
        << "the bound changed the answer, which a sound bound cannot do -- it may only "
           "change how much work reaching it took";

    // No node saving is expected here, and that is not a failure of the
    // retry. The retry gives this position its true bound, 2, which is not
    // below the single trick needed, so the cut correctly does not fire. A
    // bound prunes only where it is below tricks_needed.
    //
    // The retry pays off where cuts happen. On the four-card ending in
    // examples/, tier 2 takes nodes visited from 6283 to 3159, with 197 cuts,
    // and runs faster than tier 1 alone: the retry costs less than searching
    // the subtrees it lets the cut remove.
    ASSERT_TRUE(without_bound.by_strategy.at(1u).counters.has_value());
    ASSERT_TRUE(with_bound.by_strategy.at(1u).counters.has_value());
    EXPECT_EQ(
        with_bound.by_strategy.at(1u).counters->nodes_visited,
        without_bound.by_strategy.at(1u).counters->nodes_visited)
        << "a live position whose bound is at or above tricks_needed must be searched "
           "either way; a difference here means the bound cut something it should not";
}
