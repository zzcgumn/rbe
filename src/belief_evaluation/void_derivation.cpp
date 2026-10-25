#include <belief_evaluation/void_derivation.hpp>

#include <algorithm>
#include <array>
#include <cassert>

#include <belief_evaluation/position.hpp>
#include <belief_evaluation/trick.hpp>

namespace dds::belief_evaluation
{

namespace
{
    // The size of the PlayTraceBin arrays.
    constexpr int MaxHistoryLength = 52;

    // A card's suit and rank are read up to 52 times per call. So they are
    // clamped silently, not asserted, as in RankMap::to_relative. 0 (Spades)
    // and rank 2 are safe: the result is garbage, but never an out-of-bounds
    // `voids[seat][led_suit]` or an overflow in `rank_to_bit_position`.
    auto safe_suit(int suit) -> int
    {
        return (suit >= 0 && suit < DDS_SUITS) ? suit : 0;
    }

    auto safe_rank(int rank) -> int
    {
        return (rank >= 2 && rank <= 14) ? rank : 2;
    }
}

auto derive_voids(PlayTraceBin const& history, int opening_leader, int trump) -> VoidsBySeat
{
    VoidsBySeat voids{};

    // Caller errors, both. Asserted; the clamps below keep the function total
    // when asserts are off.
    assert(history.number >= 0 && history.number <= MaxHistoryLength);
    assert(opening_leader >= 0 && opening_leader < DDS_HANDS);
    int const history_length = std::clamp(history.number, 0, MaxHistoryLength);
    int leader = ((opening_leader % DDS_HANDS) + DDS_HANDS) % DDS_HANDS;

    for (int start = 0; start < history_length; start += 4)
    {
        int const cards_in_trick = std::min(4, history_length - start);
        int const led_suit = safe_suit(history.suit[start]);

        std::array<int, 4> suit_played{};
        std::array<int, 4> bit_played{};

        for (int i = 0; i < cards_in_trick; ++i)
        {
            int const seat = (leader + i) % DDS_HANDS;
            int const suit = safe_suit(history.suit[start + i]);

            // The void follows from the suit played, not from who wins. This covers
            // a ruff and a discard alike.
            if (suit != led_suit)
            {
                voids[seat][led_suit] = true;
            }

            suit_played[i] = suit;
            bit_played[i] = rank_to_bit_position(safe_rank(history.rank[start + i]));
        }

        // A trailing partial trick has had its voids recorded above. No winner
        // is needed: no later trick follows.
        if (cards_in_trick == 4)
        {
            leader = trick_winner(trump, leader, suit_played, bit_played);
        }
    }

    return voids;
}

}  // namespace dds::belief_evaluation
