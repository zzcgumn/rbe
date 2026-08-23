#include <gtest/gtest.h>

#include <api/dll.h>
#include <utility/constants.h>

#include <belief_evaluation/trick.hpp>
#include <belief_evaluation/types.hpp>

namespace
{
    // Rank bits, spelled out for readability at the call sites below.
    constexpr int Two = 2;
    constexpr int Three = 3;
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

class TrickTest : public ::testing::Test
{
};

// --- seat_on_play ---------------------------------------------------------

TEST_F(TrickTest, SeatOnPlayAdvancesFromLeaderByCardsAlreadyPlayed)
{
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = East;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = Ace;  // East played the ace of spades
    EXPECT_EQ(seat_on_play(deal), South);
}

TEST_F(TrickTest, SeatOnPlayIsTheLeaderWhenNothingHasBeenPlayed)
{
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = West;
    EXPECT_EQ(seat_on_play(deal), West);
}

// --- legal_cards ------------------------------------------------------------

TEST_F(TrickTest, LegalCardsWhenLeadingIsEveryHeldCardInEverySuit)
{
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = North;
    // North to lead: Ace of spades, void in hearts, king of diamonds, deuce
    // of clubs.
    deal.remainCards[North][Spades] = 1u << Ace;
    deal.remainCards[North][Hearts] = 0;
    deal.remainCards[North][Diamonds] = 1u << King;
    deal.remainCards[North][Clubs] = 1u << Two;

    auto const legal = legal_cards(deal, North);
    EXPECT_EQ(legal[Spades], 1u << Ace);
    EXPECT_EQ(legal[Hearts], 0u);
    EXPECT_EQ(legal[Diamonds], 1u << King);
    EXPECT_EQ(legal[Clubs], 1u << Two);
}

TEST_F(TrickTest, LegalCardsMustFollowTheLedSuitWhenHoldingIt)
{
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = North;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = King;  // North led the king of spades

    // East holds spades and could also play a heart, but must follow suit.
    deal.remainCards[East][Spades] = (1u << Queen) | (1u << Three);
    deal.remainCards[East][Hearts] = 1u << Ace;

    auto const legal = legal_cards(deal, East);
    EXPECT_EQ(legal[Spades], (1u << Queen) | (1u << Three));
    EXPECT_EQ(legal[Hearts], 0u);
}

TEST_F(TrickTest, LegalCardsAreEveryHeldCardWhenVoidInTheLedSuit)
{
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = North;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = King;  // North led the king of spades

    // East is void in spades, so every held card is a legal discard.
    deal.remainCards[East][Spades] = 0;
    deal.remainCards[East][Hearts] = 1u << Ace;
    deal.remainCards[East][Clubs] = 1u << Two;

    auto const legal = legal_cards(deal, East);
    EXPECT_EQ(legal[Spades], 0u);
    EXPECT_EQ(legal[Hearts], 1u << Ace);
    EXPECT_EQ(legal[Clubs], 1u << Two);
}

// --- trick_complete_winner --------------------------------------------------
//
// Concrete bridge facts, asserted directly rather than by re-deriving
// trick_winner()'s comparison logic in the test.

TEST_F(TrickTest, HighestCardOfTheLedSuitWinsInNotrump)
{
    // North leads the K of diamonds, East follows low, South follows low,
    // West (the fourth card) follows the Q. North's king is highest.
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = North;
    deal.currentTrickSuit[0] = Diamonds;
    deal.currentTrickRank[0] = King;
    deal.currentTrickSuit[1] = Diamonds;
    deal.currentTrickRank[1] = Two;
    deal.currentTrickSuit[2] = Diamonds;
    deal.currentTrickRank[2] = Three;

    EXPECT_EQ(trick_complete_winner(deal, Card{Diamonds, Queen}), North);
}

TEST_F(TrickTest, ADiscardNeverWinsEvenIfItOutranksTheLedSuit)
{
    // North leads the K of diamonds. East, void in diamonds, discards the
    // ace of spades — numerically the highest card played, but off-suit in
    // a notrump contract and therefore never a winner. South and West (the
    // fourth card) follow diamonds low. North's king still wins.
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = North;
    deal.currentTrickSuit[0] = Diamonds;
    deal.currentTrickRank[0] = King;
    deal.currentTrickSuit[1] = Spades;
    deal.currentTrickRank[1] = Ace;
    deal.currentTrickSuit[2] = Diamonds;
    deal.currentTrickRank[2] = Two;

    EXPECT_EQ(trick_complete_winner(deal, Card{Diamonds, Queen}), North);
}

TEST_F(TrickTest, ATrumpRuffWinsOverAHigherCardOfTheLedSuit)
{
    // Hearts are trump. North leads the K of spades. East, void in spades,
    // discards a low club. South, void in spades, ruffs with a heart. West
    // (the fourth card) follows spades with the Q. South's ruff wins,
    // despite North's king and West's queen both outranking it in spades.
    Deal deal{};
    deal.trump = Hearts;
    deal.first = North;
    deal.currentTrickSuit[0] = Spades;
    deal.currentTrickRank[0] = King;
    deal.currentTrickSuit[1] = Clubs;
    deal.currentTrickRank[1] = Two;
    deal.currentTrickSuit[2] = Hearts;
    deal.currentTrickRank[2] = Three;

    EXPECT_EQ(trick_complete_winner(deal, Card{Spades, Queen}), South);
}

// --- play --------------------------------------------------------------

TEST_F(TrickTest, PlayAppendsToAnInProgressTrickWithoutResolvingIt)
{
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = North;
    deal.remainCards[North][Diamonds] = (1u << King) | (1u << Two);

    Deal const after = play(deal, Card{Diamonds, King});

    EXPECT_EQ(after.currentTrickSuit[0], Diamonds);
    EXPECT_EQ(after.currentTrickRank[0], King);
    EXPECT_EQ(after.currentTrickSuit[1], 0);
    EXPECT_EQ(after.currentTrickRank[1], 0);
    EXPECT_EQ(after.first, North);  // the trick has not resolved
    EXPECT_EQ(after.remainCards[North][Diamonds], 1u << Two);
}

TEST_F(TrickTest, PlayResolvesTheTrickOnTheFourthCard)
{
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = North;
    deal.currentTrickSuit[0] = Diamonds;
    deal.currentTrickRank[0] = King;
    deal.currentTrickSuit[1] = Diamonds;
    deal.currentTrickRank[1] = Two;
    deal.currentTrickSuit[2] = Diamonds;
    deal.currentTrickRank[2] = Three;
    deal.remainCards[West][Diamonds] = 1u << Queen;

    Deal const after = play(deal, Card{Diamonds, Queen});

    EXPECT_EQ(after.currentTrickSuit[0], 0);
    EXPECT_EQ(after.currentTrickRank[0], 0);
    EXPECT_EQ(after.currentTrickSuit[1], 0);
    EXPECT_EQ(after.currentTrickRank[1], 0);
    EXPECT_EQ(after.currentTrickSuit[2], 0);
    EXPECT_EQ(after.currentTrickRank[2], 0);
    EXPECT_EQ(after.first, North);  // North's king was highest
    EXPECT_EQ(after.remainCards[West][Diamonds], 0u);
}

TEST_F(TrickTest, PlainSuitTrickInNotrumpEndToEnd)
{
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = North;
    deal.remainCards[North][Diamonds] = 1u << Ace;
    deal.remainCards[East][Diamonds] = 1u << Two;
    deal.remainCards[South][Diamonds] = 1u << Queen;
    deal.remainCards[West][Diamonds] = 1u << Three;

    deal = play(deal, Card{Diamonds, Ace});
    deal = play(deal, Card{Diamonds, Two});
    deal = play(deal, Card{Diamonds, Queen});
    deal = play(deal, Card{Diamonds, Three});

    EXPECT_EQ(deal.currentTrickSuit[0], 0);
    EXPECT_EQ(deal.currentTrickRank[0], 0);
    EXPECT_EQ(deal.first, North);  // North's ace was highest
    EXPECT_EQ(deal.remainCards[North][Diamonds], 0u);
    EXPECT_EQ(deal.remainCards[East][Diamonds], 0u);
    EXPECT_EQ(deal.remainCards[South][Diamonds], 0u);
    EXPECT_EQ(deal.remainCards[West][Diamonds], 0u);
}

TEST_F(TrickTest, TrickRuffedByADefenderEndToEnd)
{
    Deal deal{};
    deal.trump = Hearts;
    deal.first = North;
    deal.remainCards[North][Spades] = 1u << King;
    deal.remainCards[East][Clubs] = 1u << Two;      // void in spades
    deal.remainCards[South][Hearts] = 1u << Three;  // void in spades, ruffs
    deal.remainCards[West][Spades] = 1u << Queen;

    deal = play(deal, Card{Spades, King});
    deal = play(deal, Card{Clubs, Two});
    deal = play(deal, Card{Hearts, Three});
    deal = play(deal, Card{Spades, Queen});

    EXPECT_EQ(deal.currentTrickSuit[0], 0);
    EXPECT_EQ(deal.first, South);  // South's ruff won
    EXPECT_EQ(deal.remainCards[North][Spades], 0u);
    EXPECT_EQ(deal.remainCards[East][Clubs], 0u);
    EXPECT_EQ(deal.remainCards[South][Hearts], 0u);
    EXPECT_EQ(deal.remainCards[West][Spades], 0u);
}

TEST_F(TrickTest, TrickWithADiscardEndToEnd)
{
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = North;
    deal.remainCards[North][Diamonds] = 1u << King;
    deal.remainCards[East][Spades] = 1u << Ace;  // void in diamonds, discards
    deal.remainCards[South][Diamonds] = 1u << Two;
    deal.remainCards[West][Diamonds] = 1u << Queen;

    deal = play(deal, Card{Diamonds, King});
    deal = play(deal, Card{Spades, Ace});
    deal = play(deal, Card{Diamonds, Two});
    deal = play(deal, Card{Diamonds, Queen});

    EXPECT_EQ(deal.currentTrickSuit[0], 0);
    EXPECT_EQ(deal.first, North);  // North's king wins; East's discard cannot
    EXPECT_EQ(deal.remainCards[North][Diamonds], 0u);
    EXPECT_EQ(deal.remainCards[East][Spades], 0u);
    EXPECT_EQ(deal.remainCards[South][Diamonds], 0u);
    EXPECT_EQ(deal.remainCards[West][Diamonds], 0u);
}

TEST_F(TrickTest, TrumpSuitLedEndToEnd)
{
    Deal deal{};
    deal.trump = Spades;
    deal.first = North;
    deal.remainCards[North][Spades] = 1u << King;
    deal.remainCards[East][Spades] = 1u << Two;
    deal.remainCards[South][Spades] = 1u << Ace;
    deal.remainCards[West][Spades] = 1u << Queen;

    deal = play(deal, Card{Spades, King});
    deal = play(deal, Card{Spades, Two});
    deal = play(deal, Card{Spades, Ace});
    deal = play(deal, Card{Spades, Queen});

    EXPECT_EQ(deal.currentTrickSuit[0], 0);
    EXPECT_EQ(deal.first, South);  // South's ace of trumps was highest
    EXPECT_EQ(deal.remainCards[North][Spades], 0u);
    EXPECT_EQ(deal.remainCards[East][Spades], 0u);
    EXPECT_EQ(deal.remainCards[South][Spades], 0u);
    EXPECT_EQ(deal.remainCards[West][Spades], 0u);
}
