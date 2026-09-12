#include <gtest/gtest.h>

#include <functional>
#include <set>
#include <utility>
#include <vector>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

#include <belief_evaluation/history_verification.hpp>

namespace be = dds::belief_evaluation;

using be::HistoryVerdict;
using be::verify_history;

namespace
{
    constexpr int Spades = 0;
    constexpr int Diamonds = 2;

    constexpr int North = 0;
    constexpr int East = 1;
    constexpr int South = 2;
    constexpr int West = 3;

    using CardPair = std::pair<int, int>;  // (suit, rank)

    auto all_52_cards() -> std::vector<CardPair>
    {
        std::vector<CardPair> cards;
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            for (int rank = 2; rank <= 14; ++rank)
            {
                cards.emplace_back(suit, rank);
            }
        }
        return cards;
    }

    /// Builds a root + history pair that together account for exactly the
    /// 52-card deck: `played` (in order) becomes the history, and every
    /// other card is handed to `hand_for(suit, rank)` and added to that
    /// seat's remainCards in root. So a fixture states only what it cares
    /// about -- the played prefix and wherever `hand_for` deliberately
    /// deviates from its default -- while accounting for the remaining
    /// ~40-odd cards is mechanical and cannot be gotten wrong by hand.
    /// `root.trump`, `root.first` and `root.currentTrick*` are left at
    /// their defaults; callers set what they need.
    auto build_deal(
        std::vector<CardPair> const& played, std::function<int(int, int)> const& hand_for)
        -> std::pair<Deal, PlayTraceBin>
    {
        std::set<CardPair> const played_set(played.begin(), played.end());

        Deal root{};
        for (auto const& [suit, rank] : all_52_cards())
        {
            if (played_set.count({suit, rank}) != 0)
            {
                continue;
            }
            int const hand = hand_for(suit, rank);
            root.remainCards[hand][suit] |= (1u << rank);
        }

        PlayTraceBin history{};
        history.number = static_cast<int>(played.size());
        for (std::size_t i = 0; i < played.size(); ++i)
        {
            history.suit[i] = played[i].first;
            history.rank[i] = played[i].second;
        }

        return {root, history};
    }

    /// The default distribution every fixture below starts from: every
    /// unplayed card goes to West, so a fixture need only say where the
    /// handful of cards it actually cares about differ from that.
    auto everything_to_west(int, int) -> int
    {
        return West;
    }
}

class HistoryVerificationTest : public ::testing::Test
{
};

// --- accepted histories -----------------------------------------------------

TEST_F(HistoryVerificationTest, ACorrectHistoryAtATrickBoundaryIsAccepted)
{
    // North leads the ace of diamonds, East follows low, South follows
    // low, West follows the queen -- North's ace is highest, so North
    // leads next; no trick is in progress at this root.
    auto [root, history] = build_deal(
        {{Diamonds, 14}, {Diamonds, 2}, {Diamonds, 3}, {Diamonds, 12}}, everything_to_west);
    root.trump = DDS_NOTRUMP;
    root.first = North;

    EXPECT_EQ(verify_history(root, North, history, North), HistoryVerdict::Consistent);
}

TEST_F(HistoryVerificationTest, ACorrectHistoryMidTrickIsAccepted)
{
    // Trick one as above (North wins). Trick two: North leads the king of
    // spades, East follows with the deuce -- two cards into the trick in
    // progress at this root.
    auto [root, history] = build_deal(
        {{Diamonds, 14},
         {Diamonds, 2},
         {Diamonds, 3},
         {Diamonds, 12},
         {Spades, 13},
         {Spades, 2}},
        everything_to_west);
    root.trump = DDS_NOTRUMP;
    root.first = North;
    root.currentTrickSuit[0] = Spades;
    root.currentTrickRank[0] = 13;
    root.currentTrickSuit[1] = Spades;
    root.currentTrickRank[1] = 2;

    EXPECT_EQ(verify_history(root, North, history, North), HistoryVerdict::Consistent);
}

TEST_F(HistoryVerificationTest, ARootWithNothingPlayedYetIsAccepted)
{
    // A third shape: the whole deck still held, nothing played, nothing in
    // progress.
    auto [root, history] = build_deal({}, everything_to_west);
    root.trump = DDS_NOTRUMP;
    root.first = North;

    EXPECT_EQ(verify_history(root, North, history, North), HistoryVerdict::Consistent);
}

// --- the card partition ------------------------------------------------------

TEST_F(HistoryVerificationTest, ADuplicatedCardInTheHistoryIsRejected)
{
    auto [root, history] =
        build_deal({{Diamonds, 14}, {Diamonds, 2}, {Diamonds, 3}, {Diamonds, 14}}, everything_to_west);
    root.trump = DDS_NOTRUMP;
    root.first = North;

    EXPECT_EQ(verify_history(root, North, history, North), HistoryVerdict::DuplicatedCard);
}

