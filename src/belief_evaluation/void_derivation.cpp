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
    // The only range PlayTraceBin::suit/rank's 52-element arrays actually
    // hold -- see derive_voids' own doxygen for why this is asserted, not
    // merely assumed, and clamped to rather than left unchecked.
    constexpr int MaxHistoryLength = 52;
}

auto derive_voids(PlayTraceBin const& history, int opening_leader, int trump) -> VoidsBySeat
{
    VoidsBySeat voids{};

    // Caller error, both -- asserted for a build where that is caught
    // loudly; the clamp/normalise below is what stops it becoming an
    // out-of-bounds read (a malformed history.number) or an out-of-range
    // seat (a malformed opening_leader) in a build where it is not. This
    // is what keeps the function total per its own doxygen, on every
    // input, not only the ones verify_history happened to check first.
    assert(history.number >= 0 && history.number <= MaxHistoryLength);
    assert(opening_leader >= 0 && opening_leader < DDS_HANDS);
    int const history_length = std::clamp(history.number, 0, MaxHistoryLength);
    int leader = ((opening_leader % DDS_HANDS) + DDS_HANDS) % DDS_HANDS;

    for (int start = 0; start < history_length; start += 4)
    {
        int const cards_in_trick = std::min(4, history_length - start);
        int const led_suit = history.suit[start];

        std::array<int, 4> suit_played{};
        std::array<int, 4> bit_played{};

        for (int i = 0; i < cards_in_trick; ++i)
        {
            int const seat = (leader + i) % DDS_HANDS;
            int const suit = history.suit[start + i];

            // The void follows from the suit played, full stop -- never
            // from whether this card goes on to win the trick. A ruff and
            // a plain discard are both handled by this one comparison.
            if (suit != led_suit)
            {
                voids[seat][led_suit] = true;
            }

            suit_played[i] = suit;
            bit_played[i] = rank_to_bit_position(history.rank[start + i]);
        }

        // A trailing partial trick (history ends mid-trick, as
        // ObservationState::history always does at a mid-trick root) has
        // already had its voids recorded above; there is no winner to
        // compute and none is needed, since there is no further trick in
        // history to derive a leader for.
        if (cards_in_trick == 4)
        {
            leader = trick_winner(trump, leader, suit_played, bit_played);
        }
    }

    return voids;
}

}  // namespace dds::belief_evaluation
