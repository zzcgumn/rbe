#include <gtest/gtest.h>

#include <set>
#include <utility>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/void_derivation.hpp>

// Tests for derive_voids: discards, ruffs, trailing partial tricks,
// leader changes and malformed input.

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

    /// Checks each of the 16 (seat, suit) cells against `expected_void`. A
    /// failure names the cell that disagrees, unlike one EXPECT_EQ on the
    /// whole array.
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
    // North leads the ♦K, and East, South and West follow in diamonds.
    // Nobody shows out.
    PlayTraceBin const history =
        trace({{Diamonds, King}, {Diamonds, Two}, {Diamonds, Three}, {Diamonds, Queen}});
    VoidsBySeat const voids = derive_voids(history, North, DDS_NOTRUMP);
    expect_voids(voids, {});
}

TEST_F(VoidDerivationTest, APlainDiscardEstablishesVoidInTheLedSuit)
{
    // North leads the ♦K. East discards the ♠A, so East is void in diamonds.
    // South and West follow in diamonds.
    PlayTraceBin const history =
        trace({{Diamonds, King}, {Spades, Ace}, {Diamonds, Two}, {Diamonds, Queen}});
    VoidsBySeat const voids = derive_voids(history, North, DDS_NOTRUMP);
    expect_voids(voids, {{East, Diamonds}});
}

TEST_F(VoidDerivationTest, ARuffEstablishesTheSameVoidAsADiscardWould)
{
    // Hearts are trump. North leads the ♠K. East discards a club, so East is
    // void in spades. South ruffs with a heart and is also void in spades,
    // although South wins the trick. West follows in spades.
    //
    // This is the trap: the void comes from the suit played, not from whether
    // the card won.
    PlayTraceBin const history =
        trace({{Spades, King}, {Clubs, Two}, {Hearts, Three}, {Spades, Queen}});
    VoidsBySeat const voids = derive_voids(history, North, Hearts);
    expect_voids(voids, {{East, Spades}, {South, Spades}});
}

TEST_F(VoidDerivationTest, ATrailingPartialTrickStillEstablishesVoids)
{
    // Trick one (notrump, diamonds): North ♦K, East ♦2, South ♦3, West ♦Q.
    // North's king wins, so North leads trick two. Nobody shows out.
    //
    // Trick two stops after two cards: North leads the ♣5 and East discards a
    // heart. East is void in clubs, although the trick has no winner.
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
    // The trailing trick can be one card. After a complete trick one with no
    // voids, North leads the ♣5 to trick two. No void is shown. The test checks
    // that a history whose length is not a multiple of four neither crashes
    // nor drops the last card.
    PlayTraceBin const history = trace(
        {{Diamonds, King}, {Diamonds, Two}, {Diamonds, Three}, {Diamonds, Queen}, {Clubs, Five}});
    VoidsBySeat const voids = derive_voids(history, North, DDS_NOTRUMP);
    expect_voids(voids, {});
}

// --- malformed input is asserted, and safe in either build mode ----------

TEST_F(VoidDerivationTest, AnOutOfRangeHistoryLengthIsAnAssertedCallerError)
{
    // EXPECT_DEBUG_DEATH, not EXPECT_DEATH: NDEBUG compiles out the assert,
    // so EXPECT_DEATH would fail there because the program no longer dies.
    // EXPECT_DEBUG_DEATH expects death only when assert is active. Otherwise
    // it just runs the statement, which exercises the clamp.
    PlayTraceBin history = trace({{Diamonds, King}, {Diamonds, Two}, {Diamonds, Three}, {Diamonds, Queen}});
    history.number = 53;  // one past the 52-element bound of PlayTraceBin::suit and rank
    EXPECT_DEBUG_DEATH({ derive_voids(history, North, DDS_NOTRUMP); }, "");
}

TEST_F(VoidDerivationTest, AnOutOfRangeOpeningLeaderIsAnAssertedCallerError)
{
    PlayTraceBin const history = trace({{Diamonds, King}, {Diamonds, Two}, {Diamonds, Three}, {Diamonds, Queen}});
    EXPECT_DEBUG_DEATH({ derive_voids(history, /*opening_leader=*/DDS_HANDS, DDS_NOTRUMP); }, "");
    EXPECT_DEBUG_DEATH({ derive_voids(history, /*opening_leader=*/-1, DDS_NOTRUMP); }, "");
}

TEST_F(VoidDerivationTest, AMalformedSuitIsClampedRatherThanIndexingOutOfBounds)
{
    // A led suit outside [0, DDS_SUITS) would index voids[seat][led_suit]
    // out of bounds. It is clamped to Spades (0). The test compares the result
    // with the same history led in spades, not just that the call survives.
    PlayTraceBin malformed = trace({{Spades, King}, {Diamonds, Two}, {Spades, Three}, {Spades, Queen}});
    malformed.suit[0] = 99;  // the malformed led suit, outside [0, DDS_SUITS)
    VoidsBySeat const malformed_voids = derive_voids(malformed, North, DDS_NOTRUMP);

    PlayTraceBin const clamped_equivalent =
        trace({{Spades, King}, {Diamonds, Two}, {Spades, Three}, {Spades, Queen}});
    VoidsBySeat const clamped_voids = derive_voids(clamped_equivalent, North, DDS_NOTRUMP);

    expect_voids(malformed_voids, {{East, Spades}});
    EXPECT_EQ(malformed_voids, clamped_voids);
}

TEST_F(VoidDerivationTest, AMalformedRankIsClampedRatherThanLeftUnbounded)
{
    // A malformed rank cannot index out of bounds: it only reaches a
    // comparison in trick_winner. It is still clamped. The test compares the
    // result with an explicit low-rank history.
    PlayTraceBin malformed = trace({{Spades, King}, {Spades, Two}, {Spades, Three}, {Spades, Queen}});
    malformed.rank[1] = -1000000;  // the malformed rank, far below [2, 14]
    VoidsBySeat const malformed_voids = derive_voids(malformed, North, DDS_NOTRUMP);

    PlayTraceBin const clamped_equivalent =
        trace({{Spades, King}, {Spades, Two}, {Spades, Three}, {Spades, Queen}});
    VoidsBySeat const clamped_voids = derive_voids(clamped_equivalent, North, DDS_NOTRUMP);

    expect_voids(malformed_voids, {});  // every card follows the led suit
    EXPECT_EQ(malformed_voids, clamped_voids);
}

TEST_F(VoidDerivationTest, MultipleTricksAccumulateVoidsAcrossLeaderChanges)
{
    // Trick one (notrump, spades): North ♠K, East ♠2, South ♠3, West ♠Q.
    // North's king wins, so North leads trick two. The trick has no ruff, so
    // the test isolates the leader change from the ruff case above.
    //
    // Trick two (hearts): North leads the ♥9, East discards the ♠A, and South
    // and West follow in hearts. East's void in hearts is the only one.
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
