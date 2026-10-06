#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/defender_heuristic.hpp>
#include <belief_evaluation/high_in_third.hpp>
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
    constexpr int Jack = 11;
    constexpr int Queen = 12;
    constexpr int King = 13;
    constexpr int Ace = 14;

    constexpr int Spades = 0;
    constexpr int Hearts = 1;
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

    EXPECT_FALSE(be::second_seat_low(/*randomise_touching_honours=*/true)(ctx, best_cards).has_value());
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
        be::second_seat_low(/*randomise_touching_honours=*/false)(ctx, best_cards);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->rank, Queen);
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
