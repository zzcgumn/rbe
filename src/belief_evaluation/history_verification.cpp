#include <belief_evaluation/history_verification.hpp>

#include <array>

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
}

auto verify_history(Deal const& root, int declarer, PlayTraceBin const& history, int opening_leader)
    -> HistoryVerdict
{
    // Step 1+2: the card partition. `seen[card_index(suit, rank)]` becomes
    // true the first time that card is found on either side -- history's
    // played cards first, then every hand's remainCards in root. Cards of
    // the trick in progress are already removed from root.remainCards by
    // the time a root is built (play() strips the played card from
    // remainCards on every call, whether or not it resolves the trick), so
    // they belong to history's side of the partition and never root's;
    // confirmed against play()'s own behaviour, not assumed.
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

    // Step 3: nothing left unaccounted for on either side.
    for (bool const card_seen : seen)
    {
        if (! card_seen)
        {
            return HistoryVerdict::MissingCard;
        }
    }

    // Step 4: the trailing trick, count first, then the cards themselves.
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

    // Step 5: the free cross-check. Only declarer and dummy: root gives
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