TEST_F(HistoryVerificationTest, ACardBothPlayedAndStillHeldIsRejected)
{
    auto [root, history] = build_deal(
        {{Diamonds, 14}, {Diamonds, 2}, {Diamonds, 3}, {Diamonds, 12}}, everything_to_west);
    root.trump = DDS_NOTRUMP;
    root.first = North;
    // The ace of diamonds was just played, per history -- and root also
    // still shows North holding it.
    root.remainCards[North][Diamonds] |= (1u << 14);

    EXPECT_EQ(verify_history(root, North, history, North), HistoryVerdict::CardPlayedAndHeld);
}

TEST_F(HistoryVerificationTest, ACardMissingFromBothSidesIsRejected)
{
    auto [root, history] = build_deal(
        {{Diamonds, 14}, {Diamonds, 2}, {Diamonds, 3}, {Diamonds, 12}}, everything_to_west);
    root.trump = DDS_NOTRUMP;
    root.first = North;
    // The ace of spades is neither played nor held by anybody.
    root.remainCards[West][Spades] &= ~(1u << 14);

    EXPECT_EQ(verify_history(root, North, history, North), HistoryVerdict::MissingCard);
}

// --- the trailing trick -------------------------------------------------------

TEST_F(HistoryVerificationTest, ATrailingLengthThatDoesNotMatchTheRootIsRejected)
{
    auto [root, history] = build_deal(
        {{Diamonds, 14},
         {Diamonds, 2},
         {Diamonds, 3},
         {Diamonds, 12},
         {Spades, 13},
         {Spades, 2}},
        everything_to_west);
    root.trump = DDS_NOTRUMP;
    root.first = North;
    root.currentTrickSuit[0] = Spades;
    root.currentTrickRank[0] = 13;
    // Root claims only one card is in progress; history's trailing count
    // (6 % 4 == 2) disagrees, before either side's cards are even compared.

    EXPECT_EQ(verify_history(root, North, history, North), HistoryVerdict::TrickLengthMismatch);
}

TEST_F(HistoryVerificationTest, ATrailingTrickWithTheWrongCardsIsRejected)
{
    // The same 52 cards, the same trailing *count* (two), but root's own
    // record of the trick in progress names a different second card than
    // the one history actually played there -- an order error the card
    // partition alone cannot catch, since it never looks at order.
    auto [root, history] = build_deal(
        {{Diamonds, 14},
         {Diamonds, 2},
         {Diamonds, 3},
         {Diamonds, 12},
         {Spades, 13},
         {Spades, 2}},
        everything_to_west);
    root.trump = DDS_NOTRUMP;
    root.first = North;
    root.currentTrickSuit[0] = Spades;
    root.currentTrickRank[0] = 13;
    root.currentTrickSuit[1] = Spades;
    root.currentTrickRank[1] = 3;  // history actually played the deuce here

    EXPECT_EQ(verify_history(root, North, history, North), HistoryVerdict::TrailingTrickMismatch);
}

// --- the free declarer/dummy cross-check --------------------------------------

TEST_F(HistoryVerificationTest, DummyDerivedVoidWhileRootShowsDummyHoldingTheSuitIsRejected)
{
    // North (declarer) leads the ace of diamonds. East follows low. South
    // (dummy) discards a spade -- void in diamonds, despite root still
    // showing dummy holding one (the five of diamonds, placed there
    // deliberately below instead of following the default distribution).
    // West follows with the queen.
    auto const hand_for = [](int suit, int rank) -> int
    {
        if (suit == Diamonds && rank == 5)
        {
            return South;
        }
        return West;
    };
    auto [root, history] =
        build_deal({{Diamonds, 14}, {Diamonds, 2}, {Spades, 3}, {Diamonds, 12}}, hand_for);
    root.trump = DDS_NOTRUMP;
    root.first = North;  // North's ace was highest; South's discard cannot win

    EXPECT_EQ(verify_history(root, North, history, North), HistoryVerdict::VoidContradiction);
}

TEST_F(HistoryVerificationTest, DeclarerDerivedVoidWhileRootShowsDeclarerHoldingTheSuitIsRejected)
{
    // East leads the ace of diamonds, South follows low, West follows the
    // queen, and North (declarer) -- last to play this trick, seated
    // after West -- discards a spade instead: void in diamonds, despite
    // root still showing declarer holding one.
    auto const hand_for = [](int suit, int rank) -> int
    {
        if (suit == Diamonds && rank == 5)
        {
            return North;
        }
        return West;
    };
    auto [root, history] =
        build_deal({{Diamonds, 14}, {Diamonds, 2}, {Diamonds, 12}, {Spades, 3}}, hand_for);
    root.trump = DDS_NOTRUMP;
    root.first = East;  // East's ace was highest; North's discard cannot win

    EXPECT_EQ(verify_history(root, North, history, East), HistoryVerdict::VoidContradiction);
}
