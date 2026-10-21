#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/default_defender_heuristics.hpp>
#include <belief_evaluation/defender_heuristic.hpp>
#include <belief_evaluation/discard_keep_winners.hpp>
#include <belief_evaluation/fourth_seat_low.hpp>
#include <belief_evaluation/high_in_third.hpp>
#include <belief_evaluation/ruff_small.hpp>
#include <belief_evaluation/second_seat_low.hpp>
#include <belief_evaluation/third_seat_low.hpp>
#include <belief_evaluation/types.hpp>

#include "test_support.hpp"

// Tests for the defender heuristic context, each defender rule, and the
// default rule chains for notrump and trump contracts.

namespace be = dds::belief_evaluation;

namespace
{
    constexpr int Two = 2;
    constexpr int Three = 3;
    constexpr int Four = 4;
    constexpr int Five = 5;
    constexpr int Six = 6;
    constexpr int Nine = 9;
    constexpr int Ten = 10;
    constexpr int Jack = 11;
    constexpr int Queen = 12;
    constexpr int King = 13;
    constexpr int Ace = 14;

    constexpr int Spades = 0;
    constexpr int Hearts = 1;
    constexpr int Diamonds = 2;
    constexpr int Clubs = 3;
    constexpr int NoTrump = DDS_NOTRUMP;

    constexpr int North = 0;
    constexpr int East = 1;
    constexpr int South = 2;
    constexpr int West = 3;
}

class MakeDefenderHeuristicContextTest : public ::testing::Test
{
};

TEST_F(MakeDefenderHeuristicContextTest, AtTheLeadPositionInTrickIsZero)
{
    // Nothing yet on the trick: seat_on_play(deal) == deal.first == East.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = East;
    deal.remainCards[East][Spades] = be::holding({Ace});

    be::ObservationState state{};
    state.declarer = North;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, East, fut);

    EXPECT_EQ(ctx.position_in_trick, 0);
    EXPECT_TRUE(ctx.was_on_lead);
    EXPECT_EQ(ctx.on_lead_to_trick, East);
}

TEST_F(MakeDefenderHeuristicContextTest, AfterTwoCardsPositionInTrickIsTwo)
{
    // East led, South followed: seat_on_play(deal) == (East + 2) % 4 == West.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = East;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Ace;
    deal.currentTrickSuit[1] = Spades;
    deal.currentTrickRank[1] = Two;
    deal.remainCards[West][Spades] = be::holding({Three});

    be::ObservationState state{};
    state.declarer = North;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, West, fut);

    EXPECT_EQ(ctx.position_in_trick, 2);
    EXPECT_FALSE(ctx.was_on_lead);
}

TEST_F(MakeDefenderHeuristicContextTest, CanFollowLedSuitIsTrueWhenTheSeatHoldsTheLedSuit)
{
    // East led a spade; South (seat_on_play after one card) holds a spade.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = East;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Ace;
    deal.remainCards[South][Spades] = be::holding({Three});
    deal.remainCards[South][Hearts] = be::holding({Three});

    be::ObservationState state{};
    state.declarer = North;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, South, fut);

    EXPECT_TRUE(ctx.can_follow_led_suit);
}

TEST_F(MakeDefenderHeuristicContextTest, CanFollowLedSuitIsFalseWhenVoid)
{
    // East led a spade; South, this time, holds no spade at all.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = East;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Ace;
    deal.remainCards[South][Hearts] = be::holding({Three});

    be::ObservationState state{};
    state.declarer = North;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, South, fut);

    EXPECT_FALSE(ctx.can_follow_led_suit);
}

TEST_F(MakeDefenderHeuristicContextTest, DefendingSideIsBasedOnSeatParityAgainstDeclarer)
{
    // North declares (dummy is South, its partner); East and West defend.
    // Four separate one-card decks, each leading from the seat under test,
    // so seat_on_play(deal) == deal.first == that seat for every case.
    be::ObservationState state{};
    state.declarer = North;
    FutureTricks fut{};

    for (auto const [seat, expected_defending] :
         {std::pair{North, false}, std::pair{East, true}, std::pair{South, false}, std::pair{West, true}})
    {
        Deal deal{};
        deal.trump = NoTrump;
        deal.first = seat;
        deal.remainCards[seat][Spades] = be::holding({Ace});

        be::DefenderHeuristicContext const ctx =
            be::make_defender_heuristic_context(deal, state, seat, fut);

        EXPECT_EQ(ctx.defending_side, expected_defending) << "seat = " << seat;
    }
}

TEST_F(MakeDefenderHeuristicContextTest, TrumpIsCopiedFromTheLayout)
{
    Deal deal{};
    deal.trump = Hearts;
    deal.first = East;
    deal.remainCards[East][Spades] = be::holding({Ace});

    be::ObservationState state{};
    state.declarer = North;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, East, fut);

    EXPECT_EQ(ctx.trump, Hearts);
}

TEST_F(MakeDefenderHeuristicContextTest, FutIsCarriedThroughUnchanged)
{
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = East;
    deal.remainCards[East][Spades] = be::holding({Ace});

    be::ObservationState state{};
    state.declarer = North;

    FutureTricks fut{};
    fut.cards = 1;
    fut.suit[0] = Spades;
    fut.rank[0] = Ace;
    fut.equals[0] = 1 << Queen;
    fut.score[0] = 1;

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, East, fut);

    // The same object, not an equal copy.
    EXPECT_EQ(&ctx.fut, &fut);
    EXPECT_EQ(ctx.fut.equals[0], 1 << Queen);
}

// --- second_seat_low -----------------------------------------------------

namespace
{
    // East declares, West is dummy (declarer + 2). West leads: the second
    // seat to act is North, a defender (North's seat parity differs from
    // East's).
    constexpr int Declarer = East;
    constexpr int Leader = West;
    constexpr int SecondSeat = North;
}

class SecondSeatLowTest : public ::testing::Test
{
};

