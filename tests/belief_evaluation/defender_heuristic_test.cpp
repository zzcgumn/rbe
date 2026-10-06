#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/defender_heuristic.hpp>
#include <belief_evaluation/types.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;

namespace
{
    constexpr int Two = 2;
    constexpr int Three = 3;
    constexpr int Queen = 12;
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
