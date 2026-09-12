#include <belief_evaluation/history_verification.hpp>

#include <array>

#include <belief_evaluation/position.hpp>
#include <belief_evaluation/trick.hpp>
#include <belief_evaluation/void_derivation.hpp>
#include <utility/constants.h>

namespace dds::belief_evaluation
{

namespace
{
    constexpr int DeckSize = DDS_SUITS * 13;

    auto card_index(int suit, int rank) -> int
    {
        return suit * 13 + (rank - 2);
    }

    /// How many cards `root` shows played to its own trick in progress:
    /// the count of leading non-zero entries of currentTrickRank. Mirrors
    /// trick.cpp's own played_count() exactly (rank 0 is the empty-slot
    /// sentinel, matching validation.cpp's led_suit()) -- duplicated here
    /// rather than exported, since trick.cpp keeps it as an internal
    /// implementation detail of a different, Deal-mutating operation.
    auto played_count(Deal const& deal) -> int
    {
        int count = 0;
        while (count < 3 && deal.currentTrickRank[count] != 0)
        {
            ++count;
        }
        return count;
    }

    /// The seat leading the trick `history`'s own trailing cards belong to:
    /// replays every *complete* trick in `history` (blocks of four, stopping
    /// short of a trailing partial one) from `opening_leader`, advancing by
    /// `trick_winner()` exactly as `derive_voids` does internally, but
    /// without needing to build a `VoidsBySeat` to get there. Every seat
    /// `trick_winner` ever attributes a play to is `(leader + offset) %
    /// DDS_HANDS`, where `offset` depends only on the cards played, never on
    /// `leader` itself -- so this replay is a single fixed rotation of
    /// `opening_leader`, and comparing its result to `root.first` is exactly
    /// equivalent to checking `opening_leader` was right in the first place.
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
    // Check 0: the shape of the input itself, before either value is ever
    // used to index anything below (card_index() indirectly via history.number,
    // and leader_of_trailing_trick()/derive_voids() via opening_leader).
    // history is caller input like any other value this function checks --
    // reported here, not left for derive_voids's own asserted fallback to
    // catch on this function's behalf (see that function's own doxygen).
    if (history.number < 0 || history.number > DeckSize || opening_leader < 0 || opening_leader >= DDS_HANDS)
    {
        return HistoryVerdict::InvalidInput;
    }

    // Check 1: the card partition. `seen[card_index(suit, rank)]` becomes
    // true the first time that card is found on either side -- history's
    // played cards first, then every hand's remainCards in root. Cards of
    // the trick in progress are already removed from root.remainCards by
    // the time a root is built (play() strips the played card from
    // remainCards on every call, whether or not it resolves the trick), so
    // they belong to history's side of the partition and never root's;
    // confirmed against play()'s own behaviour, not assumed.
    //
    // Part one: history's own duplicates.
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

    // Check 3: the leader. Neither of the two checks above depends on which
    // seat played which card, only on which cards in what order, so a
    // history whose every card is attributed to the wrong seat -- shifted
    // by the same fixed rotation from opening_leader, the only way a wrong
    // opening_leader can go wrong (see leader_of_trailing_trick's own
    // doxygen) -- passes both of them. This is the one check that catches
    // it, and it must run before check 4 below: derive_voids trusts
    // opening_leader completely, so its own output means nothing once this
    // has failed.
    if (leader_of_trailing_trick(history, opening_leader, root.trump) != root.first)
    {
        return HistoryVerdict::LeaderMismatch;
    }

    // Check 4: the free cross-check. Only declarer and dummy: root gives
    // their holdings exactly, but a defender's own split in root is not
    // itself binding -- only the two defenders' pooled union is (see
    // DefenderPool's own doxygen) -- so a defender void derived here says
    // nothing about whether *this particular* root is wrong.
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