TEST_F(SecondSeatLowTest, FiresOnlyInSecondSeatFollowingSuitAsADefender)
{
    be::DefenderHeuristic const rule = be::second_seat_low();

    // Positive: second seat, following suit, defending.
    {
        Deal deal{};
        deal.trump = NoTrump;
        deal.first = Leader;
        deal.currentTrickSuit[0] = Spades;
        deal.currentTrickRank[0] = Three;
        deal.remainCards[SecondSeat][Spades] = be::holding({Two, King});
        be::ObservationState state{};
        state.declarer = Declarer;
        FutureTricks fut{};
        be::DefenderHeuristicContext const ctx =
            be::make_defender_heuristic_context(deal, state, SecondSeat, fut);
        std::vector<be::Card> const best_cards{be::Card{Spades, Two}, be::Card{Spades, King}};

        std::optional<be::Card> const result = rule(ctx, best_cards);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->rank, Two);
    }

    // Negative: wrong position. The seat on play leads the trick (position
    // 0): a defender the trick has not reached yet.
    {
        Deal deal{};
        deal.trump = NoTrump;
        deal.first = SecondSeat;  // North on lead this time: position 0
        deal.remainCards[SecondSeat][Spades] = be::holding({Two});
        be::ObservationState state{};
        state.declarer = Declarer;
        FutureTricks fut{};
        be::DefenderHeuristicContext const ctx =
            be::make_defender_heuristic_context(deal, state, SecondSeat, fut);
        std::vector<be::Card> const best_cards{be::Card{Spades, Two}};
        EXPECT_FALSE(rule(ctx, best_cards).has_value());
    }

    // Negative: wrong position (third seat, not second). North leads,
    // East (declarer) follows, South (a defender) is on play at position 2.
    {
        Deal deal{};
        deal.trump = NoTrump;
        deal.first = North;
        deal.currentTrickSuit[0] = Spades;
        deal.currentTrickRank[0] = Three;
        deal.currentTrickSuit[1] = Spades;
        deal.currentTrickRank[1] = Four;
        deal.remainCards[South][Spades] = be::holding({Two, King});
        be::ObservationState state{};
        state.declarer = Declarer;
        FutureTricks fut{};
        be::DefenderHeuristicContext const ctx =
            be::make_defender_heuristic_context(deal, state, South, fut);
        std::vector<be::Card> const best_cards{be::Card{Spades, Two}, be::Card{Spades, King}};
        EXPECT_FALSE(rule(ctx, best_cards).has_value());
    }

    // Negative: cannot follow suit (void in the led suit).
    {
        Deal deal{};
        deal.trump = NoTrump;
        deal.first = Leader;
        deal.currentTrickSuit[0] = Spades;
        deal.currentTrickRank[0] = Three;
        deal.remainCards[SecondSeat][Hearts] = be::holding({Two});
        be::ObservationState state{};
        state.declarer = Declarer;
        FutureTricks fut{};
        be::DefenderHeuristicContext const ctx =
            be::make_defender_heuristic_context(deal, state, SecondSeat, fut);
        std::vector<be::Card> const best_cards{be::Card{Hearts, Two}};
        EXPECT_FALSE(rule(ctx, best_cards).has_value());
    }

    // Negative: declaring side. The seat at this position is declarer, not a
    // defender.
    {
        Deal deal{};
        deal.trump = NoTrump;
        deal.first = East;
        deal.currentTrickSuit[0] = Spades;
        deal.currentTrickRank[0] = Three;
        deal.remainCards[South][Spades] = be::holding({Two});
        be::ObservationState state{};
        state.declarer = South;  // the second seat (South) is declarer itself
        FutureTricks fut{};
        be::DefenderHeuristicContext const ctx =
            be::make_defender_heuristic_context(deal, state, South, fut);
        std::vector<be::Card> const best_cards{be::Card{Spades, Two}};
        EXPECT_FALSE(rule(ctx, best_cards).has_value());
    }
}

TEST_F(SecondSeatLowTest, ReturnsTheLowestCandidateWhenNoTouchingGroup)
{
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = Leader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[SecondSeat][Spades] = be::holding({Two, King});
    be::ObservationState state{};
    state.declarer = Declarer;

    // Two and King are not adjacent in rank, so no gone card can join them
    // into one touching group. equals is zero for both.
    FutureTricks fut{};
    fut.cards = 2;
    fut.suit[0] = Spades;
    fut.rank[0] = Two;
    fut.equals[0] = 0;
    fut.suit[1] = Spades;
    fut.rank[1] = King;
    fut.equals[1] = 0;

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, SecondSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Two}, be::Card{Spades, King}};

    std::optional<be::Card> const result = be::second_seat_low()(ctx, best_cards);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->rank, Two);
}

TEST_F(SecondSeatLowTest, DefersWhenTheLowestTouchesAnotherCandidateAndRandomizingIsOn)
{
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = Leader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[SecondSeat][Spades] = be::holding({Queen, Ace});
    be::ObservationState state{};
    state.declarer = Declarer;

    // best_cards[0] (Queen, the lowest) touches best_cards[1] (Ace): the
    // Ace's equals bit names the Queen, as dds reports it when the king is
    // gone from play (checked against the real solver).
    FutureTricks fut{};
    fut.cards = 2;
    fut.suit[0] = Spades;
    fut.rank[0] = Queen;
    fut.equals[0] = 0;
    fut.suit[1] = Spades;
    fut.rank[1] = Ace;
    fut.equals[1] = 1 << Queen;

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, SecondSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Queen}, be::Card{Spades, Ace}};

    EXPECT_FALSE(be::second_seat_low(/*randomize_touching_honors=*/true)(ctx, best_cards).has_value());
}

TEST_F(SecondSeatLowTest, DoesNotDeferWhenTheFallbackPolicyIsNotTouchingSequence)
{
    // The same fixture as the deferring test above, except the fallback
    // policy passed to make_defender_heuristic_context is
    // SpreadPolicy::AllOptimal, not the default TouchingSequence. Deferring
    // would hand the choice to spread(fut, AllOptimal), which spreads over
    // every tied candidate in every suit, not this queen/ace group: not
    // restricted choice. So the rule must return its own answer, as with
    // randomize_touching_honors=false.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = Leader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[SecondSeat][Spades] = be::holding({Queen, Ace});
    be::ObservationState state{};
    state.declarer = Declarer;

    FutureTricks fut{};
    fut.cards = 2;
    fut.suit[0] = Spades;
    fut.rank[0] = Queen;
    fut.equals[0] = 0;
    fut.suit[1] = Spades;
    fut.rank[1] = Ace;
    fut.equals[1] = 1 << Queen;

    be::DefenderHeuristicContext const ctx = be::make_defender_heuristic_context(
        deal, state, SecondSeat, fut, be::SpreadPolicy::AllOptimal);
    std::vector<be::Card> const best_cards{be::Card{Spades, Queen}, be::Card{Spades, Ace}};

    std::optional<be::Card> const result = be::second_seat_low(/*randomize_touching_honors=*/true)(ctx, best_cards);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->rank, Queen);
}

