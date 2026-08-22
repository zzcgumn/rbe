#include <gtest/gtest.h>

#include <api/dll.h>
#include <lookup_tables/lookup_tables.hpp>
#include <utility/constants.h>

#include <belief_evaluation/types.hpp>
#include <belief_evaluation/rank_map.hpp>

class RankMapTest : public ::testing::Test
{
protected:
    auto SetUp() -> void override
    {
        init_lookup_tables();
    }
};

TEST_F(RankMapTest, RoundTripHoldsForEveryOutstandingRankInEveryAggregate)
{
    for (unsigned aggregate = 1; aggregate < 8192; ++aggregate)
    {
        RankMap map{};
        map.aggr[0] = aggregate;
        for (int rank = 2; rank <= 14; ++rank)
        {
            int const relative = map.to_relative(0, rank);
            if (relative == 0)
            {
                continue;  // rank not outstanding in this aggregate
            }
            EXPECT_EQ(map.to_absolute(0, relative), rank)
                << "aggregate=" << aggregate << " rank=" << rank;
        }
    }
}

TEST_F(RankMapTest, NotOutstandingRankMapsToZero)
{
    RankMap map{};
    map.aggr[0] = 0;  // nothing outstanding
    EXPECT_EQ(map.to_relative(0, 14), 0);
}

TEST_F(RankMapTest, BuildsAggrAsUnionOfAllFourHandsHoldingsPerSuit)
{
    Deal deal{};
    deal.trump = DDS_NOTRUMP;
    deal.first = 0;
    // remainCards uses dds's public convention: bit r = absolute rank r.
    deal.remainCards[0][0] = 1u << 2;  // N spades: deuce (rank 2)
    deal.remainCards[1][0] = 1u << 3;  // E spades: three (rank 3)
    deal.remainCards[2][0] = 1u << 4;  // S spades: four (rank 4)
    deal.remainCards[3][0] = 0;         // W spades: void

    RankMap const map = make_rank_map(deal);

    // aggr uses the compacted convention (bit 0 = deuce), so the three
    // outstanding ranks 2, 3, 4 land at bits 0, 1, 2.
    EXPECT_EQ(map.aggr[0], 0b0111u);
}
