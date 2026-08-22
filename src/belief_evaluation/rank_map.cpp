#include <belief_evaluation/rank_map.hpp>

#include <lookup_tables/lookup_tables.hpp>
#include <utility/constants.h>

auto RankMap::to_relative(int suit, int rank) const -> int
{
    return rel_rank[aggr[suit]][rank];
}

auto RankMap::to_absolute(int suit, int ordinal) const -> int
{
    if (ordinal <= 0)
    {
        return 0;
    }
    unsigned short const top_n = win_ranks[aggr[suit]][ordinal];
    unsigned short const top_n_minus_one = win_ranks[aggr[suit]][ordinal - 1];
    return highest_rank[top_n ^ top_n_minus_one];
}

auto make_rank_map(Deal const& deal) -> RankMap
{
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
        map.aggr[suit] = pool >> 2;
    }
    return map;
}