TEST_F(SecondSeatLowTest, AlwaysReturnsTheLowestWhenRandomizingIsOff)
{
    // The same fixture as the test above except for the one constructor
    // argument. This shows the argument has an effect.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = Leader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[SecondSeat][Spades] = be::holding({Queen, Ace});
    be::ObservationState state{};
    state.declarer = Declarer;

    FutureTricks fut{};
    fut.cards = 2;
    fut.suit[0] = Spades;
    fut.rank[0] = Queen;
    fut.equals[0] = 0;
    fut.suit[1] = Spades;
    fut.rank[1] = Ace;
    fut.equals[1] = 1 << Queen;

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, SecondSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Queen}, be::Card{Spades, Ace}};

    std::optional<be::Card> const result =
        be::second_seat_low(/*randomize_touching_honors=*/false)(ctx, best_cards);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->rank, Queen);
}

TEST_F(SecondSeatLowTest, DetectsTouchingWhenSolveBoardCollapsesTheGroupToOneEntry)
{
    // The shape solve_board returns for a plain touching pair, unlike the
    // two-entry shape built by hand above. There is ONE fut entry (dds lists
    // the highest card of a touching run), and the lower card is folded into
    // that entry's equals. best_cards, built one entry per fut index as
    // HeuristicDefender builds it, also has one member. Checked against a real
    // solve_board call (in main() of restricted_choice_6nt_belief_space.py).
    // A chain with this bug returns the queen at probability 1.0 under both
    // randomize_touching_honors settings: it never sees the fold.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = Leader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[SecondSeat][Spades] = be::holding({Queen, Jack});
    be::ObservationState state{};
    state.declarer = Declarer;

    FutureTricks fut{};
    fut.cards = 1;
    fut.suit[0] = Spades;
    fut.rank[0] = Queen;
    fut.equals[0] = 1 << Jack;

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, SecondSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Queen}};

    EXPECT_FALSE(be::second_seat_low(/*randomize_touching_honors=*/true)(ctx, best_cards).has_value());

    std::optional<be::Card> const fixed = be::second_seat_low(/*randomize_touching_honors=*/false)(ctx, best_cards);
    ASSERT_TRUE(fixed.has_value());
    EXPECT_EQ(fixed->suit, Spades);
    EXPECT_EQ(fixed->rank, Jack)
        << "the true lowest card in the group is the jack, folded into "
           "the queen's own equals -- not the queen itself, which is "
           "only the group's canonical representative";
}

TEST_F(SecondSeatLowTest, DoesNotDeferWhenTheLowestGroupIsNotTheOneSpreadWouldPick)
{
    // Two separate touching groups tie for best score: ace-king at fut index
    // 0 (dds lists it first) and queen-jack at index 1. The lowest candidate
    // is the queen, this rule's pick, which does touch the jack. But
    // spread(fut, SpreadPolicy::TouchingSequence) always resolves to
    // canonical_best(fut), index 0 here (the first entry tied for the top
    // score): the ace-king pair. Deferring would randomise the wrong pair, so
    // the rule must return its own answer (the jack), as with
    // randomize_touching_honors=false.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = Leader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[SecondSeat][Spades] = be::holding({Ace, King, Queen, Jack});
    be::ObservationState state{};
    state.declarer = Declarer;

    FutureTricks fut{};
    fut.cards = 2;
    fut.suit[0] = Spades;
    fut.rank[0] = Ace;
    fut.equals[0] = 1 << King;
    fut.score[0] = 5;
    fut.suit[1] = Spades;
    fut.rank[1] = Queen;
    fut.equals[1] = 1 << Jack;
    fut.score[1] = 5;  // tied with index 0; canonical_best keeps index 0

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, SecondSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Ace}, be::Card{Spades, Queen}};

    std::optional<be::Card> const result = be::second_seat_low(/*randomize_touching_honors=*/true)(ctx, best_cards);
    ASSERT_TRUE(result.has_value())
        << "deferring here would hand the decision to spread(), which "
           "would randomise the ace/king pair instead of this rule's own "
           "queen/jack pair -- not what randomize_touching_honors=true "
           "is supposed to do";
    EXPECT_EQ(result->suit, Spades);
    EXPECT_EQ(result->rank, Jack);
}

// --- high_in_third / third_seat_low ---------------------------------------
//
// South leads, West (dummy) plays second, North (a defender) third, East
// (declarer) last. South and North are the defenders; East and West are
// declarer and dummy. Dummy's card is visible by North's turn, which
// "high in third" needs.

namespace
{
    constexpr int ThirdSeatDeclarer = East;
    constexpr int ThirdSeatLeader = South;
    constexpr int ThirdSeat = North;
}

class HighInThirdAndThirdSeatLowTest : public ::testing::Test
{
};

TEST_F(HighInThirdAndThirdSeatLowTest, HighInThirdFiresWhenPartnerLedAndDummysCardIsBeatable)
{
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = ThirdSeatLeader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.currentTrickSuit[1] = Spades;
    deal.currentTrickRank[1] = Jack;  // dummy's card, beatable by the ace
    deal.remainCards[ThirdSeat][Spades] = be::holding({Two, Ace});

    be::ObservationState state{};
    state.declarer = ThirdSeatDeclarer;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, ThirdSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Two}, be::Card{Spades, Ace}};

    std::optional<be::Card> const high_result = be::high_in_third()(ctx, best_cards);
    ASSERT_TRUE(high_result.has_value());
    EXPECT_EQ(high_result->rank, Ace);

    // Same context: third_seat_low must defer, so the rules partition the cases.
    EXPECT_FALSE(be::third_seat_low()(ctx, best_cards).has_value());
}

