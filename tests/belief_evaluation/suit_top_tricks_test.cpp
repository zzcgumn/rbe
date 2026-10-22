#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/suit_top_tricks.hpp>
#include <belief_evaluation/types.hpp>

#include "test_support.hpp"

// Tests for suit_top_tricks: partnership and opponent play in a suit,
// opponents going void, ruffing, and one documented undercount.

namespace be = dds::belief_evaluation;

namespace
{
    constexpr int Two = 2;
    constexpr int Three = 3;
    constexpr int Four = 4;
    constexpr int Five = 5;
    constexpr int Six = 6;
    constexpr int Seven = 7;
    constexpr int Eight = 8;
    constexpr int Nine = 9;
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

class SuitTopTricksTest : public ::testing::Test
{
};

TEST_F(SuitTopTricksTest, OneCardEachSplitBetweenPartnersGivesOneTopTrick)
{
    // North (defender) holds the ace and South (partner) the king, the top
    // two cards. Every hand holds one card of the suit, so it is played only
    // once. The ace and king fall on the same trick, which the ace wins. One
    // top trick, not two: summing the two defending hands would overcount.
    Deal deal{};
    deal.remainCards[North][Spades] = be::holding({Ace});
    deal.remainCards[South][Spades] = be::holding({King});
    deal.remainCards[East][Spades] = be::holding({Two});
    deal.remainCards[West][Spades] = be::holding({Three});

    std::array<int, 4> const tricks = be::suit_top_tricks(deal, North, NoTrump);
    EXPECT_EQ(tricks[Spades], 1);
}

TEST_F(SuitTopTricksTest, LongerHandKeepsRunningAfterTheShorterOneIsExhausted)
{
    // North (defender) holds the ace and a low card; South (partner) holds
    // only the king. Round one uses the ace and the king, and the ace wins.
    // South is then void, and North's low card wins round two, since nothing
    // is left to beat it. Two top tricks: not 4 cards' worth, and not 1 from
    // max(2, 1).
    Deal deal{};
    deal.remainCards[North][Spades] = be::holding({Ace, Four});
    deal.remainCards[South][Spades] = be::holding({King});
    deal.remainCards[East][Spades] = be::holding({Two});
    deal.remainCards[West][Spades] = be::holding({Three});

    std::array<int, 4> const tricks = be::suit_top_tricks(deal, North, NoTrump);
    EXPECT_EQ(tricks[Spades], 2);
}

TEST_F(SuitTopTricksTest, AnOpponentDucksRatherThanAlwaysUsingItsHighestRemainingCard)
{
    // North (defender) holds the eight and six; East (an opponent) holds the
    // seven and five. Pairing the cards in descending order, the bug this
    // test catches, gives eight over seven and six over five: two tricks. A
    // real opponent ducks the eight with the five and keeps the seven to beat
    // the six. One top trick, not two.
    Deal deal{};
    deal.remainCards[North][Spades] = be::holding({Eight, Six});
    deal.remainCards[East][Spades] = be::holding({Seven, Five});

    std::array<int, 4> const tricks = be::suit_top_tricks(deal, North, NoTrump);
    EXPECT_EQ(tricks[Spades], 1);
}

TEST_F(SuitTopTricksTest, StopsCountingAfterTheFirstStopperRatherThanConsumingItAndContinuing)
{
    // North (defender) holds the two to the six; East (an opponent) holds
    // only the seven, higher than all of them. A count that spends the seven
    // on North's two and then credits the other four cards would claim four
    // tricks. In play, North leads the six and the seven wins at once. East
    // need not lead spades back. Zero top tricks, not four.
    Deal deal{};
    deal.remainCards[North][Spades] = be::holding({Two, Three, Four, Five, Six});
    deal.remainCards[East][Spades] = be::holding({Seven});

    std::array<int, 4> const tricks = be::suit_top_tricks(deal, North, NoTrump);
    EXPECT_EQ(tricks[Spades], 0);
}

TEST_F(SuitTopTricksTest, BothOpponentsGoingVoidTogetherLetsTheRemainingLowCardsWinToo)
{
    // North (defender) holds the ace and two; South (partner) holds none.
    // East holds only the king and West only the queen. A count that compares
    // each of North's cards with the king would stop at the two (2 <= 13).
    // It misses that both opponents are void after round one. In play,
    // North leads the ace, and East and West must follow with the king and
    // queen. The ace wins, both opponents are void, and the two wins round
    // two. Two top tricks, not one.
    Deal deal{};
    deal.remainCards[North][Spades] = be::holding({Ace, Two});
    deal.remainCards[East][Spades] = be::holding({King});
    deal.remainCards[West][Spades] = be::holding({Queen});

    std::array<int, 4> const tricks = be::suit_top_tricks(deal, North, NoTrump);
    EXPECT_EQ(tricks[Spades], 2);
}

TEST_F(SuitTopTricksTest, UndercountsWhenTheShorterOpponentHoldsTheOnlyDangerousCard)
{
    // The gap that suit_top_tricks.hpp documents. One fixed opponent_max is
    // compared with each of our cards while count < opponent_rounds, the
    // length of the longer opposing hand. But opponent_max can belong to the
    // shorter hand, which is void earlier.
    //
    // North (defender) holds the king, eight and five; South (partner) holds
    // none. East holds only the nine: the shorter hand, with the only card
    // that could beat North's king. West holds the two and three, longer but
    // harmless.
    //
    // In play: North leads the king, East must play the nine and West ducks
    // the two. The king wins and East is void. North leads the eight, West's
    // three cannot beat it, and West is void too. The five then wins with no
    // opposition. Three top tricks, the true answer.
    //
    // The function returns 1. opponent_max is 9 (East's card), and
    // opponent_rounds = max(1, 2) = 2, West's length, with no notion of who
    // holds opponent_max. So the eight is compared with the 9 before the
    // auto-win branch triggers, although East is already void.
    //
    // An undercount is safe and allowed by the documented contract: this is
    // not an exact double-dummy suit solver. The test pins the limitation so a
    // change to the opponent-void model has a concrete case to check.
    Deal deal{};
    deal.remainCards[North][Spades] = be::holding({King, Eight, Five});
    deal.remainCards[East][Spades] = be::holding({Nine});
    deal.remainCards[West][Spades] = be::holding({Two, Three});

    std::array<int, 4> const tricks = be::suit_top_tricks(deal, North, NoTrump);
    EXPECT_EQ(tricks[Spades], 1)
        << "today's known undercount (true answer: 3) -- see this test's own comment";
}

TEST_F(SuitTopTricksTest, DeclaringSideHoldingTheTopGivesZero)
{
    // East (an opponent of North and South) holds the ace, which North's best
    // card, the king, cannot beat.
    Deal deal{};
    deal.remainCards[North][Spades] = be::holding({King});
    deal.remainCards[East][Spades] = be::holding({Ace});

    std::array<int, 4> const tricks = be::suit_top_tricks(deal, North, NoTrump);
    EXPECT_EQ(tricks[Spades], 0);
}

TEST_F(SuitTopTricksTest, CapsAtTheRuffingOpponentsOwnSuitCountRatherThanOverclaiming)
{
    // Hearts are trump. North holds ♠AKQJ, which by rank looks like four top
    // tricks. East holds one spade (the nine) and a heart. East must follow
    // with the nine on round one, but is void from round two and can ruff. A
    // count of the cards in hand would claim four; the true ceiling, and the
    // result, is one.
    Deal deal{};
    deal.remainCards[North][Spades] = be::holding({Jack, Ace, King, Queen});
    deal.remainCards[East][Spades] = be::holding({Nine});
    deal.remainCards[East][Hearts] = be::holding({Two});
    deal.remainCards[West][Spades] = be::holding({Two, Three});

    std::array<int, 4> const tricks = be::suit_top_tricks(deal, North, Hearts);
    EXPECT_LE(tricks[Spades], 1);
    EXPECT_EQ(tricks[Spades], 1);  // the exact value of this simple count
}

TEST_F(SuitTopTricksTest, DefendingSideHoldingNothingGivesZero)
{
    Deal deal{};
    deal.remainCards[East][Spades] = be::holding({Ace});
    deal.remainCards[West][Spades] = be::holding({King});

    std::array<int, 4> const tricks = be::suit_top_tricks(deal, North, NoTrump);
    EXPECT_EQ(tricks[Spades], 0);
}
