#include <gtest/gtest.h>

#include <type_traits>

#include <api/dds_data_types.hpp>
#include <api/solve_board.hpp>
#include <solver_context/solver_context.hpp>
#include <utility/constants.h>

#include <belief_evaluation/double_dummy_defender.hpp>
#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/validation.hpp>

#include "test_support.hpp"

// This file constructs a raw SolverContext directly (unlike the rest of the
// suite), so it sees both api/dds.h's own ::Card and this module's
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
    constexpr int Queen = 12;
    constexpr int King = 13;
    constexpr int Ace = 14;

    constexpr int Spades = 0;
    constexpr int Hearts = 1;
    constexpr int Diamonds = 2;
    constexpr int Clubs = 3;

    constexpr int North = 0;
    constexpr int East = 1;
    constexpr int South = 2;
    constexpr int West = 3;
}

class DoubleDummyDefenderTest : public ::testing::Test
{
};

// --- a hand with one legal card ---------------------------------------

TEST_F(DoubleDummyDefenderTest, AHandWithOneLegalCardGetsProbabilityOne)
{
    // A one-trick ending: East holds only the king of spades, so there is
    // no ambiguity about what double dummy should say -- it is East's only
    // legal play, whatever the outcome of the trick.
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = East;
    deal.remainCards[North][Spades] = be::holding({Ace});
    deal.remainCards[East][Spades] = be::holding({King});
    deal.remainCards[South][Spades] = be::holding({Three});
    deal.remainCards[West][Spades] = be::holding({Two});

    SolverContext ctx;
    be::DoubleDummyDefender defender(ctx);
    std::vector<be::WeightedCard> const distribution =
        defender.as_strategy()(be::DefenderQuery{deal, East, be::ObservationState{}});

    ASSERT_EQ(distribution.size(), 1u);
    EXPECT_EQ(distribution[0].card.suit, Spades);
    EXPECT_EQ(distribution[0].card.rank, King);
    EXPECT_DOUBLE_EQ(distribution[0].probability, 1.0);
    EXPECT_EQ(
        be::validate_defender_distribution(deal, East, distribution), be::ValidationError::None);
}

// This file is the one place in the suite where api/dds.h's ::Card and this
// module's own be::Card are both visible at once -- the two coexisting,
// asserted rather than assumed, is what the namespace split was for. The
// two are layout-identical (same members, same types, same order), so this
// only compiles at all because they are genuinely two distinct types.
TEST_F(DoubleDummyDefenderTest, DdsAndModuleCardTypesCoexistAndAreDistinct)
{
    ::Card const solver_card{Spades, Ace};
    be::Card const module_card{Spades, Ace};
    static_assert(!std::is_same_v<decltype(solver_card), decltype(module_card)>);
    EXPECT_EQ(solver_card.suit, module_card.suit);
    EXPECT_EQ(solver_card.rank, module_card.rank);
}

// --- a clearly best card, and why -----------------------------------------

TEST_F(DoubleDummyDefenderTest, AClearlyBestCardWinsAllTheProbability)
{
    // Trump = spades. East, on lead, holds the ace of clubs and the two of
    // spades (its only trump). North holds the king of clubs (its only
    // club) and the three of hearts; South holds the three of spades (its
    // only trump) and the ace of hearts; West holds the four of spades
    // (its only trump) and the four of hearts. Trick order is East, South,
    // West, North.
    //
    // Leading the ace of clubs first: South, void in clubs, ruffs with its
    // three of spades -- but West, also void in clubs, *overruffs* with its
    // four of spades, winning the trick for East's side before East's own
    // two of spades is ever needed; North, last to play, is forced to follow
    // suit with its king (loses, and never affects the winner -- the trick
    // is already decided between the two trump plays). West then leads its
    // last card (the four of hearts); North follows with its remaining
    // heart; East, void in hearts, is forced to play its last card, the two
    // of spades -- a trump, which wins the trick outright over South's ace
    // of hearts (a trump beats a non-trump regardless of rank). East's side:
    // 2 tricks (the overruff, then the forced ruff).
    //
    // Leading the two of spades (the trump) first instead: South and West
    // must both follow suit (forced), so their three and four of spades
    // are spent on nothing -- West's four is high enough to win the trick
    // for East's side, but *only* that one trick, since neither defender
    // has a trump left afterwards (North, void in spades throughout, simply
    // discards). South's ace of hearts then survives uncontested to win the
    // last trick for North-South. East's side: 1 trick.
    //
    // So the ace of clubs is strictly better than the trump: leading it
    // lets West's overruff (and East's own forced follow-up ruff) happen
    // *before* the defense's own trumps are used up on nothing.
    Deal deal{};
    deal.trump = Spades;
    deal.first = East;
    deal.remainCards[North][Clubs] = be::holding({King});
    deal.remainCards[North][Hearts] = be::holding({Three});
    deal.remainCards[South][Spades] = be::holding({Three});
    deal.remainCards[South][Hearts] = be::holding({Ace});
    deal.remainCards[East][Spades] = be::holding({Two});
    deal.remainCards[East][Clubs] = be::holding({Ace});
    deal.remainCards[West][Spades] = be::holding({Four});
    deal.remainCards[West][Hearts] = be::holding({Four});

    SolverContext ctx;
    be::DoubleDummyDefender defender(ctx);  // TouchingSequence (default); no tie here either way
    std::vector<be::WeightedCard> const distribution =
        defender.as_strategy()(be::DefenderQuery{deal, East, be::ObservationState{}});

    ASSERT_EQ(distribution.size(), 1u);
    EXPECT_EQ(distribution[0].card.suit, Clubs);
    EXPECT_EQ(distribution[0].card.rank, Ace);
    EXPECT_DOUBLE_EQ(distribution[0].probability, 1.0);
    EXPECT_EQ(
        be::validate_defender_distribution(deal, East, distribution), be::ValidationError::None);
}

