#include <belief_evaluation/trick.hpp>

#include <cassert>

#include <belief_evaluation/position.hpp>

namespace
{
    /// How many cards have already been played to the trick in progress:
    /// the count of leading non-zero entries of currentTrickRank. Rank 0 is
    /// the empty-slot sentinel, matching validation.cpp's led_suit().
    auto played_count(Deal const& deal) -> int
    {
        int count = 0;
        while (count < 3 && deal.currentTrickRank[count] != 0)
        {
            ++count;
        }
        return count;
    }

    /// -1 if no card has yet been played to the trick in progress, else the
    /// suit of the first card played.
    auto led_suit_of(Deal const& deal) -> int
    {
        if (deal.currentTrickRank[0] == 0)
        {
            return -1;
        }
        return deal.currentTrickSuit[0];
    }
}

auto seat_on_play(Deal const& deal) -> int
{
    return (deal.first + played_count(deal)) % DDS_HANDS;
}

auto legal_cards(Deal const& deal, int seat) -> std::array<unsigned, DDS_SUITS>
{
    Position position{};
    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        position.holding[seat][suit] = to_compacted(deal.remainCards[seat][suit]);
    }

    std::array<unsigned, DDS_SUITS> const compacted =
        legal_plays(position, seat, led_suit_of(deal));

    std::array<unsigned, DDS_SUITS> result{};
    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        result[suit] = compacted[suit] << 2;
    }
    return result;
}

auto trick_complete_winner(Deal const& deal, Card const& card) -> int
{
    // Precondition, per this function's own doxygen: exactly three cards
    // already played. An unplayed slot (rank 0) would feed
    // rank_to_bit_position() a -2, an invalid bit position, straight into
    // trick_winner()'s comparisons -- silent out-of-bounds, not a crash.
    assert(deal.currentTrickRank[0] != 0);
    assert(deal.currentTrickRank[1] != 0);
    assert(deal.currentTrickRank[2] != 0);

    std::array<int, 4> suit_played{};
    std::array<int, 4> bit_played{};
    for (int i = 0; i < 3; ++i)
    {
        suit_played[i] = deal.currentTrickSuit[i];
        bit_played[i] = rank_to_bit_position(deal.currentTrickRank[i]);
    }
    suit_played[3] = card.suit;
    bit_played[3] = rank_to_bit_position(card.rank);

    return trick_winner(deal.trump, deal.first, suit_played, bit_played);
}

auto play(Deal const& deal, Card const& card) -> Deal
{
    Deal result = deal;
    int const seat = seat_on_play(deal);
    result.remainCards[seat][card.suit] &= ~(1u << card.rank);

    int const n = played_count(deal);
    if (n < 3)
    {
        result.currentTrickSuit[n] = card.suit;
        result.currentTrickRank[n] = card.rank;
        return result;
    }

    int const winner = trick_complete_winner(deal, card);
    for (int i = 0; i < 3; ++i)
    {
        result.currentTrickSuit[i] = 0;
        result.currentTrickRank[i] = 0;
    }
    result.first = winner;
    return result;
}
