#include <gtest/gtest.h>

#include <functional>
#include <set>
#include <utility>
#include <vector>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/history_verification.hpp>

// Tests for verify_history: accepted histories, malformed input, the card
// partition, the trailing trick, the leader and the declarer/dummy
// cross-check.

namespace be = dds::belief_evaluation;

using be::HistoryVerdict;
using be::verify_history;

namespace
{
    constexpr int Spades = 0;
    constexpr int Diamonds = 2;
    constexpr int Clubs = 3;

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

    /// Builds a root and history that together account for the whole 52-card
    /// deck. `played` (in order) becomes the history, and every other card
    /// goes to `hand_for(suit, rank)`, in that seat's remainCards in root. So
    /// a fixture states only what it cares about: the played prefix and where
    /// `hand_for` departs from its default. The other 40-odd cards are placed
    /// mechanically and cannot be got wrong by hand. `root.trump`,
    /// `root.first` and `root.currentTrick*` keep their defaults; callers set
    /// what they need.
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

    /// The default distribution for every fixture below: every unplayed card
    /// goes to West. A fixture states only the few cards that differ.
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
    // North leads the ace of diamonds, East and South follow low, and West
    // follows with the queen. North's ace wins, so North leads next; no trick
    // is in progress at this root.
    auto [root, history] = build_deal(
        {{Diamonds, 14}, {Diamonds, 2}, {Diamonds, 3}, {Diamonds, 12}}, everything_to_west);
    root.trump = DDS_NOTRUMP;
    root.first = North;

    EXPECT_EQ(verify_history(root, North, history, North), HistoryVerdict::Consistent);
}

