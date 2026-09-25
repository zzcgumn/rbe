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

// This file constructs a raw SolverContext directly (for the orientation
// tests below), so it sees both api/dds.h's own ::Card and this module's
// dds::belief_evaluation::Card in the same translation unit. Qualifying
// through this alias rather than `using namespace` keeps every module type
// unambiguous against dds's -- the two are layout-identical, so an
// unqualified `Card` would compile as either with no error to catch it.
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

// --- what solve_board's raw score actually measures ------------------------
//
// Verified empirically against the solver, not assumed -- the same
// discipline as confirming FutureTricks::equals' bit convention before
// building anything on it. A single-trick, one-card-per-hand ending, with
// North (declarer) always holding the ace -- so declarer's own trick count
// is 1 regardless of who leads -- but who is *on lead* changes what
// solve_board's score[0] reports.
//
// A single suit with one card per hand is enough here (unlike every other
// fixture below, which need a second, filler suit -- see its own note).

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

    // North (declarer) on lead: declarer's own side is on lead, so the two
    // hypotheses ("declarer's absolute tricks" vs "the side on lead's own
    // tricks") coincide -- both would say 1. Not a disambiguator by
    // itself, but the expected, unsurprising case.
    EXPECT_EQ(solve_first(North), 1);

    // East (a defender) on lead, same holdings: declarer's own ace still
    // wins the only trick whoever leads, so "declarer's absolute tricks"
    // would still say 1 -- but "the side on lead's own tricks" says 0,
    // since East's side can never beat North's ace. The actual result
    // disambiguates the two hypotheses directly.
    EXPECT_EQ(solve_first(East), 0);
}

namespace
{
    /// East holds the top two spades, North (declarer) and South (dummy)
    /// the bottom two, West the middle two -- East wins both spade tricks
    /// outright however anyone else plays. North holds the top two clubs
    /// outright -- North wins both club tricks the same way. Four tricks
    /// total, split evenly: declarer's double-dummy bound is exactly 2, a
    /// genuine in-between value, not 0 or 4.
    ///
    /// The second suit is not incidental: a solver call over a *single*
    /// suit's worth of cards with more than one card per hand was measured
    /// directly (not assumed) to report a nonsensical score (a negative
    /// number) from this solver build, while the same holdings alongside
    /// any second, filler suit solve correctly. Every fixture below with
    /// more than one card per hand therefore carries a second suit, even
    /// where the suit itself plays no role in the position's logic.
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

    /// North (declarer) holds the top two spades outright, and the top two
    /// clubs too -- two certain tricks in each suit, four total, regardless
    /// of who leads or what anyone else plays. The second suit here is
    /// again the filler-suit workaround described above, not part of the
    /// position's own logic -- North would win the spade tricks alone
    /// exactly the same way with clubs absent.
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
    // Same shape as make_north_wins_every_trick(), but with East holding
    // every honour in both suits -- declarer cannot win a single trick,
    // whoever leads or plays.
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
    // make_north_wins_every_trick() but re-led from East (a defender) --
    // the orientation conversion (tricks_remaining(layout) minus the raw
    // score) must still recover declarer's own bound of 4, not the
    // defenders' own raw score of 0.
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
    // An empty Deal (no cards anywhere, first = North by zero-init) is not
    // a legal position for the solver to analyse -- solve_board is
    // expected to reject it non-zero, the same class of failure
    // DoubleDummyDefender's own tests exercise.
    Deal const empty_deal{};
    SolverContext ctx;
    be::DoubleDummyBound provider(ctx, North);
    be::LayoutBound const bound = provider.as_bound();

    EXPECT_EQ(bound(empty_deal), 14);
}

// --- reproduction run (b): tier 1 and tier 2 together, against a delta
// that satisfies EvaluateOptions::delta_is_double_dummy_optimal (the
// headline acceptance criterion for early cuts, the solver-linked half --
// run (a) is in reproduction_test.cpp). DoubleDummyDefender paired with
// DoubleDummyBound, both driven by the same SolverContext, is the intended
// sound configuration DoubleDummyBound's own doxygen names.

namespace
{
    constexpr int Six = 6;
    constexpr int Seven = 7;
    constexpr int Eight = 8;
    constexpr int Nine = 9;

    /// North (declarer) holds only the queen and jack of spades -- East
    /// and West hold the ace and king between them, so declarer never
    /// wins a spade trick regardless of the split or who leads. The club
    /// filler (one card each, never a genuine choice) also always goes to
    /// West, the highest of the four -- so declarer's true double-dummy
    /// value over the whole three-trick ending is 0, not just in spades.
    ///
    /// tier 1's own dead cut cannot see this: at the root, declarer's own
    /// card count (3: queen, jack, club) is well above tricks_needed (1),
    /// so tier 1 finds nothing to prune -- tier 1 counts cards, not
    /// winners. Only a real double-dummy bound catches it, which is the
    /// point of this fixture.
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

