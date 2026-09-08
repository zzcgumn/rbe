#include <belief_evaluation/defender_split.hpp>

#include <bit>

#include <utility/constants.h>

namespace dds::belief_evaluation
{

auto defender_pool_decomposition(Deal const& root, int declarer) -> DefenderPool
{
    // declarer and dummy are never read: the pool is exactly the two
    // defenders' union, and neither holding contributes to it whatever it
    // is.
    int const fixed_seat = (declarer + 1) % DDS_HANDS;
    int const other_seat = (declarer + 3) % DDS_HANDS;

    DefenderPool pool;
    // std::popcount, not half of the eventual cards.size(): mid-trick the
    // two defenders' counts legitimately differ by one, and this must read
    // the fixed seat's own remaining holding rather than assume symmetry.
    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        pool.fixed_seat_count += std::popcount(root.remainCards[fixed_seat][suit]);
    }

    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        unsigned const suit_pool = root.remainCards[fixed_seat][suit] | root.remainCards[other_seat][suit];
        // Ranks ascending, bit r for absolute rank r -- Deal::remainCards'
        // own convention, no >> 2 shift (that compacted convention belongs
        // to aggr and the lookup tables, not here; see rank_map.cpp's own
        // comment on the distinction).
        for (int rank = 2; rank <= 14; ++rank)
        {
            if ((suit_pool & (1u << rank)) != 0)
            {
                pool.cards.push_back(Card{suit, rank});
            }
        }
    }
    return pool;
}

}  // namespace dds::belief_evaluation
