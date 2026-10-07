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

    // Same object, not merely an equal-looking copy.
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

    // Negative: wrong position -- the seat on play is the trick leader
    // itself (position 0), a defender who simply has not been passed the
    // trick yet.
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

    // Negative: wrong position -- third seat, not second. North leads,
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

    // Negative: declaring side -- the seat at this trick position is the
    // declarer itself, not a defender.
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

    // Two and King are not adjacent in rank, so no "gap closed by a card
    // being gone" story could ever put them in the same touching group --
    // equals left at zero for both reflects that directly.
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

TEST_F(SecondSeatLowTest, DefersWhenTheLowestTouchesAnotherCandidateAndRandomisingIsOn)
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
    // Ace's own equals bit names the Queen, as dds reports it for the
    // king-gone-from-play case -- see the Ace/Queen empirical check this
    // plan's own analysis ran against the real solver.
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

TEST_F(SecondSeatLowTest, AlwaysReturnsTheLowestWhenRandomisingIsOff)
{
    // The identical fixture as the test above, differing only in the one
    // constructor argument -- this is what proves the argument does
    // something, not merely that it compiles.
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
    // The shape solve_board actually returns for a plain touching pair --
    // not the two-separate-entries shape the tests above construct by
    // hand: ONE fut entry (dds reports the *highest* card of a touching
    // run as the representative), with the lower card folded into that
    // one entry's own equals field. best_cards, built one entry per fut
    // index exactly as HeuristicDefender builds it, therefore has only
    // one member here too -- confirmed directly against a real
    // solve_board call (not asserted by construction) while building the
    // minimal example in restricted_choice_6nt_belief_space.py's own
    // main(), where this was first caught: a chain carrying this bug
    // returns the queen at probability 1.0 under both
    // randomize_touching_honors settings, never detecting the fold at
    // all.
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
    // Two disjoint touching groups tied for best score: ace-with-king
    // at fut index 0 (the representative dds lists first), queen-with-jack
    // at index 1. The lowest candidate by rank is the queen -- this
    // rule's own pick, which does touch (the jack) -- but
    // spread(fut, SpreadPolicy::TouchingSequence) always resolves to
    // canonical_best(fut)'s own entry, index 0 here (first entry reached
    // among those tied for the top score), which is the ace/king pair,
    // not this one. Deferring would silently randomise the wrong pair;
    // this rule must return its own answer (the jack) directly instead,
    // exactly as randomize_touching_honors=false would.
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
    fut.score[1] = 5;  // tied with index 0 -- canonical_best keeps index 0

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
// South leads, West (dummy) plays second, North (a defender) is third to
// act, East (declarer) plays last. South and North are partners (both
// defenders); East and West are partners (declarer and dummy) -- chosen
// so dummy's card is already visible by North's turn, which is what
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

    // Same context: third_seat_low must defer, proving the partition.
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
    // Same collapsing shape as fourth_seat_low's own regression test: one
    // fut entry (the king, dds's representative), with the queen folded
    // into its equals. Dummy's ace beats the king, so high_in_third
    // defers and third_seat_low must fire -- the true lowest card in the
    // group is the queen, not the king it is written down as.
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
    // The fixture above always has EAST declare, which puts WEST (dummy)
    // at the trick's second position -- the only case a seat-derived
    // "dummy's position" calculation happens to land safely inside
    // currentTrickRank's three-element bound. Declaring WEST instead
    // puts EAST (dummy) at the *fourth* position and WEST (declarer) at
    // the second: the same partner-led, third-seat gate holds (the
    // rotation does not care who declares), but a prior version of this
    // rule computed dummy's seat and derived position 3 from it here --
    // one past the bound. This fixture exists to prove the fix compares
    // against trick index 1 directly, regardless of who's sitting there.
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
    // West, void in the led suit (spades), ruffs with a trump instead of
    // following -- nothing that merely follows suit can beat a trump, so
    // high_in_third must defer (rising cannot help) and third_seat_low
    // must fire, no matter how high North's own candidates rank.
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
    // West, void in the led suit, discards a plain card of a third suit
    // (not trump -- there is none here) instead of following: that card
    // does not contest the trick at all, so high_in_third must fire
    // regardless of how low North's own candidate ranks.
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
    // Identical to the first test's own fixture, except the third seat
    // (North) is itself declared the declarer -- no longer a defender.
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

    // Positive: fourth seat, following suit, defending. Three cards
    // already on the trick (ranks irrelevant beyond being non-zero,
    // i.e. actually played) so seat_on_play(deal) genuinely reaches the
    // fourth seat.
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
    // The same collapsing shape second_seat_low's own regression test
    // reproduces: solve_board folds a touching run (queen and jack, king
    // gone) to one canonical entry (the queen, dds's own representative),
    // with the jack folded into that entry's equals. best_cards therefore
    // has only this one member, and the true lowest card in the group --
    // the jack -- is nowhere written down as its own rank.
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
    // Spades: East (an opponent) holds the ace, so North's own spade is
    // not a winner -- zero top tricks there. Diamonds: North holds the
    // ace and South (partner) holds the king, nothing else outstanding
    // in the suit -- one top trick (the ace and king are consumed in
    // the same round), still worth more than zero. The rule must
    // discard the spade, not the diamond ace.
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
    // Clubs and diamonds both give zero top tricks (East holds the ace of
    // each). best_cards offers a higher club and a lower diamond first --
    // rank breaks the tie (the diamond, lower, wins) -- and then a second
    // comparison where both suits offer the same rank, where suit itself
    // (diamonds < clubs) breaks the tie.
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
    // Unlike second/third/fourth-seat low, this rule compares candidates
    // from *different* suits -- so, unlike those rules, comparing by raw
    // (representative) rank is not merely cosmetically wrong when a
    // candidate's group is collapsed, it can pick the wrong suit
    // outright. Both suits give zero top tricks: East holds the ace of
    // diamonds (beating North's only diamond outright), and *both* the
    // ace and king of spades (so neither of North's two spades gets
    // through -- one ace could only stop one of the two, which is not
    // zero; this is why suit_top_tricks's own regression tests require
    // two genuine stoppers here, not one). So the tie is broken by rank
    // alone. Spades collapses to one fut entry, the six, with the two
    // folded into its equals -- the true lowest card there is the two.
    // Diamonds is a plain five, no fold. Comparing *representative*
    // ranks (six vs five) picks diamonds; the true lowest ranks (two vs
    // five) must pick spades instead.
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
    // Trump is hearts. West leads a spade; North (seat 0), one card into
    // the trick, is void in spades and holds two low hearts. East (the
    // declarer) and South have not yet played to this trick.
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
    // Neither not-yet-played seat can overruff: East can follow suit
    // (holds a spade), South holds no hearts at all.
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

TEST_F(RuffSmallTest, DetectsAnOverruffAgainstTheTrueLowestTrumpNotTheCollapsedRepresentative)
{
    // North's trumps collapse to one fut entry: the jack (dds's own
    // representative), with the nine folded into its equals -- nothing
    // holds the ten between them. The true lowest trump North would
    // actually play is the nine, not the jack. East's own ten sits
    // strictly between the two: it overruffs the nine but not the jack.
    // A rule that computed the overruff mask from the uncollapsed
    // representative rank (the jack) would miss this threat entirely and
    // wrongly return a card instead of deferring.
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
    // South, the *defending* side, is void in spades and holds a much
    // higher trump -- irrelevant to this rule's own declaring-side gate.
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
    // ruff_small defers unconditionally whenever ctx.trump == NoTrump
    // (see its own gate), whether or not it is present in the chain --
    // its exclusion from the notrump chain is therefore not separately
    // observable by behaviour alone. What *is* observable, and is what
    // this test actually checks: the notrump chain still reaches and
    // fires discard_keep_winners, its last rule, on a genuine discard.
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
    // Hearts is trump. North, void in spades (the led suit), holds the
    // two of hearts (a trump) and is offered a club ace as an
    // alternative candidate. Clubs gives zero top tricks for the
    // defending side (East holds the ace); hearts gives one (nothing
    // else is in the deal to beat North's own lone heart). If
    // discard_keep_winners ran first, it would pick the club (fewer top
    // tricks) -- ruff_small running first instead must return the trump.
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
