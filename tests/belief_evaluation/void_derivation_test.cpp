#include <gtest/gtest.h>

#include <set>
#include <utility>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

#include <belief_evaluation/void_derivation.hpp>

namespace be = dds::belief_evaluation;

using be::derive_voids;
using be::VoidsBySeat;

namespace
{
    constexpr int Two = 2;
    constexpr int Three = 3;
    constexpr int Five = 5;
    constexpr int Nine = 9;
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

    auto trace(std::vector<std::pair<int, int>> const& cards) -> PlayTraceBin
    {
        PlayTraceBin history{};
        history.number = static_cast<int>(cards.size());
        for (std::size_t i = 0; i < cards.size(); ++i)
        {
            history.suit[i] = cards[i].first;
            history.rank[i] = cards[i].second;
        }
        return history;
    }

    /// Every one of the 16 (seat, suit) cells checked individually against
    /// `expected_void` -- an "asserted in full" comparison with a failure
    /// message naming exactly which cell disagreed, rather than one opaque
    /// whole-array EXPECT_EQ.
    auto expect_voids(VoidsBySeat const& actual, std::set<std::pair<int, int>> const& expected_void)
        -> void
    {
        for (int seat = 0; seat < DDS_HANDS; ++seat)
        {
            for (int suit = 0; suit < DDS_SUITS; ++suit)
            {
                bool const want = expected_void.count({seat, suit}) != 0;
                EXPECT_EQ(actual[seat][suit], want)
                    << "seat " << seat << " suit " << suit << ": expected void=" << want
                    << ", got " << actual[seat][suit];
            }
        }
    }
}

class VoidDerivationTest : public ::testing::Test
{
};

TEST_F(VoidDerivationTest, EmptyHistoryYieldsNoVoidsForAnybody)
{
    PlayTraceBin const history = trace({});
    VoidsBySeat const voids = derive_voids(history, North, DDS_NOTRUMP);
    expect_voids(voids, {});
}

TEST_F(VoidDerivationTest, OneCompleteTrickWithNobodyShowingOutYieldsNoVoids)
{
    // North leads the king of diamonds; East, South and West all follow
    // diamonds. Nobody shows out of anything.
    PlayTraceBin const history =
        trace({{Diamonds, King}, {Diamonds, Two}, {Diamonds, Three}, {Diamonds, Queen}});
    VoidsBySeat const voids = derive_voids(history, North, DDS_NOTRUMP);
    expect_voids(voids, {});
}

TEST_F(VoidDerivationTest, APlainDiscardEstablishesVoidInTheLedSuit)
{
    // North leads the king of diamonds. East discards a spade -- void in
    // diamonds. South and West follow diamonds.
    PlayTraceBin const history =
        trace({{Diamonds, King}, {Spades, Ace}, {Diamonds, Two}, {Diamonds, Queen}});
    VoidsBySeat const voids = derive_voids(history, North, DDS_NOTRUMP);
    expect_voids(voids, {{East, Diamonds}});
}

TEST_F(VoidDerivationTest, ARuffEstablishesTheSameVoidAsADiscardWould)
{
    // Hearts are trump. North leads the king of spades. East discards a
    // club -- void in spades. South ruffs with a heart -- also void in
    // spades, despite winning the trick. West follows spades.
    //
    // This is the trap: the void comes from the suit played, never from
    // whether the card happened to win.
    PlayTraceBin const history =
        trace({{Spades, King}, {Clubs, Two}, {Hearts, Three}, {Spades, Queen}});
    VoidsBySeat const voids = derive_voids(history, North, Hearts);
    expect_voids(voids, {{East, Spades}, {South, Spades}});
}

TEST_F(VoidDerivationTest, ATrailingPartialTrickStillEstablishesVoids)
{
    // Trick one (no-trump, diamonds): North K, East 2, South 3, West Q --
    // North's king is highest, so North leads trick two. Nobody shows out.
    //
    // Trick two is left incomplete after two cards: North leads a club,
    // East discards a heart -- void in clubs, even though the trick never
    // completes and has no winner.
    PlayTraceBin const history = trace(
        {{Diamonds, King},
         {Diamonds, Two},
         {Diamonds, Three},
         {Diamonds, Queen},
         {Clubs, Five},
         {Hearts, Nine}});
    VoidsBySeat const voids = derive_voids(history, North, DDS_NOTRUMP);
    expect_voids(voids, {{East, Clubs}});
}

TEST_F(VoidDerivationTest, ASingleCardTrailingTrickStillEstablishesAVoid)
{
    // The trailing trick can be as short as one card. North leads a
    // diamond as the sole card of an otherwise-unplayed trick two, after a
    // complete, void-free trick one -- nothing to check here except that a
    // history whose length is not a multiple of four does not crash or
    // silently drop the last card.
    PlayTraceBin const history = trace(
        {{Diamonds, King}, {Diamonds, Two}, {Diamonds, Three}, {Diamonds, Queen}, {Clubs, Five}});
    VoidsBySeat const voids = derive_voids(history, North, DDS_NOTRUMP);
    expect_voids(voids, {});
}

TEST_F(VoidDerivationTest, MultipleTricksAccumulateVoidsAcrossLeaderChanges)
{
    // Trick one (no-trump, spades): North K, East 2 (follows), South ruffs
    // -- no, no-trump has no ruffing; keep this trick trump-free to isolate
    // leader-change arithmetic from the ruff case, already covered above.
    // North K, East 2, South 3, West Q -- North's king wins, North leads
    // trick two.
    //
    // Trick two (hearts): North leads a heart, East discards a spade (void
    // in hearts), South follows hearts, West follows hearts. East's
    // hearts-void is the only one established.
    PlayTraceBin const history = trace(
        {{Spades, King},
         {Spades, Two},
         {Spades, Three},
         {Spades, Queen},
         {Hearts, Nine},
         {Spades, Ace},
         {Hearts, Two},
         {Hearts, Three}});
    VoidsBySeat const voids = derive_voids(history, North, DDS_NOTRUMP);
    expect_voids(voids, {{East, Hearts}});
}