TEST_F(DoubleDummyDefenderTest, SolutionsTwoReportsEveryScoreTiedCandidateAcrossSuits)
{
    // Pins the reasoning double_dummy_defender.cpp gives for using
    // solutions = 2: on a position where two different suits genuinely tie
    // for the maximum score (the same fixture
    // TwoTiedSuitsDistinguishTouchingSequenceFromAllOptimal below uses),
    // solutions = 2 reports *both* tied entries -- not just a single best
    // guess -- with the same scores and equals groups solutions = 3 gives
    // those same entries, checked directly below rather than assumed.
    // spread() only ever consumes entries at the maximum score (both
    // TouchingSequence and AllOptimal filter to it before using anything
    // else), so a solution mode that returns exactly that set is already
    // everything spread() needs; the entries solutions = 3 additionally
    // reports (every legal card, not just the tied ones) are strictly
    // sub-optimal and would be discarded by that filter regardless.
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = East;
    deal.remainCards[North][Spades] = be::holding({Three});
    deal.remainCards[North][Hearts] = be::holding({Three});
    deal.remainCards[North][Diamonds] = be::holding({Two});
    deal.remainCards[North][Clubs] = be::holding({Two});
    deal.remainCards[South][Spades] = be::holding({Four});
    deal.remainCards[South][Hearts] = be::holding({Four});
    deal.remainCards[South][Diamonds] = be::holding({Three});
    deal.remainCards[South][Clubs] = be::holding({Three});
    deal.remainCards[East][Spades] = be::holding({Ace, King});
    deal.remainCards[East][Hearts] = be::holding({Ace, King});
    deal.remainCards[West][Spades] = be::holding({Two});
    deal.remainCards[West][Hearts] = be::holding({Two});
    deal.remainCards[West][Diamonds] = be::holding({Four});
    deal.remainCards[West][Clubs] = be::holding({Four});

    SolverContext ctx;
    FutureTricks two_solutions{};
    ASSERT_EQ(
        solve_board(ctx, deal, /*target=*/-1, /*solutions=*/2, /*mode=*/0, &two_solutions),
        RETURN_NO_FAULT);

    ASSERT_EQ(two_solutions.cards, 2);  // both suits' tied entries present, not just one
    int spade_count = 0;
    int heart_count = 0;
    for (int i = 0; i < two_solutions.cards; ++i)
    {
        EXPECT_EQ(two_solutions.rank[i], Ace);
        EXPECT_EQ(two_solutions.score[i], 4);
        EXPECT_EQ(two_solutions.equals[i], 1 << King);  // each suit's own king, touching its ace
        if (two_solutions.suit[i] == Spades)
        {
            ++spade_count;
        }
        else if (two_solutions.suit[i] == Hearts)
        {
            ++heart_count;
        }
    }
    EXPECT_EQ(spade_count, 1);
    EXPECT_EQ(heart_count, 1);

    // The comparison the comment above actually needs: solutions = 3 must
    // agree with solutions = 2 on this same deal, not merely be assumed to.
    FutureTricks three_solutions{};
    ASSERT_EQ(
        solve_board(ctx, deal, /*target=*/-1, /*solutions=*/3, /*mode=*/0, &three_solutions),
        RETURN_NO_FAULT);
    ASSERT_EQ(three_solutions.cards, two_solutions.cards);
    for (int i = 0; i < two_solutions.cards; ++i)
    {
        EXPECT_EQ(three_solutions.suit[i], two_solutions.suit[i]);
        EXPECT_EQ(three_solutions.rank[i], two_solutions.rank[i]);
        EXPECT_EQ(three_solutions.score[i], two_solutions.score[i]);
        EXPECT_EQ(three_solutions.equals[i], two_solutions.equals[i]);
    }
}