TEST_F(HighInThirdAndThirdSeatLowTest, ThirdSeatLowFiresWhenDummysCardAlreadyBeatsEveryCandidate)
{
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = ThirdSeatLeader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.currentTrickSuit[1] = Spades;
    deal.currentTrickRank[1] = Ace;  // dummy's card, beats every candidate below
    deal.remainCards[ThirdSeat][Spades] = be::holding({Two, King});

    be::ObservationState state{};
    state.declarer = ThirdSeatDeclarer;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, ThirdSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Two}, be::Card{Spades, King}};

    EXPECT_FALSE(be::high_in_third()(ctx, best_cards).has_value());

    std::optional<be::Card> const low_result = be::third_seat_low()(ctx, best_cards);
    ASSERT_TRUE(low_result.has_value());
    EXPECT_EQ(low_result->rank, Two);
}

TEST_F(HighInThirdAndThirdSeatLowTest, ThirdSeatLowReturnsTheTrueLowestCandidateWhenSolveBoardCollapsesTheGroupToOneEntry)
{
    // The collapsed shape of fourth_seat_low's regression test: one fut entry
    // (the king, dds's representative) with the queen folded into its equals.
    // Dummy's ace beats the king, so high_in_third defers and third_seat_low
    // must fire. The group's true lowest card is the queen, not the king it is
    // listed as.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = ThirdSeatLeader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.currentTrickSuit[1] = Spades;
    deal.currentTrickRank[1] = Ace;  // dummy's card, beats the king
    deal.remainCards[ThirdSeat][Spades] = be::holding({Queen, King});
    be::ObservationState state{};
    state.declarer = ThirdSeatDeclarer;

    FutureTricks fut{};
    fut.cards = 1;
    fut.suit[0] = Spades;
    fut.rank[0] = King;
    fut.equals[0] = 1 << Queen;

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, ThirdSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, King}};

    EXPECT_FALSE(be::high_in_third()(ctx, best_cards).has_value());

    std::optional<be::Card> const low_result = be::third_seat_low()(ctx, best_cards);
    ASSERT_TRUE(low_result.has_value());
    EXPECT_EQ(low_result->suit, Spades);
    EXPECT_EQ(low_result->rank, Queen)
        << "the true lowest card in the group is the queen, folded into "
           "the king's own equals -- not the king itself, which is only "
           "the group's canonical representative";
}

TEST_F(HighInThirdAndThirdSeatLowTest, ComparesAgainstTheSecondPlayersCardWhenItIsDeclarerNotDummy)
{
    // The fixture above has EAST declare, which puts WEST (dummy) second on
    // the trick: the only case where deriving dummy's position from its seat
    // stays inside currentTrickRank's three elements. With WEST declaring,
    // EAST (dummy) plays fourth and WEST (declarer) second. The partner-led,
    // third-seat gate still holds, but deriving dummy's position from its seat
    // gives 3, one past the bound. This fixture checks that the rule compares
    // against trick index 1 directly, whoever sits there.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = ThirdSeatLeader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.currentTrickSuit[1] = Spades;
    deal.currentTrickRank[1] = Jack;  // West (declarer here), following suit
    deal.remainCards[ThirdSeat][Spades] = be::holding({Two, Ace});

    be::ObservationState state{};
    state.declarer = West;

    FutureTricks fut{};
    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, ThirdSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Two}, be::Card{Spades, Ace}};

    std::optional<be::Card> const high_result = be::high_in_third()(ctx, best_cards);
    ASSERT_TRUE(high_result.has_value());
    EXPECT_EQ(high_result->rank, Ace);
    EXPECT_FALSE(be::third_seat_low()(ctx, best_cards).has_value());
}

TEST_F(HighInThirdAndThirdSeatLowTest, ASecondHandRuffBeatsEveryCandidateRegardlessOfRank)
{
    // West, void in spades, ruffs with a trump. No card that follows suit can
    // beat a trump, so high_in_third must defer (rising cannot help) and
    // third_seat_low must fire, however high North's candidates are.
    Deal deal{};
    deal.trump = Hearts;
    deal.first = ThirdSeatLeader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.currentTrickSuit[1] = Hearts;
    deal.currentTrickRank[1] = Four;  // West's ruff
    deal.remainCards[ThirdSeat][Spades] = be::holding({Ace});

    be::ObservationState state{};
    state.declarer = West;

    FutureTricks fut{};
    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, ThirdSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Ace}};

    EXPECT_FALSE(be::high_in_third()(ctx, best_cards).has_value());

    std::optional<be::Card> const low_result = be::third_seat_low()(ctx, best_cards);
    ASSERT_TRUE(low_result.has_value());
    EXPECT_EQ(low_result->rank, Ace);
}

TEST_F(HighInThirdAndThirdSeatLowTest, ASecondHandDiscardOfAThirdSuitBeatsNoCandidate)
{
    // West, void in spades, discards a card of a third suit (there is no
    // trump). That card does not contest the trick, so high_in_third must fire
    // however low North's candidate is.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = ThirdSeatLeader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.currentTrickSuit[1] = Diamonds;
    deal.currentTrickRank[1] = Two;  // West's discard, no trump exists
    deal.remainCards[ThirdSeat][Spades] = be::holding({Jack});

    be::ObservationState state{};
    state.declarer = West;

    FutureTricks fut{};
    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, ThirdSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Jack}};

    std::optional<be::Card> const high_result = be::high_in_third()(ctx, best_cards);
    ASSERT_TRUE(high_result.has_value());
    EXPECT_EQ(high_result->rank, Jack);
}

TEST_F(HighInThirdAndThirdSeatLowTest, NeitherFiresOutsideThirdSeat)
{
    // North itself on lead: position 0, not 2.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = ThirdSeat;
    deal.remainCards[ThirdSeat][Spades] = be::holding({Ace});

    be::ObservationState state{};
    state.declarer = ThirdSeatDeclarer;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, ThirdSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Ace}};

    EXPECT_FALSE(be::high_in_third()(ctx, best_cards).has_value());
    EXPECT_FALSE(be::third_seat_low()(ctx, best_cards).has_value());
}

