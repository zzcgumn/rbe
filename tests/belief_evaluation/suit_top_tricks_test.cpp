#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/suit_top_tricks.hpp>
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
    // North (defender) holds the ace, South (partner) holds the king --
    // the top two cards, split between the partnership, with nothing
    // between them held by the opponents. All four hands hold exactly
    // one card of this suit, so it can only ever be played once: the
    // ace and king are both consumed in that same single trick, which
    // the ace wins. One top trick, not two -- summing the two defending
    // holdings would overcount this.
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
    // North (defender) holds the ace plus a low card; South (partner)
    // holds only the king. Round one consumes the ace and the king
    // together (both still hold the suit) -- a trick the ace wins.
    // South is now void; North alone runs the suit for a second round
    // with its remaining low card, which nothing is left to beat. Two
    // top tricks from four total cards (not four, and not
    // max(2,1) undercounting to 1 either): the sum-based cap this
    // replaces would have claimed three.
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
    // North (defender) holds the eight and the six; East (an opponent)
    // holds the seven and the five. Comparing in strict descending
    // order from both sides -- the bug this test exists to catch --
    // would pair North's eight against East's seven (eight wins) and
    // then North's six against East's five (six wins too), claiming two
    // top tricks. A real opponent does not play that way: East ducks
    // under the eight with the five (the seven cannot beat it either,
    // so using it here would only waste it), keeping the seven to beat
    // North's six on the next round instead. One top trick, not two.
    Deal deal{};
    deal.remainCards[North][Spades] = be::holding({Eight, Six});
    deal.remainCards[East][Spades] = be::holding({Seven, Five});

    std::array<int, 4> const tricks = be::suit_top_tricks(deal, North, NoTrump);
    EXPECT_EQ(tricks[Spades], 1);
}

TEST_F(SuitTopTricksTest, StopsCountingAfterTheFirstStopperRatherThanConsumingItAndContinuing)
{
    // North (defender) holds two through six, five cards headed low;
    // East (an opponent) holds only the seven -- one card, but higher
    // than every one of North's. A version that "matches and removes" a
    // stopper, then keeps counting North's remaining cards as automatic
    // winners, would consume the seven against North's two and credit
    // the three, four, five, and six as four more top tricks. That is
    // not how leading the suit actually goes: North would lead the six
    // first (highest first, to maximise the run), the seven beats it
    // immediately, and North never even gets to try the lower four --
    // once East wins that trick, East is not obliged to lead spades
    // back. Zero top tricks, not four.
    Deal deal{};
    deal.remainCards[North][Spades] = be::holding({Two, Three, Four, Five, Six});
    deal.remainCards[East][Spades] = be::holding({Seven});

    std::array<int, 4> const tricks = be::suit_top_tricks(deal, North, NoTrump);
    EXPECT_EQ(tricks[Spades], 0);
}

TEST_F(SuitTopTricksTest, BothOpponentsGoingVoidTogetherLetsTheRemainingLowCardsWinToo)
{
    // North (defender) holds the ace and the two; South (partner) holds
    // nothing. East holds only the king, West only the queen -- one
    // card each, neither beating North's ace. Comparing every one of
    // North's cards against a single static "highest opposing card"
    // (the king) without ever recognising that both opponents go
    // completely void after round one would wrongly stop at the two
    // (2 <= 13). Physically: trick one, North leads the ace; East and
    // West, each holding exactly one spade, are both forced to follow
    // suit in that same trick regardless of what North leads, so the
    // king and the queen are both spent there, and the ace -- the
    // highest card played -- wins. Both opponents are now void, so
    // trick two, North's two, wins automatically with nothing left to
    // contest it. Two top tricks, not one.
    Deal deal{};
    deal.remainCards[North][Spades] = be::holding({Ace, Two});
    deal.remainCards[East][Spades] = be::holding({King});
    deal.remainCards[West][Spades] = be::holding({Queen});

    std::array<int, 4> const tricks = be::suit_top_tricks(deal, North, NoTrump);
    EXPECT_EQ(tricks[Spades], 2);
}