// --- a genuine touching sequence -------------------------------------------

TEST_F(DoubleDummyDefenderTest, ATouchingSequenceSpreadsEvenlyUnderTouchingSequence)
{
    // North holds the king and queen of spades -- a touching pair -- with
    // East, South and West holding low singletons. Playing either honour
    // from KQ leaves an isomorphic position (a strictly order-preserving
    // bijection within the suit, under which every rule of trick-taking is
    // preserved), so double dummy reports one entry (the king) with the
    // queen named in its equals group, and TouchingSequence spreads 0.5/0.5
    // over the two.
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = North;
    deal.remainCards[North][Spades] = be::holding({King, Queen});
    deal.remainCards[East][Spades] = be::holding({Two});
    deal.remainCards[East][Hearts] = be::holding({Two});
    deal.remainCards[South][Spades] = be::holding({Three});
    deal.remainCards[South][Hearts] = be::holding({Three});
    deal.remainCards[West][Spades] = be::holding({Four});
    deal.remainCards[West][Hearts] = be::holding({Four});

    SolverContext ctx;
    be::DoubleDummyDefender defender(ctx, be::SpreadPolicy::TouchingSequence);
    std::vector<be::WeightedCard> const distribution =
        defender.as_strategy()(be::DefenderQuery{deal, North, be::ObservationState{}});

    ASSERT_EQ(distribution.size(), 2u);
    for (be::WeightedCard const& entry : distribution)
    {
        EXPECT_EQ(entry.card.suit, Spades);
        EXPECT_DOUBLE_EQ(entry.probability, 0.5);
    }
    EXPECT_EQ(
        be::validate_defender_distribution(deal, North, distribution), be::ValidationError::None);
}

// --- the case that distinguishes TouchingSequence from AllOptimal ---------

TEST_F(DoubleDummyDefenderTest, TwoTiedSuitsDistinguishTouchingSequenceFromAllOptimal)
{
    // East holds the ace-king of spades and the ace-king of hearts -- two
    // independent touching pairs -- with North, South and West holding low
    // fillers in all four suits so nothing else is live. Whichever suit
    // East leads, its side takes both of that suit's top cards regardless
    // of order (nothing else in the deal can beat an ace or, once the ace
    // is gone, the king), and the same is true of the other suit -- so
    // double dummy genuinely ties both suits at the same score. This is the
    // case that distinguishes the two policies: TouchingSequence spreads
    // only over the canonical suit's pair; AllOptimal spreads over both.
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = East;
    deal.remainCards[North][Spades] = be::holding({Three});
    deal.remainCards[North][Hearts] = be::holding({Three});
    deal.remainCards[North][Diamonds] = be::holding({Two});
    deal.remainCards[North][Clubs] = be::holding({Two});
    deal.remainCards[South][Spades] = be::holding({Four});
    deal.remainCards[South][Hearts] = be::holding({Four});
    deal.remainCards[South][Diamonds] = be::holding({Three});
    deal.remainCards[South][Clubs] = be::holding({Three});
    deal.remainCards[East][Spades] = be::holding({Ace, King});
    deal.remainCards[East][Hearts] = be::holding({Ace, King});
    deal.remainCards[West][Spades] = be::holding({Two});
    deal.remainCards[West][Hearts] = be::holding({Two});
    deal.remainCards[West][Diamonds] = be::holding({Four});
    deal.remainCards[West][Clubs] = be::holding({Four});

    SolverContext ctx;

    be::DoubleDummyDefender touching(ctx, be::SpreadPolicy::TouchingSequence);
    std::vector<be::WeightedCard> const touching_distribution =
        touching.as_strategy()(be::DefenderQuery{deal, East, be::ObservationState{}});
    ASSERT_EQ(touching_distribution.size(), 2u);
    int const touching_suit = touching_distribution[0].card.suit;
    for (be::WeightedCard const& entry : touching_distribution)
    {
        EXPECT_EQ(entry.card.suit, touching_suit);  // one suit only
        EXPECT_DOUBLE_EQ(entry.probability, 0.5);
    }
    EXPECT_EQ(
        be::validate_defender_distribution(deal, East, touching_distribution),
        be::ValidationError::None);

    be::DoubleDummyDefender all_optimal(ctx, be::SpreadPolicy::AllOptimal);
    std::vector<be::WeightedCard> const all_optimal_distribution =
        all_optimal.as_strategy()(be::DefenderQuery{deal, East, be::ObservationState{}});
    ASSERT_EQ(all_optimal_distribution.size(), 4u);  // both suits' AK pairs
    int spade_count = 0;
    int heart_count = 0;
    for (be::WeightedCard const& entry : all_optimal_distribution)
    {
        EXPECT_DOUBLE_EQ(entry.probability, 0.25);
        if (entry.card.suit == Spades)
        {
            ++spade_count;
        }
        else if (entry.card.suit == Hearts)
        {
            ++heart_count;
        }
    }
    EXPECT_EQ(spade_count, 2);
    EXPECT_EQ(heart_count, 2);
    EXPECT_EQ(
        be::validate_defender_distribution(deal, East, all_optimal_distribution),
        be::ValidationError::None);
}

