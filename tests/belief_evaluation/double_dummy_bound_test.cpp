#include <gtest/gtest.h>

// api/dds.h -- pulled in transitively by solver_context.hpp and by
// api/solve_board.hpp -- declares its own unrelated struct Card that
// collides with belief_evaluation's. See double_dummy_defender.cpp's own
// top-of-file comment for the full explanation; renamed locally here for
// the same reason, since this file also needs to construct a raw
// SolverContext directly for the orientation tests below.
#define Card DdsInternalCard
#include <api/solve_board.hpp>
#include <solver_context/solver_context.hpp>
#undef Card

#include <api/dll.h>
#include <utility/constants.h>

#include <belief_evaluation/double_dummy_bound.hpp>
#include <belief_evaluation/double_dummy_defender.hpp>
#include <belief_evaluation/evaluate.hpp>

#include "test_support.hpp"

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
        deal.remainCards[North][Spades] = holding({Ace});
        deal.remainCards[East][Spades] = holding({Two});
        deal.remainCards[South][Spades] = holding({Three});
        deal.remainCards[West][Spades] = holding({Four});
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
        deal.remainCards[East][Spades] = holding({Ace, King});
        deal.remainCards[West][Spades] = holding({Queen, Jack});
        deal.remainCards[North][Spades] = holding({Two, Three});
        deal.remainCards[South][Spades] = holding({Four, Five});
        deal.remainCards[North][Clubs] = holding({Ace, King});
        deal.remainCards[South][Clubs] = holding({Queen, Jack});
        deal.remainCards[East][Clubs] = holding({Two, Three});
        deal.remainCards[West][Clubs] = holding({Four, Five});
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
        deal.remainCards[North][Spades] = holding({Ace, King});
        deal.remainCards[East][Spades] = holding({Queen, Jack});
        deal.remainCards[South][Spades] = holding({Two, Three});
        deal.remainCards[West][Spades] = holding({Four, Five});
        deal.remainCards[North][Clubs] = holding({Ace, King});
        deal.remainCards[East][Clubs] = holding({Queen, Jack});
        deal.remainCards[South][Clubs] = holding({Two, Three});
        deal.remainCards[West][Clubs] = holding({Four, Five});
        return deal;
    }
}

// --- hand-checkable positions ------------------------------------------

TEST_F(DoubleDummyBoundTest, ADeclarerWinOutrightBoundsAtTheFullTrickCount)
{
    Deal const layout = make_north_wins_every_trick();
    SolverContext ctx;
    DoubleDummyBound provider(ctx, North);
    LayoutBound const bound = provider.as_bound();

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
    deal.remainCards[East][Spades] = holding({Ace, King});
    deal.remainCards[West][Spades] = holding({Queen, Jack});
    deal.remainCards[North][Spades] = holding({Two, Three});
    deal.remainCards[South][Spades] = holding({Four, Five});
    deal.remainCards[East][Clubs] = holding({Ace, King});
    deal.remainCards[West][Clubs] = holding({Queen, Jack});
    deal.remainCards[North][Clubs] = holding({Two, Three});
    deal.remainCards[South][Clubs] = holding({Four, Five});

    SolverContext ctx;
    DoubleDummyBound provider(ctx, North);
    LayoutBound const bound = provider.as_bound();

    EXPECT_EQ(bound(deal), 0);
}

TEST_F(DoubleDummyBoundTest, APositionInBetweenBoundsAtExactlyHalf)
{
    Deal const layout = make_declarer_wins_exactly_half_the_tricks();
    SolverContext ctx;
    DoubleDummyBound provider(ctx, North);
    LayoutBound const bound = provider.as_bound();

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
    DoubleDummyBound provider(ctx, North);
    LayoutBound const bound = provider.as_bound();

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
    DoubleDummyBound provider(ctx, North);
    LayoutBound const bound = provider.as_bound();

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
        deal.remainCards[North][Spades] = holding({Queen, Jack});
        deal.remainCards[South][Spades] = holding({Two, Three});
        deal.remainCards[East][Spades] = holding({Ace, Four});
        deal.remainCards[West][Spades] = holding({King, Five});
        deal.remainCards[North][Clubs] = holding({Six});
        deal.remainCards[South][Clubs] = holding({Seven});
        deal.remainCards[East][Clubs] = holding({Eight});
        deal.remainCards[West][Clubs] = holding({Nine});
        return deal;
    }
}

TEST_F(DoubleDummyBoundTest, ReproductionRunBTierOneAndTwoTogetherAgainstAQualifyingDelta)
{
    Deal const layout = make_declarer_never_wins_a_trick();
    VectorLayoutSource source({layout});
    SolverContext ctx;
    DoubleDummyDefender defender(ctx);
    DoubleDummyBound provider(ctx, North);
    DeclarerStrategy const pi{
        .id = 1, .play = single_card_declarer_play, .state_key = nullptr};

    // Baseline: tier 1 only (no bound supplied, so tier2_dead() never
    // fires -- see its own guard). The true value is 0.0, reached by full
    // recursion through both tricks.
    EvaluationResult const tier1_only = evaluate(
        layout,
        North,
        /*tricks_needed=*/1,
        source,
        pi,
        defender.as_strategy(),
        EvaluateOptions{.collect_counters = true});

    // Tier 1 and 2 together: DoubleDummyDefender satisfies
    // delta_is_double_dummy_optimal (target = -1 is trick-maximising for
    // both sides -- see EvaluateOptions::delta_is_double_dummy_optimal's
    // own doxygen), so this is the sound, intended pairing.
    EvaluationResult const tier1_and_2 = evaluate(
        layout,
        North,
        /*tricks_needed=*/1,
        source,
        pi,
        defender.as_strategy(),
        EvaluateOptions{
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