    // Baseline: tier 1 only (no bound supplied, so tier2_dead() never
    // fires -- see its own guard). The true value is 0.0, reached by full
    // recursion through both tricks.
    be::EvaluationResult const tier1_only = be::evaluate(
        layout,
        North,
        /*tricks_needed=*/1,
        source,
        pi,
        defender.as_strategy(),
        be::EvaluateOptions{.collect_counters = true});

    // Tier 1 and 2 together: DoubleDummyDefender satisfies
    // delta_is_double_dummy_optimal (target = -1 is trick-maximising for
    // both sides -- see EvaluateOptions::delta_is_double_dummy_optimal's
    // own doxygen), so this is the sound, intended pairing.
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
    // Bitwise (criterion for run (b) itself): both exactly 0.0, and both
    // reached the identical way regardless of which tiers were active.
    EXPECT_EQ(tier1_only.by_strategy.at(1u).p_make, tier1_and_2.by_strategy.at(1u).p_make);
    EXPECT_EQ(tier1_only.by_strategy.at(1u).p_make, 0.0);

    // Evidence tier 2 did real work tier 1 alone could not: fewer nodes
    // visited with tier 2 enabled, since tier 2's bound catches the
    // all-dead position at the root itself, where tier 1's own dead cut
    // could not (see the fixture's own comment).
    ASSERT_TRUE(tier1_only.by_strategy.at(1u).counters.has_value());
    ASSERT_TRUE(tier1_and_2.by_strategy.at(1u).counters.has_value());
    EXPECT_LT(
        tier1_and_2.by_strategy.at(1u).counters->nodes_visited,
        tier1_only.by_strategy.at(1u).counters->nodes_visited);
    // Fires at the very root: exactly 1 node visited with tier 2 enabled.
    EXPECT_EQ(tier1_and_2.by_strategy.at(1u).counters->nodes_visited, 1u);
}

TEST_F(DoubleDummyBoundTest, TierTwoCutRateUnderSamplingIsExactlyZeroBecauseOfTheGateNotByAccident)
{
    // The same fixture, real bound and real defender as the reproduction
    // run above -- reused, not re-derived, per this file's own established
    // pattern -- but this time with genuinely more than one consistent
    // layout in the source, so a sample_size can actually bind. Five
    // identical copies of make_declarer_never_wins_a_trick(): duplicate
    // content is fine here (is_consistent compares each candidate against
    // the root layout, and an exact copy of the root trivially matches),
    // and DoubleDummyBound computes the identical answer (0 tricks) for
    // every one of them, since the property driving that answer -- the
    // spades holding -- is the same in every copy.
    //
    // A zero tier-2 rate is trivially achievable by not measuring, by a
    // fixture with no bound, or by a bound that never fires -- none of
    // that is what this test shows. The unsampled run below fires the cut
    // on this exact bound and delta (tier2_cuts >= 1); the sampled run,
    // same bound, same delta, same layouts, fires it zero times. The only
    // difference between the two runs is is_sample.
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
    // The true value happens to be 0.0 either way here (declarer really
    // does never win a trick in this fixture), so this is not itself
    // evidence the gate did anything -- tier2_cuts is. Asserted anyway,
    // for the same reason the unsampled run's own value is: a mechanism
    // that reaches a plausible-looking wrong number is worse than one
    // that visibly fails.
    EXPECT_EQ(sampled.by_strategy.at(1u).p_make, 0.0);
}

// --- known unsoundness: solve_board can report "not evaluated" as a score
//
// **The tests below assert behaviour that is wrong.** They exist so it cannot
// change silently. When the bound is fixed they fail; update them and the
// doxygen on `DoubleDummyBound::as_bound()` together.
//
// What is established. `as_bound()` asks solve_board for `solutions = 1` and
// reads `score[0]` as declarer's trick count. On positions this evaluator
// reaches, solve_board can return `score[0] == -2` with
// `status == RETURN_NO_FAULT` -- "not evaluated" rather than a trick count.
// The status guard therefore never fires, the -2 is returned as a bound, and
// being negative it is below any `tricks_needed`, so `tier2_dead()` fires on a
// live node and `evaluate()` reports 0.0 for a contract that makes.
// `solutions = 3` scores every affected position correctly.
//
// **What is NOT established: which positions trigger it.** An earlier version
// of this comment claimed two triggers (a single-suit position with more than
// one card per hand, and a forced or all-equals play) reaching one mechanism,
// `nodes == 0`. That was wrong, and the parameterised test below is the
// refutation rather than a demonstration:
//
//   - `nodes == 0` occurs with a *correct* score (SingleSuitOneCardEach), so
//     it is a correlate and not the mechanism.
//   - Changing only `first` on the failing fixture makes the same holdings
//     score correctly (SameHoldingsLedFromNorth), so the holdings alone are
//     not the trigger.
//   - A two-suit position also fails (TwoSuitsOneHeartInWest), so a single
//     suit is not necessary.
//   - A forced play scores correctly (TwoSuitsForcedFollow), so being forced
//     is not sufficient.
//
// So: the symptom is pinned, the trigger is open. Anyone fixing this should
// not trust a trigger story, including this one. Note that this file's own
// fixture note (see make_declarer_wins_exactly_half_the_tricks) records a
// narrower observation -- single suit, more than one card per hand -- which
// the second bullet above also contradicts as a complete account.
//
// The safe fix does not depend on knowing the trigger: treat a score outside
// `[0, tricks_remaining(layout)]` as SolverFailureSentinel, which restores the
// "too high, never too low" property the sentinel already promises. Recovering
// the lost pruning does need it, or a retry with `solutions = 3`.

