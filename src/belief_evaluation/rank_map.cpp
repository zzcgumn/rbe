#include <belief_evaluation/rank_map.hpp>

#include <lookup_tables/lookup_tables.hpp>
#include <utility/constants.h>

namespace dds::belief_evaluation
{

auto RankMap::to_relative(int suit, int rank) const -> int
{
    // Out-of-range suit/rank cannot be outstanding, so 0 ("not outstanding")
    // is the correct answer as well as the safe one — without this guard,
    // an out-of-range suit indexes aggr out of bounds and an out-of-range
    // rank indexes rel_rank's second dimension out of bounds, both undefined
    // behaviour. RankMap is a public type callback authors read directly, so
    // it must fail safely rather than assume its callers validated first.
    if (suit < 0 || suit >= DDS_SUITS || rank < 2 || rank > 14)
    {
        return 0;
    }
    return rel_rank[aggr[suit]][rank];
}

auto RankMap::to_absolute(int suit, int ordinal) const -> int
{
    // ordinal is a count of top cards to keep, valid over 0..13 (win_ranks'
    // second dimension); see win_ranks' doxygen in lookup_tables.hpp. Guard
    // both ends and the suit for the same reason as to_relative above.
    if (suit < 0 || suit >= DDS_SUITS || ordinal <= 0 || ordinal > 13)
    {
        return 0;
    }
    unsigned short const top_n = win_ranks[aggr[suit]][ordinal];
    unsigned short const top_n_minus_one = win_ranks[aggr[suit]][ordinal - 1];
    return highest_rank[top_n ^ top_n_minus_one];
}

auto make_rank_map(Deal const& deal) -> RankMap
{
    constexpr unsigned ThirteenBitMask = 0x1FFFu;

    RankMap map{};
    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        unsigned pool = 0;
        for (int hand = 0; hand < DDS_HANDS; ++hand)
        {
            pool |= deal.remainCards[hand][suit];
        }
        // remainCards sets bit r for absolute rank r; aggr and the lookup
        // tables use the compacted convention, bit r-2 for absolute rank r.
        // A well-formed Deal never sets a remainCards bit above rank 14, but
        // nothing in the type enforces that, and an unmasked stray bit here
        // would push aggr[suit] past 0x1FFF — out of bounds for every
        // rel_rank/win_ranks/highest_rank lookup that indexes by aggr.
        map.aggr[suit] = (pool >> 2) & ThirteenBitMask;
    }
    return map;
}

}  // namespace dds::belief_evaluation