TEST_F(SuitTopTricksTest, UndercountsWhenTheShorterOpponentHoldsTheOnlyDangerousCard)
{
    // The one gap suit_top_tricks.hpp's own doxygen names explicitly: a
    // single static opponent_max is compared against every one of our
    // cards for as long as count < opponent_rounds (the LONGER opposing
    // hand's own count) -- but opponent_max's owner can itself be the
    // SHORTER hand, already void well before opponent_rounds is reached.
    //
    // North (defender) holds King, Eight, Five; South (partner) holds
    // nothing. East holds only the Nine -- the shorter hand, and the one
    // holding the only card that could trouble North's King. West holds
    // Two, Three -- longer, but harmless (nothing above North's Five).
    //
    // Physically: trick one, North leads the King; East, with its one
    // card, must play the Nine; West ducks with the Two, keeping the
    // Three in reserve. North's King wins; East is now void. Trick two,
    // North leads the Eight; West's only remaining card, the Three,
    // cannot beat it; North's Eight wins; West is now void too. Trick
    // three, North's Five wins automatically with nothing left to
    // contest it at all. Three top tricks -- the true optimal-defense
    // answer.
    //
    // This function returns 1: opponent_max is 9 (East's card, the only
    // one that could ever beat anything of North's here), and
    // opponent_rounds = max(East's count = 1, West's count = 2) = 2 --
    // West's own length, not East's, since opponent_rounds tracks
    // whichever opposing hand is LONGER, with no notion of which one
    // actually holds opponent_max. So North's second card (the Eight) is
    // still compared against the stale opponent_max (9) before
    // opponent_rounds's auto-win branch ever triggers, even though East
    // -- opponent_max's own owner -- went void a full round earlier.
    //
    // Safe (an undercount, never an overestimate) and accepted per this
    // function's own documented contract and the plan's Non-goals (no
    // exact double-dummy suit-trick calculator is in scope) -- pinned
    // here as a known limitation, not a bug to fix, so a future change
    // to the opponent-exhaustion model has a concrete regression to
    // check itself against either way.
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
    // East (an opponent of North/South) holds the ace; North's own best
    // card (the king) cannot beat it.
    Deal deal{};
    deal.remainCards[North][Spades] = be::holding({King});
    deal.remainCards[East][Spades] = be::holding({Ace});

    std::array<int, 4> const tricks = be::suit_top_tricks(deal, North, NoTrump);
    EXPECT_EQ(tricks[Spades], 0);
}

TEST_F(SuitTopTricksTest, CapsAtTheRuffingOpponentsOwnSuitCountRatherThanOverclaiming)
{
    // Trump is hearts. North holds A-K-Q-J of spades outright -- by rank
    // alone this looks like four top tricks. But East holds only one
    // spade (the nine) and a heart: East must follow suit with the nine
    // on the first round (forced, not a ruff -- East is not yet void),
    // but is void from the second round on and could ruff instead of
    // following. A naive "count the cards in hand" estimate would claim
    // four; the true ceiling, and this function's own result, is one.
    Deal deal{};
    deal.remainCards[North][Spades] = be::holding({Jack, Ace, King, Queen});
    deal.remainCards[East][Spades] = be::holding({Nine});
    deal.remainCards[East][Hearts] = be::holding({Two});
    deal.remainCards[West][Spades] = be::holding({Two, Three});

    std::array<int, 4> const tricks = be::suit_top_tricks(deal, North, Hearts);
    EXPECT_LE(tricks[Spades], 1);
    EXPECT_EQ(tricks[Spades], 1);  // the exact value this simplified algorithm reaches here
}

TEST_F(SuitTopTricksTest, DefendingSideHoldingNothingGivesZero)
{
    Deal deal{};
    deal.remainCards[East][Spades] = be::holding({Ace});
    deal.remainCards[West][Spades] = be::holding({King});

    std::array<int, 4> const tricks = be::suit_top_tricks(deal, North, NoTrump);
    EXPECT_EQ(tricks[Spades], 0);
}
