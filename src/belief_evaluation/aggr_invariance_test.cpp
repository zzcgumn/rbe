#include <gtest/gtest.h>

#include <vector>

#include <api/dll.h>
#include <utility/constants.h>

#include <belief_evaluation/rank_map.hpp>

TEST(AggrInvariance, IdenticalAcrossEveryDefenderSplitOfTheSamePool)
{
    // Declarer (South, seat 2) and dummy (North, seat 0) hold fixed cards;
    // East/West share the outstanding pool 0b1111 (four spades) in every
    // possible two-way split.
    auto const make_deal = [](unsigned east_spades, unsigned west_spades)
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = 1;
        deal.remainCards[0][0] = 1u << 6;  // North (dummy): fifth spade (rank 6)
        deal.remainCards[2][0] = 1u << 7;  // South (declarer): sixth spade (rank 7)
        deal.remainCards[1][0] = east_spades;
        deal.remainCards[3][0] = west_spades;
        return deal;
    };

    unsigned const pool = 0b1111 << 2;  // four outstanding spades (ranks 2-5) to split
    std::vector<RankMap> maps;
    // Standard submask enumeration: every subset of `pool`, including 0 and
    // `pool` itself, each visited exactly once.
    unsigned east_subset = pool;
    while (true)
    {
        unsigned const west_subset = pool & ~east_subset;
        maps.push_back(make_rank_map(make_deal(east_subset, west_subset)));
        if (east_subset == 0)
        {
            break;
        }
        east_subset = (east_subset - 1) & pool;
    }

    ASSERT_EQ(maps.size(), 16u);
    for (auto const& map : maps)
    {
        EXPECT_EQ(map.aggr[0], maps.front().aggr[0]);
    }
    for (auto const& map : maps)
    {
        for (int suit = 1; suit < DDS_SUITS; ++suit)
        {
            EXPECT_EQ(map.aggr[suit], 0u);
        }
    }
}