// --- a non-zero solve_board status --------------------------------------

TEST_F(DoubleDummyDefenderTest, ANonZeroSolveBoardStatusSurfacesAsARejectedEmptyDistribution)
{
    // A malformed deal -- the same card (the ace of spades) held by two
    // hands at once -- which solve_board rejects outright rather than
    // solving. DoubleDummyDefender does not invent a second error-reporting
    // path: it returns an empty distribution, and the existing
    // validate_defender_distribution rejects that (ProbabilitiesDoNotSumToOne)
    // exactly as expand_defender_node would for any other invalid script,
    // which is what actually surfaces the error to a caller.
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = North;
    deal.remainCards[North][Spades] = be::holding({Ace});
    deal.remainCards[East][Spades] = be::holding({Ace});  // duplicate
    deal.remainCards[South][Spades] = be::holding({Three});
    deal.remainCards[West][Spades] = be::holding({Four});

    SolverContext ctx;
    be::DoubleDummyDefender defender(ctx);
    std::vector<be::WeightedCard> const distribution =
        defender.as_strategy()(be::DefenderQuery{deal, North, be::ObservationState{}});

    EXPECT_TRUE(distribution.empty());
    EXPECT_EQ(
        be::validate_defender_distribution(deal, North, distribution),
        be::ValidationError::ProbabilitiesDoNotSumToOne);
}

// --- the collapse: no touching sequences anywhere reduces to a
// deterministic defender, reproducing a scripted-delta result exactly ------

TEST_F(DoubleDummyDefenderTest, NoTouchingSequencesAnywhereReproducesAScriptedResultExactly)
{
    // A one-trick ending, one card per hand: no hand holds more than one
    // card of any suit, so no touching sequence is even possible anywhere
    // in the deal. East's only legal card is the king -- spread() must
    // return it at probability 1 regardless of policy, identically to a
    // defender scripted to always play its lowest (here, only) legal card.
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = East;
    deal.remainCards[North][Spades] = be::holding({Ace});
    deal.remainCards[East][Spades] = be::holding({King});
    deal.remainCards[South][Spades] = be::holding({Three});
    deal.remainCards[West][Spades] = be::holding({Two});

    be::VectorLayoutSource source({deal});
    be::DeclarerStrategy const pi{.id = 1, .play = be::single_card_declarer_play, .state_key = nullptr};

    SolverContext ctx;
    be::DoubleDummyDefender dd_defender(ctx, be::SpreadPolicy::TouchingSequence);
    be::EvaluationResult const dd_result =
        be::evaluate(deal, North, /*tricks_needed=*/1, source, pi, dd_defender.as_strategy());

    be::EvaluationResult const scripted_result =
        be::evaluate(deal, North, /*tricks_needed=*/1, source, pi, be::single_card_defender);

    ASSERT_FALSE(dd_result.error.has_value());
    ASSERT_FALSE(scripted_result.error.has_value());
    // Bitwise, not EXPECT_DOUBLE_EQ: the whole point is that the stochastic
    // machinery performs the identical arithmetic as the deterministic path
    // in this degenerate case, not merely an approximately equal one.
    EXPECT_EQ(dd_result.by_strategy.at(1u).p_make, scripted_result.by_strategy.at(1u).p_make);
}