TEST_F(HighInThirdAndThirdSeatLowTest, NeitherFiresWhenVoid)
{
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = ThirdSeatLeader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.currentTrickSuit[1] = Spades;
    deal.currentTrickRank[1] = Jack;
    deal.remainCards[ThirdSeat][Hearts] = be::holding({Two});  // void in spades

    be::ObservationState state{};
    state.declarer = ThirdSeatDeclarer;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, ThirdSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Hearts, Two}};

    EXPECT_FALSE(be::high_in_third()(ctx, best_cards).has_value());
    EXPECT_FALSE(be::third_seat_low()(ctx, best_cards).has_value());
}

TEST_F(HighInThirdAndThirdSeatLowTest, NeitherFiresOnTheDeclaringSide)
{
    // The first test's fixture, except the third seat (North) is declarer,
    // not a defender.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = ThirdSeatLeader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.currentTrickSuit[1] = Spades;
    deal.currentTrickRank[1] = Jack;
    deal.remainCards[ThirdSeat][Spades] = be::holding({Two, Ace});

    be::ObservationState state{};
    state.declarer = ThirdSeat;  // North is declarer, not a defender
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, ThirdSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Two}, be::Card{Spades, Ace}};

    EXPECT_FALSE(be::high_in_third()(ctx, best_cards).has_value());
    EXPECT_FALSE(be::third_seat_low()(ctx, best_cards).has_value());
}

// --- fourth_seat_low -------------------------------------------------------

namespace
{
    // East (declarer) leads; North, fourth to act, is a defender.
    constexpr int FourthSeatDeclarer = East;
    constexpr int FourthSeatLeader = East;
    constexpr int FourthSeat = North;
}

class FourthSeatLowTest : public ::testing::Test
{
};

TEST_F(FourthSeatLowTest, FiresOnlyInFourthSeatFollowingSuitAsADefender)
{
    be::DefenderHeuristic const rule = be::fourth_seat_low();

    // Positive: fourth seat, following suit, defending. Three cards are
    // already on the trick (any non-zero ranks), so seat_on_play(deal) reaches
    // the fourth seat.
    {
        Deal deal{};
        deal.trump = NoTrump;
        deal.first = FourthSeatLeader;
        deal.currentTrickSuit[0] = Spades;
        deal.currentTrickRank[0] = Three;
        deal.currentTrickSuit[1] = Hearts;
        deal.currentTrickRank[1] = Four;
        deal.currentTrickSuit[2] = Hearts;
        deal.currentTrickRank[2] = King;
        deal.remainCards[FourthSeat][Spades] = be::holding({Two, Queen});
        be::ObservationState state{};
        state.declarer = FourthSeatDeclarer;
        FutureTricks fut{};
        be::DefenderHeuristicContext const ctx =
            be::make_defender_heuristic_context(deal, state, FourthSeat, fut);
        std::vector<be::Card> const best_cards{be::Card{Spades, Two}, be::Card{Spades, Queen}};

        std::optional<be::Card> const result = rule(ctx, best_cards);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->rank, Two);
    }

    // Negative: wrong position (the seat on play is the trick leader
    // itself).
    {
        Deal deal{};
        deal.trump = NoTrump;
        deal.first = FourthSeat;
        deal.remainCards[FourthSeat][Spades] = be::holding({Two});
        be::ObservationState state{};
        state.declarer = FourthSeatDeclarer;
        FutureTricks fut{};
        be::DefenderHeuristicContext const ctx =
            be::make_defender_heuristic_context(deal, state, FourthSeat, fut);
        std::vector<be::Card> const best_cards{be::Card{Spades, Two}};
        EXPECT_FALSE(rule(ctx, best_cards).has_value());
    }

    // Negative: cannot follow suit (void in the led suit).
    {
        Deal deal{};
        deal.trump = NoTrump;
        deal.first = FourthSeatLeader;
        deal.currentTrickSuit[0] = Spades;
        deal.currentTrickRank[0] = Three;
        deal.currentTrickSuit[1] = Diamonds;
        deal.currentTrickRank[1] = Four;
        deal.currentTrickSuit[2] = Diamonds;
        deal.currentTrickRank[2] = King;
        deal.remainCards[FourthSeat][Hearts] = be::holding({Two});
        be::ObservationState state{};
        state.declarer = FourthSeatDeclarer;
        FutureTricks fut{};
        be::DefenderHeuristicContext const ctx =
            be::make_defender_heuristic_context(deal, state, FourthSeat, fut);
        std::vector<be::Card> const best_cards{be::Card{Hearts, Two}};
        EXPECT_FALSE(rule(ctx, best_cards).has_value());
    }

    // Negative: declaring side (the fourth seat is the declarer itself).
    {
        Deal deal{};
        deal.trump = NoTrump;
        deal.first = FourthSeatLeader;
        deal.currentTrickSuit[0] = Spades;
        deal.currentTrickRank[0] = Three;
        deal.currentTrickSuit[1] = Diamonds;
        deal.currentTrickRank[1] = Four;
        deal.currentTrickSuit[2] = Diamonds;
        deal.currentTrickRank[2] = King;
        deal.remainCards[FourthSeat][Spades] = be::holding({Two});
        be::ObservationState state{};
        state.declarer = FourthSeat;  // North is declarer, not a defender
        FutureTricks fut{};
        be::DefenderHeuristicContext const ctx =
            be::make_defender_heuristic_context(deal, state, FourthSeat, fut);
        std::vector<be::Card> const best_cards{be::Card{Spades, Two}};
        EXPECT_FALSE(rule(ctx, best_cards).has_value());
    }
}

TEST_F(FourthSeatLowTest, ReturnsTheLowestCandidate)
{
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = FourthSeatLeader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.currentTrickSuit[1] = Diamonds;
    deal.currentTrickRank[1] = Four;
    deal.currentTrickSuit[2] = Diamonds;
    deal.currentTrickRank[2] = King;
    deal.remainCards[FourthSeat][Spades] = be::holding({Three, Two, Queen});
    be::ObservationState state{};
    state.declarer = FourthSeatDeclarer;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, FourthSeat, fut);
    std::vector<be::Card> const best_cards{
        be::Card{Spades, Three}, be::Card{Spades, Two}, be::Card{Spades, Queen}};

    std::optional<be::Card> const result = be::fourth_seat_low()(ctx, best_cards);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->rank, Two);
}

