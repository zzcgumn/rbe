#include <belief_evaluation/rank_map.hpp>

#include <api/dds_constants.hpp>
#include <lookup_tables/lookup_tables.hpp>

namespace dds::belief_evaluation
{

auto RankMap::to_relative(int suit, int rank) const -> int
{
    // A suit or rank out of range cannot be outstanding, so 0 is correct.
    // Without the guard it would index aggr or rel_rank out of bounds.
    // Callback authors call RankMap directly, so it must fail safely.
    if (suit < 0 || suit >= DDS_SUITS || rank < 2 || rank > 14)
    {
        return 0;
    }
    return rel_rank[aggr[suit]][rank];
}

auto RankMap::to_absolute(int suit, int ordinal) const -> int
{
    // ordinal is valid over 1..13 (see win_ranks in lookup_tables.hpp). The
    // suit is guarded as in to_relative.
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
        // remainCards uses bit r for rank r; aggr uses bit r-2. Masked to 13 bits,
        // so a stray bit above rank 14 cannot push aggr[suit] past 0x1FFF and out
        // of the lookup tables.
        map.aggr[suit] = (pool >> 2) & ThirteenBitMask;
    }
    return map;
}

}  // namespace dds::belief_evaluation
