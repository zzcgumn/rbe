#include <gtest/gtest.h>

#include <type_traits>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>
#include <api/solve_board.hpp>
#include <solver_context/solver_context.hpp>

#include <belief_evaluation/double_dummy_defender.hpp>
#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/validation.hpp>

#include "test_support.hpp"

// Tests for DoubleDummyDefender against real solve_board calls: best
// cards, touching sequences, the two spread policies and solver failures.

// Unlike the rest of the suite, this file builds a raw SolverContext, so
// it sees both ::Card from api/dds.h and dds::belief_evaluation::Card. The
// two are layout-identical, so an unqualified `Card` would compile as
// either with no error. Qualifying through this alias, not `using
// namespace`, keeps every module type unambiguous.
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
    // A one-trick ending: East holds only the king of spades. Whatever the
    // trick's outcome, it is East's only legal play, so double dummy has one
    // answer.
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

// ::Card from api/dds.h and be::Card are both visible here, and this test
// asserts that they coexist. They are layout-identical (same members,
// types and order), so this compiles only because they are two distinct
// types.
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
    // spades (its only trump). North holds the king of clubs (its only club)
    // and the three of hearts. South holds the three of spades (its only
    // trump) and the ace of hearts. West holds the four of spades (its only
    // trump) and the four of hearts. Trick order is East, South, West, North.
    //
    // Leading the ace of clubs: if South ruffs with the three of spades, West
    // overruffs with the four, and North must follow with the king. West then
    // leads the four of hearts, North follows, and East, void in hearts, ruffs
    // with the two of spades over South's ace. If South discards the ace of
    // hearts instead, West discards too and East's ace wins; East's two of
    // spades then draws South's three, and West's four wins. East's side: 2
    // tricks either way.
    //
    // Leading the two of spades: South and West must follow with the three and
    // four, and North discards. West's four wins, but neither defender has a
    // trump left. South's ace of hearts wins the last trick. East's side: 1
    // trick.
    //
    // So the ace of clubs is strictly better: it lets the defenders' trumps
    // win tricks instead of falling on each other.
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
    // Pins the reason double_dummy_defender.cpp uses solutions = 2. Two suits
    // tie for the maximum score here (the fixture of
    // TwoTiedSuitsDistinguishTouchingSequenceFromAllOptimal below).
    // solutions = 2 reports both tied entries, with the same scores and equals
    // groups that solutions = 3 gives; the test checks this below. spread()
    // uses only entries at the maximum score: TouchingSequence and AllOptimal
    // both filter to it first. So solutions = 2 gives spread() all it needs.
    // The extra entries from solutions = 3 (every legal card) are sub-optimal,
    // and that filter discards them.
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
        EXPECT_EQ(two_solutions.equals[i], 1 << King);  // the suit's king, touching its ace
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

    // solutions = 3 must agree with solutions = 2 on this deal, checked rather
    // than assumed.
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
    // North holds the king and queen of spades, a touching pair. East, South
    // and West hold low cards. Playing either honour leaves an isomorphic
    // position: an order-preserving bijection within the suit keeps every
    // rule of trick-taking. So double dummy reports one entry (the king) with
    // the queen in its equals group, and TouchingSequence spreads 0.5/0.5 over
    // the two.
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
    // East holds the ace-king of spades and the ace-king of hearts: two
    // separate touching pairs. North, South and West hold low fillers in all
    // four suits, so nothing else is live. Whichever suit East leads, its side
    // takes both top cards of that suit, in either order, and then of the
    // other. So double dummy ties both suits at the same score. This case
    // separates the two policies: TouchingSequence spreads only over the
    // canonical suit's pair, AllOptimal over both.
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
    // A malformed deal: two hands hold the ace of spades. solve_board rejects
    // it without solving. DoubleDummyDefender then returns an empty
    // distribution, the same shape any failing defender strategy returns; it
    // has no second error path. This test calls
    // validate_defender_distribution directly, a pure check of a distribution
    // against a layout. An empty distribution fails its sum-to-one check:
    // ProbabilitiesDoNotSumToOne. Through expand_defender_node, the same input
    // gives ValidationError::DistributionEmpty instead (see its doxygen).
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

TEST_F(DoubleDummyDefenderTest, ASolverFailureThroughExpandDefenderNodeSurfacesAsDistributionEmpty)
{
    // The same malformed deal as the test above, now through
    // expand_defender_node, the path a real caller takes. There, the solver
    // failure must surface as ValidationError::DistributionEmpty, not
    // ProbabilitiesDoNotSumToOne, because expand_defender_node checks for an
    // empty distribution first.
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = North;
    deal.remainCards[North][Spades] = be::holding({Ace});
    deal.remainCards[East][Spades] = be::holding({Ace});  // duplicate
    deal.remainCards[South][Spades] = be::holding({Three});
    deal.remainCards[West][Spades] = be::holding({Four});

    be::BeliefNode node{};
    node.state.trump = deal.trump;
    node.state.first = deal.first;
    node.state.declarer = South;  // arbitrary: expand_defender_node never consults it
    node.state.tricks_needed = 1;
    node.state.known_holdings = deal;
    node.layouts = {deal};
    node.p = {1.0};
    // West is (declarer + 1) % DDS_HANDS, as in make_root. The value plays no
    // role: expand_defender_node fails before it reads root_keys.
    node.root_keys = {be::layout_key(deal, West)};
    node.kappa = 1.0;

    SolverContext ctx;
    be::DoubleDummyDefender defender(ctx);
    be::ExpandDefenderResult const result = be::expand_defender_node(node, defender.as_strategy());

    EXPECT_FALSE(result.children.has_value());
    EXPECT_EQ(result.error, be::ValidationError::DistributionEmpty);
    EXPECT_EQ(result.offending_layout.remainCards[North][Spades], deal.remainCards[North][Spades]);
}

// --- the collapse: no touching sequences anywhere reduces to a
// deterministic defender, reproducing a scripted-delta result exactly ------

TEST_F(DoubleDummyDefenderTest, NoTouchingSequencesAnywhereReproducesAScriptedResultExactly)
{
    // A one-trick ending, one card per hand: no hand holds two cards of a
    // suit, so no touching sequence is possible. East's only legal card is the
    // king. spread() must return it at probability 1 under either policy, the
    // same as a defender scripted to play its lowest (here, only) legal
    // card.
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
    // Bitwise, not EXPECT_DOUBLE_EQ: in this degenerate case the stochastic
    // path must do the same arithmetic as the deterministic one, not merely
    // get close.
    EXPECT_EQ(dd_result.by_strategy.at(1u).p_make, scripted_result.by_strategy.at(1u).p_make);
}