TEST_F(FourthSeatLowTest, ReturnsTheTrueLowestCandidateWhenSolveBoardCollapsesTheGroupToOneEntry)
{
    // The collapsed shape of second_seat_low's regression test: solve_board
    // folds a touching run (queen and jack, king gone) into one entry (the
    // queen, dds's representative), with the jack in its equals. best_cards
    // has one member, and the group's true lowest card, the jack, is not
    // listed separately.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = FourthSeatLeader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.currentTrickSuit[1] = Diamonds;
    deal.currentTrickRank[1] = Four;
    deal.currentTrickSuit[2] = Diamonds;
    deal.currentTrickRank[2] = King;
    deal.remainCards[FourthSeat][Spades] = be::holding({Queen, Jack});
    be::ObservationState state{};
    state.declarer = FourthSeatDeclarer;

    FutureTricks fut{};
    fut.cards = 1;
    fut.suit[0] = Spades;
    fut.rank[0] = Queen;
    fut.equals[0] = 1 << Jack;

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, FourthSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Queen}};

    std::optional<be::Card> const result = be::fourth_seat_low()(ctx, best_cards);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->suit, Spades);
    EXPECT_EQ(result->rank, Jack)
        << "the true lowest card in the group is the jack, folded into "
           "the queen's own equals -- not the queen itself, which is "
           "only the group's canonical representative";
}

// --- discard_keep_winners --------------------------------------------------

namespace
{
    // East declares, West is dummy. North is a defender, void in the led
    // suit (hearts) and must discard.
    constexpr int DiscardDeclarer = East;
    // West on lead -> North (seat 0) is next to act, one card into the
    // trick: seat_on_play((West + 1) % 4) == North.
    constexpr int DiscardLeader = West;
    constexpr int Discarder = North;
}

class DiscardKeepWinnersTest : public ::testing::Test
{
};

TEST_F(DiscardKeepWinnersTest, FiresOnlyOnAGenuineDiscardAsADefender)
{
    be::DefenderHeuristic const rule = be::discard_keep_winners();

    // Positive: not on lead, void in the led suit, defending.
    {
        Deal deal{};
        deal.trump = NoTrump;
        deal.first = DiscardLeader;
        deal.currentTrickSuit[0] = Hearts;
        deal.currentTrickRank[0] = Three;
        deal.remainCards[Discarder][Spades] = be::holding({Two});
        be::ObservationState state{};
        state.declarer = DiscardDeclarer;
        FutureTricks fut{};
        be::DefenderHeuristicContext const ctx =
            be::make_defender_heuristic_context(deal, state, Discarder, fut);
        std::vector<be::Card> const best_cards{be::Card{Spades, Two}};
        EXPECT_TRUE(rule(ctx, best_cards).has_value());
    }

    // Negative: on lead.
    {
        Deal deal{};
        deal.trump = NoTrump;
        deal.first = Discarder;
        deal.remainCards[Discarder][Spades] = be::holding({Two});
        be::ObservationState state{};
        state.declarer = DiscardDeclarer;
        FutureTricks fut{};
        be::DefenderHeuristicContext const ctx =
            be::make_defender_heuristic_context(deal, state, Discarder, fut);
        std::vector<be::Card> const best_cards{be::Card{Spades, Two}};
        EXPECT_FALSE(rule(ctx, best_cards).has_value());
    }

    // Negative: can follow suit (not a discard).
    {
        Deal deal{};
        deal.trump = NoTrump;
        deal.first = DiscardLeader;
        deal.currentTrickSuit[0] = Hearts;
        deal.currentTrickRank[0] = Three;
        deal.remainCards[Discarder][Hearts] = be::holding({Two});
        be::ObservationState state{};
        state.declarer = DiscardDeclarer;
        FutureTricks fut{};
        be::DefenderHeuristicContext const ctx =
            be::make_defender_heuristic_context(deal, state, Discarder, fut);
        std::vector<be::Card> const best_cards{be::Card{Hearts, Two}};
        EXPECT_FALSE(rule(ctx, best_cards).has_value());
    }

    // Negative: declaring side.
    {
        Deal deal{};
        deal.trump = NoTrump;
        deal.first = DiscardLeader;
        deal.currentTrickSuit[0] = Hearts;
        deal.currentTrickRank[0] = Three;
        deal.remainCards[Discarder][Spades] = be::holding({Two});
        be::ObservationState state{};
        state.declarer = Discarder;  // North is declarer, not a defender
        FutureTricks fut{};
        be::DefenderHeuristicContext const ctx =
            be::make_defender_heuristic_context(deal, state, Discarder, fut);
        std::vector<be::Card> const best_cards{be::Card{Spades, Two}};
        EXPECT_FALSE(rule(ctx, best_cards).has_value());
    }
}

TEST_F(DiscardKeepWinnersTest, DiscardsFromTheSuitWithTheFewestDefendingSideTopTricks)
{
    // Spades: East holds the ace, so North's spade is not a winner: zero top
    // tricks. Diamonds: North holds the ace and South (partner) the king, with
    // nothing else out. One top trick, since the ace and king fall in the same
    // round, which is still more than zero. The rule must discard the spade,
    // not the diamond ace.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = DiscardLeader;
    deal.currentTrickSuit[0] = Hearts;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[Discarder][Spades] = be::holding({Two});
    deal.remainCards[East][Spades] = be::holding({Ace});
    deal.remainCards[Discarder][Diamonds] = be::holding({Ace});
    deal.remainCards[South][Diamonds] = be::holding({King});

    be::ObservationState state{};
    state.declarer = DiscardDeclarer;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, Discarder, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Two}, be::Card{Diamonds, Ace}};

    std::optional<be::Card> const result = be::discard_keep_winners()(ctx, best_cards);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->suit, Spades);
    EXPECT_EQ(result->rank, Two);
}