namespace
{
    // Every position below is fully specified here, so the table is
    // re-derivable from this file alone. An earlier version of this comment
    // carried measurements whose positions were not recorded, which made them
    // unverifiable -- the filler ranks turned out to be load-bearing.
    struct NoSearchCase
    {
        char const* name;
        Deal deal;
        bool score_is_negative;  ///< what solutions = 1 does today
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
             true},
            {"SameHoldingsLedFromNorth",
             make_deal(North, {spades({King, Jack}), spades({6, Five}), spades({10, 9}),
                               spades({Queen, Four})},
                       {None, None, None, None}),
             false},
            {"SingleSuitOneCardEach",
             make_deal(South, {spades({King}), spades({6}), spades({10}), spades({Queen})},
                       {None, None, None, None}),
             false},
            {"TwoSuitsOneHeartInWest",
             make_deal(South, {spades({King, Jack}), spades({6, Five}), spades({10, 9}),
                               spades({Queen})},
                       {None, None, None, 1u << 9}),
             true},
            {"TwoSuitsForcedFollow",
             make_deal(South, {spades({King}), spades({6}), spades({10}), spades({Queen})},
                       {1u << Queen, 1u << Two, 1u << Ace, 1u << 9}),
             false},
        };
    }
}

// Each row asserts what solve_board does today, at both solutions = 1 and
// solutions = 3, so the table above is a measurement a reader can re-run
// rather than a claim they have to take.
TEST_F(DoubleDummyBoundTest, KnownUnsoundnessSolutionsOneCanReportNotEvaluated)
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

        // The heart of it: a *successful* status carrying a negative score.
        EXPECT_EQ(status_one, RETURN_NO_FAULT) << one.name;
        EXPECT_EQ(status_three, RETURN_NO_FAULT) << one.name;
        EXPECT_EQ(one_solution.score[0] < 0, one.score_is_negative) << one.name;
        EXPECT_GE(three_solutions.score[0], 0) << one.name << ": solutions = 3 always scores";

        if (one_solution.score[0] < 0)
        {
            any_negative = true;
            EXPECT_EQ(one_solution.score[0], -2) << one.name;
            EXPECT_EQ(one_solution.cards, 1) << one.name;
            EXPECT_EQ(one_solution.nodes, 0) << one.name;
        }
        else if (one_solution.nodes == 0)
        {
            // Why `nodes == 0` is not the mechanism: here it coincides with a
            // correct score.
            any_zero_nodes_with_a_correct_score = true;
            EXPECT_EQ(one_solution.score[0], three_solutions.score[0]) << one.name;
        }
    }

    EXPECT_TRUE(any_negative) << "no position still reproduces the negative score -- "
                                 "the unsoundness may be fixed; see this section's comment";
    EXPECT_TRUE(any_zero_nodes_with_a_correct_score)
        << "nodes == 0 no longer coincides with a correct score anywhere here, so the "
           "refutation this table exists for no longer holds -- re-derive it";
}

TEST_F(DoubleDummyBoundTest, KnownUnsoundnessANegativeScoreBecomesANegativeBound)
{
    // The step that makes the solver's sentinel a soundness problem rather
    // than a curiosity: as_bound() passes it straight through.
    Deal const layout = no_search_cases().front().deal;
    SolverContext ctx;
    be::DoubleDummyBound provider(ctx, North);
    be::LayoutBound const bound = provider.as_bound();

    EXPECT_LT(bound(layout), 0) << "the bound no longer leaks solve_board's \"not "
                                  "evaluated\" score -- if it now returns a value in "
                                  "[0, 4], the unsoundness is fixed: delete these tests "
                                  "and update double_dummy_bound.hpp's doxygen";
}

TEST_F(DoubleDummyBoundTest, KnownUnsoundnessANegativeBoundIsBelowAnyTricksNeeded)
{
    // Why the negative matters: it is not merely a wrong number, it is a
    // number that fires the cut. Any tricks_needed a caller could ask for is
    // above it, so tier2_dead() concludes "every layout here is dead" at a
    // node where declarer in fact takes tricks.
    Deal const layout = no_search_cases().front().deal;
    SolverContext ctx;
    be::DoubleDummyBound provider(ctx, North);
    be::LayoutBound const bound = provider.as_bound();

    for (int tricks_needed = 1; tricks_needed <= 4; ++tricks_needed)
    {
        EXPECT_LT(bound(layout), tricks_needed) << "at tricks_needed = " << tricks_needed;
    }
}
