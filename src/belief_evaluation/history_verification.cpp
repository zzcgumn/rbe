#include <belief_evaluation/history_verification.hpp>

#include <array>

#include <api/dds_constants.hpp>
#include <belief_evaluation/position.hpp>
#include <belief_evaluation/trick.hpp>
#include <belief_evaluation/void_derivation.hpp>

namespace dds::belief_evaluation
{

namespace
{
    constexpr int DeckSize = DDS_SUITS * 13;

    auto card_index(int suit, int rank) -> int
    {
        return suit * 13 + (rank - 2);
    }

    /// The number of cards played to the current trick of `root`: the leading
    /// non-zero entries of currentTrickRank (rank 0 marks an empty slot). A
    /// copy of the private played_count() in trick.cpp.
    auto played_count(Deal const& deal) -> int
    {
        int count = 0;
        while (count < 3 && deal.currentTrickRank[count] != 0)
        {
            ++count;
        }
        return count;
    }

    /// The leader of the trailing trick of `history`. Replays the complete
    /// tricks from `opening_leader` with `trick_winner()`, as `derive_voids`
    /// does. The result is `opening_leader` rotated by an offset that depends
    /// only on the cards. So comparing it with `root.first` checks
    /// `opening_leader`.
    auto leader_of_trailing_trick(PlayTraceBin const& history, int opening_leader, int trump) -> int
    {
        int leader = opening_leader;
        for (int start = 0; start + 4 <= history.number; start += 4)
        {
            std::array<int, 4> suit_played{};
            std::array<int, 4> bit_played{};
            for (int i = 0; i < 4; ++i)
            {
                suit_played[i] = history.suit[start + i];
                bit_played[i] = rank_to_bit_position(history.rank[start + i]);
            }
            leader = trick_winner(trump, leader, suit_played, bit_played);
        }
        return leader;
    }
}

auto verify_history(Deal const& root, int declarer, PlayTraceBin const& history, int opening_leader)
    -> HistoryVerdict
{
    // Check 0: the input shape, before anything is used as an index:
    // history.number, opening_leader and declarer here, each card's suit and
    // rank next. declarer is checked because check 4 indexes
    // voids[declarer] and voids[dummy]. Report bad input here; do not rely on
    // the clamping fallback in derive_voids, which is for its direct callers.
    if (history.number < 0 || history.number > DeckSize || opening_leader < 0 || opening_leader >= DDS_HANDS
        || declarer < 0 || declarer >= DDS_HANDS)
    {
        return HistoryVerdict::InvalidInput;
    }
    for (int i = 0; i < history.number; ++i)
    {
        if (history.suit[i] < 0 || history.suit[i] >= DDS_SUITS || history.rank[i] < 2
            || history.rank[i] > 14)
        {
            return HistoryVerdict::InvalidInput;
        }
    }

    // Check 1: the card partition. `seen[card_index(suit, rank)]` is set the
    // first time a card is found: first the played cards, then every hand in
    // root. play() removes a card from remainCards when it is played, so the
    // current trick counts as played.
    //
    // Part one: duplicates in history.
    std::array<bool, DeckSize> seen{};

    for (int i = 0; i < history.number; ++i)
    {
        int const index = card_index(history.suit[i], history.rank[i]);
        if (seen[index])
        {
            return HistoryVerdict::DuplicatedCard;
        }
        seen[index] = true;
    }

    // Part two: cards root still holds, checked against the same seen set.
    for (int hand = 0; hand < DDS_HANDS; ++hand)
    {
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            unsigned const holding = root.remainCards[hand][suit];
            for (int rank = 2; rank <= 14; ++rank)
            {
                if ((holding & (1u << rank)) == 0)
                {
                    continue;
                }
                int const index = card_index(suit, rank);
                if (seen[index])
                {
                    return HistoryVerdict::CardPlayedAndHeld;
                }
                seen[index] = true;
            }
        }
    }

    // Part three: nothing left unaccounted for on either side.
    for (bool const card_seen : seen)
    {
        if (! card_seen)
        {
            return HistoryVerdict::MissingCard;
        }
    }

    // Check 2: the trailing trick, count first, then the cards themselves.
    int const trailing = played_count(root);
    if (history.number % 4 != trailing)
    {
        return HistoryVerdict::TrickLengthMismatch;
    }
    for (int i = 0; i < trailing; ++i)
    {
        int const history_index = history.number - trailing + i;
        if (history.suit[history_index] != root.currentTrickSuit[i]
            || history.rank[history_index] != root.currentTrickRank[i])
        {
            return HistoryVerdict::TrailingTrickMismatch;
        }
    }

    // Check 3: the leader. Checks 1 and 2 ignore which seat played each card,
    // so a history rotated by a wrong opening_leader passes them. Must run
    // before check 4: derive_voids trusts opening_leader.
    if (leader_of_trailing_trick(history, opening_leader, root.trump) != root.first)
    {
        return HistoryVerdict::LeaderMismatch;
    }

    // Check 4: voids, for declarer and dummy only. root gives their holdings
    // exactly. It does not fix the defenders' split, so a defender void
    // proves nothing here.
    VoidsBySeat const voids = derive_voids(history, opening_leader, root.trump);
    int const dummy = (declarer + 2) % DDS_HANDS;
    for (int const seat : {declarer, dummy})
    {
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            if (voids[seat][suit] && root.remainCards[seat][suit] != 0)
            {
                return HistoryVerdict::VoidContradiction;
            }
        }
    }

    return HistoryVerdict::Consistent;
}

}  // namespace dds::belief_evaluation