TEST_F(HistoryVerificationTest, ACorrectHistoryMidTrickIsAccepted)
{
    // Trick one as above (North wins). Trick two: North leads the king of
    // spades and East follows with the two, so two cards are in the trick in
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

// --- the shape of the input itself --------------------------------------------

TEST_F(HistoryVerificationTest, AnOutOfRangeHistoryLengthIsRejected)
{
    auto [root, history] = build_deal(
        {{Diamonds, 14}, {Diamonds, 2}, {Diamonds, 3}, {Diamonds, 12}}, everything_to_west);
    root.trump = DDS_NOTRUMP;
    root.first = North;
    history.number = 53;  // one past the 52 elements of PlayTraceBin::suit/rank

    EXPECT_EQ(verify_history(root, North, history, North), HistoryVerdict::InvalidInput);
}

TEST_F(HistoryVerificationTest, ANegativeHistoryLengthIsRejected)
{
    auto [root, history] = build_deal({}, everything_to_west);
    root.trump = DDS_NOTRUMP;
    root.first = North;
    history.number = -1;

    EXPECT_EQ(verify_history(root, North, history, North), HistoryVerdict::InvalidInput);
}

TEST_F(HistoryVerificationTest, AnOutOfRangeOpeningLeaderIsRejected)
{
    auto [root, history] = build_deal(
        {{Diamonds, 14}, {Diamonds, 2}, {Diamonds, 3}, {Diamonds, 12}}, everything_to_west);
    root.trump = DDS_NOTRUMP;
    root.first = North;

    EXPECT_EQ(
        verify_history(root, North, history, /*opening_leader=*/DDS_HANDS), HistoryVerdict::InvalidInput);
    EXPECT_EQ(
        verify_history(root, North, history, /*opening_leader=*/-1), HistoryVerdict::InvalidInput);
}

TEST_F(HistoryVerificationTest, AnOutOfRangeDeclarerIsRejected)
{
    // declarer flows unchecked into check 4's voids[declarer][suit], and
    // through dummy = (declarer + 2) % DDS_HANDS into voids[dummy][suit]. An
    // out-of-range declarer would index either one out of bounds if this
    // check did not catch it first.
    auto [root, history] = build_deal(
        {{Diamonds, 14}, {Diamonds, 2}, {Diamonds, 3}, {Diamonds, 12}}, everything_to_west);
    root.trump = DDS_NOTRUMP;
    root.first = North;

    EXPECT_EQ(
        verify_history(root, /*declarer=*/DDS_HANDS, history, North), HistoryVerdict::InvalidInput);
    EXPECT_EQ(
        verify_history(root, /*declarer=*/-1, history, North), HistoryVerdict::InvalidInput);
}

TEST_F(HistoryVerificationTest, AMalformedCardSuitIsRejected)
{
    // The next check (the card partition) indexes the 52-entry "seen" set
    // with card_index(suit, rank). An out-of-range suit would index it out of
    // bounds if this check did not catch it first.
    auto [root, history] = build_deal(
        {{Diamonds, 14}, {Diamonds, 2}, {Diamonds, 3}, {Diamonds, 12}}, everything_to_west);
    root.trump = DDS_NOTRUMP;
    root.first = North;
    history.suit[1] = 99;  // out of [0, DDS_SUITS)

    EXPECT_EQ(verify_history(root, North, history, North), HistoryVerdict::InvalidInput);
}

TEST_F(HistoryVerificationTest, AMalformedCardRankIsRejected)
{
    auto [root, history] = build_deal(
        {{Diamonds, 14}, {Diamonds, 2}, {Diamonds, 3}, {Diamonds, 12}}, everything_to_west);
    root.trump = DDS_NOTRUMP;
    root.first = North;
    history.rank[2] = 1;  // out of [2, 14]

    EXPECT_EQ(verify_history(root, North, history, North), HistoryVerdict::InvalidInput);
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
    // History says the ace of diamonds was just played, and root still shows
    // North holding it.
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
    // Root has one card in progress, but history's trailing count is
    // 6 % 4 == 2. The counts disagree before any cards are compared.

    EXPECT_EQ(verify_history(root, North, history, North), HistoryVerdict::TrickLengthMismatch);
}

TEST_F(HistoryVerificationTest, ATrailingTrickWithTheWrongCardsIsRejected)
{
    // The same 52 cards and the same trailing count (two), but root's trick in
    // progress names a different second card from the one history played.
    // This is an order error, and the card partition cannot catch it because
    // it ignores order.
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

// --- the leader ---------------------------------------------------------------

TEST_F(HistoryVerificationTest, AWrongOpeningLeaderIsRejectedEvenWhenTheCardsAndOrderAreRight)
{
    // North leads the ace of diamonds, East discards a club (void in
    // diamonds), South follows low, and West follows with the queen. North's
    // ace wins, so root.first really is North. The history is correct: the
    // same 52 cards in the same order, and the trailing trick matches (empty,
    // as the trick is resolved). Neither the card partition nor the
    // trailing-trick check depends on which seat played which card.
    //
    // Giving South as the opening leader rotates every seat by two. East's
    // void moves to West, and North's play to South. West is neither declarer
    // (North) nor dummy (South), so the declarer/dummy cross-check does not
    // catch it. Only checking the replayed leader against root.first does.
    auto [root, history] =
        build_deal({{Diamonds, 14}, {Clubs, 2}, {Diamonds, 3}, {Diamonds, 12}}, [](int, int)
                    { return North; });
    root.trump = DDS_NOTRUMP;
    root.first = North;

    EXPECT_EQ(verify_history(root, North, history, South), HistoryVerdict::LeaderMismatch);
}

TEST_F(HistoryVerificationTest, AWrongOpeningLeaderMidTrickIsAlsoRejected)
{
    // The same trap, but the trailing trick is not empty, and the wrong leader
    // is off by one seat instead of two. This checks that the test does not
    // depend on either detail. Trick one (spades) ends with North's ace
    // winning, so North leads trick two: a diamond, and East, void, discards a
    // club.
    //
    // Off by one, the void moves to South (dummy) instead of a defender. But
    // South holds no diamonds here (every card not placed goes to North). So
    // the cross-check's condition, "dummy void in a suit dummy still holds",
    // is not met either. A dummy with no cards in the suit is an ordinary
    // position.
    auto [root, history] = build_deal(
        {{Spades, 14},
         {Spades, 2},
         {Spades, 3},
         {Spades, 12},
         {Diamonds, 9},
         {Clubs, 5}},
        [](int, int) { return North; });
    root.trump = DDS_NOTRUMP;
    root.first = North;
    root.currentTrickSuit[0] = Diamonds;
    root.currentTrickRank[0] = 9;
    root.currentTrickSuit[1] = Clubs;
    root.currentTrickRank[1] = 5;

    // The true opening leader is North (it won trick one, so it also leads
    // trick two). East, one seat off, is a plausible typing mistake.
    EXPECT_EQ(verify_history(root, North, history, East), HistoryVerdict::LeaderMismatch);
}

// --- the free declarer/dummy cross-check --------------------------------------

TEST_F(HistoryVerificationTest, DummyDerivedVoidWhileRootShowsDummyHoldingTheSuitIsRejected)
{
    // North (declarer) leads the ace of diamonds, and East follows low. South
    // (dummy) discards a spade, void in diamonds, although root still shows
    // dummy holding the five of diamonds (placed there below on purpose). West
    // follows with the queen.
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
    // East leads the ace of diamonds, South follows low, and West follows
    // with the queen. North (declarer), last to play, discards a spade: void
    // in diamonds, although root still shows declarer holding one.
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