TEST_F(DiscardKeepWinnersTest, BreaksATieByRankThenSuit)
{
    // Clubs and diamonds both give zero top tricks (East holds both aces).
    // First best_cards offers a higher club and a lower diamond: rank breaks
    // the tie (the lower diamond wins). Then both suits offer the same rank:
    // suit breaks the tie (diamonds < clubs).
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = DiscardLeader;
    deal.currentTrickSuit[0] = Hearts;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[Discarder][Clubs] = be::holding({Three});
    deal.remainCards[East][Clubs] = be::holding({Ace});
    deal.remainCards[Discarder][Diamonds] = be::holding({Two});
    deal.remainCards[East][Diamonds] = be::holding({Ace});

    be::ObservationState state{};
    state.declarer = DiscardDeclarer;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, Discarder, fut);

    // Rank breaks the tie: the lower-ranked diamond two wins over the club three.
    {
        std::vector<be::Card> const best_cards{be::Card{Clubs, Three}, be::Card{Diamonds, Two}};
        std::optional<be::Card> const result = be::discard_keep_winners()(ctx, best_cards);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->suit, Diamonds);
        EXPECT_EQ(result->rank, Two);
    }

    // Same rank in both suits: suit itself breaks the tie (diamonds < clubs).
    {
        std::vector<be::Card> const best_cards{be::Card{Clubs, Two}, be::Card{Diamonds, Two}};
        std::optional<be::Card> const result = be::discard_keep_winners()(ctx, best_cards);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->suit, Diamonds);
    }
}

TEST_F(DiscardKeepWinnersTest, ResolvesTrueRankBeforeComparingAcrossDifferentSuits)
{
    // Unlike the second, third and fourth-seat rules, this one compares cards
    // from different suits. So comparing representative ranks of a collapsed
    // group can pick the wrong suit outright.
    //
    // Both suits give zero top tricks. East holds the diamond ace, which beats
    // North's only diamond, and both the spade ace and king, so neither of
    // North's spades gets through. (One ace stops only one spade, which is not
    // zero; that is why suit_top_tricks's regression tests need two stoppers.)
    // So rank alone breaks the tie. Spades collapse to one fut entry, the six,
    // with the two in its equals: the true lowest spade is the two. Diamonds
    // are a plain five. Representative ranks (six against five) pick
    // diamonds; true lowest ranks (two against five) must pick spades.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = DiscardLeader;
    deal.currentTrickSuit[0] = Hearts;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[Discarder][Spades] = be::holding({Two, Six});
    deal.remainCards[East][Spades] = be::holding({Ace, King});
    deal.remainCards[Discarder][Diamonds] = be::holding({Five});
    deal.remainCards[East][Diamonds] = be::holding({Ace});

    be::ObservationState state{};
    state.declarer = DiscardDeclarer;

    FutureTricks fut{};
    fut.cards = 2;
    fut.suit[0] = Spades;
    fut.rank[0] = Six;
    fut.equals[0] = 1 << Two;
    fut.suit[1] = Diamonds;
    fut.rank[1] = Five;

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, Discarder, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Six}, be::Card{Diamonds, Five}};

    std::optional<be::Card> const result = be::discard_keep_winners()(ctx, best_cards);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->suit, Spades)
        << "the true lowest rank (the spade two, folded into the six's "
           "own equals) beats the diamond five -- comparing by "
           "representative rank alone would have picked diamonds instead";
    EXPECT_EQ(result->rank, Two);
}

// --- ruff_small -------------------------------------------------------

namespace
{
    // Trump is hearts. West leads a spade. North (seat 0), one card into the
    // trick, is void in spades and holds two low hearts. East (declarer) and
    // South have not yet played.
    constexpr int RuffTrump = Hearts;
    constexpr int RuffLeader = West;
    constexpr int RuffSeat = North;
    constexpr int RuffDeclarer = East;  // dummy is West, the leader
    constexpr int RuffNotYetPlayedDeclaringSide = East;
    constexpr int RuffNotYetPlayedDefendingSide = South;
}

class RuffSmallTest : public ::testing::Test
{
};

TEST_F(RuffSmallTest, FiresWhenVoidInANotrumpContractNever)
{
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = RuffLeader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[RuffSeat][Hearts] = be::holding({Two, Three});

    be::ObservationState state{};
    state.declarer = RuffDeclarer;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, RuffSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Hearts, Two}, be::Card{Hearts, Three}};

    EXPECT_FALSE(be::ruff_small()(ctx, best_cards).has_value());
}

TEST_F(RuffSmallTest, ReturnsTheSmallestTrumpWhenNoOverruffIsPossible)
{
    Deal deal{};
    deal.trump = RuffTrump;
    deal.first = RuffLeader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[RuffSeat][Hearts] = be::holding({Two, Three});
    // Neither seat still to play can overruff: East can follow suit (it holds
    // a spade), and South holds no hearts.
    deal.remainCards[RuffNotYetPlayedDeclaringSide][Spades] = be::holding({Four});

    be::ObservationState state{};
    state.declarer = RuffDeclarer;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, RuffSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Hearts, Two}, be::Card{Hearts, Three}};

    std::optional<be::Card> const result = be::ruff_small()(ctx, best_cards);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->rank, Two);
}

TEST_F(RuffSmallTest, DoesNotFireOnTheDeclaringSide)
{
    // The fixture of ReturnsTheSmallestTrumpWhenNoOverruffIsPossible (same
    // void, same trumps, no overruff), except RuffSeat is declarer, not a
    // defender. Every other rule (second_seat_low, high_in_third,
    // third_seat_low, fourth_seat_low, discard_keep_winners) also gates on
    // ctx.defending_side, and this one must too. A DefenderHeuristicContext
    // can be built by hand for any seat, and nothing else stops this rule
    // returning a defensive ruff for a seat on the declaring side.
    Deal deal{};
    deal.trump = RuffTrump;
    deal.first = RuffLeader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[RuffSeat][Hearts] = be::holding({Two, Three});
    deal.remainCards[RuffNotYetPlayedDeclaringSide][Spades] = be::holding({Four});

    be::ObservationState state{};
    state.declarer = RuffSeat;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, RuffSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Hearts, Two}, be::Card{Hearts, Three}};

    EXPECT_FALSE(be::ruff_small()(ctx, best_cards).has_value());
}

