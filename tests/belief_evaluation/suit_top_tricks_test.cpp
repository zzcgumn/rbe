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

TEST_F(SuitTopTricksTest, TopTwoSplitBetweenPartnersGivesTwoTopTricks)
{
    // North (defender) holds the ace, South (partner) holds the king --
    // the top two cards, split between the partnership, with nothing
    // between them held by the opponents. Two top tricks.
    Deal deal{};
    deal.remainCards[North][Spades] = be::holding({Ace});
    deal.remainCards[South][Spades] = be::holding({King});
    deal.remainCards[East][Spades] = be::holding({Two});
    deal.remainCards[West][Spades] = be::holding({Three});

    std::array<int, 4> const tricks = be::suit_top_tricks(deal, North, NoTrump);
    EXPECT_EQ(tricks[Spades], 2);
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