TEST_F(RuffSmallTest, DetectsAnOverruffAgainstTheTrueLowestTrumpNotTheCollapsedRepresentative)
{
    // North's trumps collapse to one fut entry: the jack (dds's
    // representative), with the nine in its equals, since no one holds the
    // ten between them. The trump North would play is the nine, not the jack.
    // East's ten sits between them: it overruffs the nine but not the jack. A
    // rule that built the overruff mask from the representative rank (the
    // jack) would miss the threat and return a card instead of deferring.
    Deal deal{};
    deal.trump = RuffTrump;
    deal.first = RuffLeader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[RuffSeat][Hearts] = be::holding({Nine, Jack});
    deal.remainCards[RuffNotYetPlayedDeclaringSide][Hearts] = be::holding({Ten});

    be::ObservationState state{};
    state.declarer = RuffDeclarer;

    FutureTricks fut{};
    fut.cards = 1;
    fut.suit[0] = RuffTrump;
    fut.rank[0] = Jack;
    fut.equals[0] = 1 << Nine;

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, RuffSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{RuffTrump, Jack}};

    EXPECT_FALSE(be::ruff_small()(ctx, best_cards).has_value())
        << "East's ten overruffs the true lowest trump (the nine, folded "
           "into the jack's own equals), even though it cannot beat the "
           "jack itself";
}

TEST_F(RuffSmallTest, DefersWhenADeclaringSideSeatCanOverruff)
{
    Deal deal{};
    deal.trump = RuffTrump;
    deal.first = RuffLeader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[RuffSeat][Hearts] = be::holding({Two, Three});
    // East, declaring side, is void in spades and holds a higher trump.
    deal.remainCards[RuffNotYetPlayedDeclaringSide][Hearts] = be::holding({Four});

    be::ObservationState state{};
    state.declarer = RuffDeclarer;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, RuffSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Hearts, Two}, be::Card{Hearts, Three}};

    EXPECT_FALSE(be::ruff_small()(ctx, best_cards).has_value());
}

TEST_F(RuffSmallTest, DoesNotDeferToADefendingSideSeatsOverruffPotential)
{
    Deal deal{};
    deal.trump = RuffTrump;
    deal.first = RuffLeader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[RuffSeat][Hearts] = be::holding({Two, Three});
    // South, on the defending side, is void in spades and holds a much higher
    // trump. That does not matter to this rule's declaring-side gate.
    deal.remainCards[RuffNotYetPlayedDefendingSide][Hearts] = be::holding({Ace});
    // East (declaring) can follow suit, so cannot overruff either.
    deal.remainCards[RuffNotYetPlayedDeclaringSide][Spades] = be::holding({Four});

    be::ObservationState state{};
    state.declarer = RuffDeclarer;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, RuffSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Hearts, Two}, be::Card{Hearts, Three}};

    std::optional<be::Card> const result = be::ruff_small()(ctx, best_cards);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->rank, Two);
}

TEST_F(RuffSmallTest, FiresOnlyWhenVoidInTheLedSuit)
{
    Deal deal{};
    deal.trump = RuffTrump;
    deal.first = RuffLeader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[RuffSeat][Spades] = be::holding({Two});  // can follow suit

    be::ObservationState state{};
    state.declarer = RuffDeclarer;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, RuffSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Two}};

    EXPECT_FALSE(be::ruff_small()(ctx, best_cards).has_value());
}

// --- make_default_defender_heuristics --------------------------------------

class MakeDefaultDefenderHeuristicsTest : public ::testing::Test
{
};

TEST_F(MakeDefaultDefenderHeuristicsTest, TheNotrumpChainWorksEndToEndThroughADiscard)
{
    // ruff_small always defers when ctx.trump == NoTrump (see its gate),
    // whether or not it is in the chain. So leaving it out of the notrump
    // chain cannot be seen in behaviour. This test checks what can be seen:
    // the notrump chain still reaches its last rule, discard_keep_winners,
    // and fires it on a real discard.
    Deal deal{};
    deal.trump = NoTrump;
    deal.first = DiscardLeader;
    deal.currentTrickSuit[0] = Hearts;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[Discarder][Spades] = be::holding({Two});
    deal.remainCards[East][Spades] = be::holding({Ace});

    be::ObservationState state{};
    state.declarer = DiscardDeclarer;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, Discarder, fut);
    std::vector<be::Card> const best_cards{be::Card{Spades, Two}};

    be::DefenderHeuristicChain const chain = be::make_default_defender_heuristics(NoTrump);
    std::optional<be::Card> const result = chain.select_card(ctx, best_cards);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->suit, Spades);
    EXPECT_EQ(result->rank, Two);
}

TEST_F(MakeDefaultDefenderHeuristicsTest, InsertsRuffSmallBeforeDiscardKeepWinnersForATrumpContract)
{
    // Hearts are trump. North, void in spades (the led suit), holds the heart
    // two and is offered a club ace as an alternative. Clubs give the
    // defenders zero top tricks (East holds the ace); hearts give one (nothing
    // beats North's lone heart). If discard_keep_winners ran first it would
    // pick the club (fewer top tricks). ruff_small runs first and must return
    // the trump.
    Deal deal{};
    deal.trump = RuffTrump;
    deal.first = RuffLeader;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Three;
    deal.remainCards[RuffSeat][Hearts] = be::holding({Two});
    deal.remainCards[RuffNotYetPlayedDeclaringSide][Spades] = be::holding({Four});  // can follow suit
    deal.remainCards[East][Clubs] = be::holding({Ace});

    be::ObservationState state{};
    state.declarer = RuffDeclarer;
    FutureTricks fut{};

    be::DefenderHeuristicContext const ctx =
        be::make_defender_heuristic_context(deal, state, RuffSeat, fut);
    std::vector<be::Card> const best_cards{be::Card{Hearts, Two}, be::Card{Clubs, Ace}};

    be::DefenderHeuristicChain const chain = be::make_default_defender_heuristics(RuffTrump);
    std::optional<be::Card> const result = chain.select_card(ctx, best_cards);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->suit, Hearts);
    EXPECT_EQ(result->rank, Two);
}
