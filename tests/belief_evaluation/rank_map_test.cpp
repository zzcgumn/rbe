#include <gtest/gtest.h>

#include <api/dds_data_types.hpp>
#include <lookup_tables/lookup_tables.hpp>
#include <utility/constants.h>

#include <belief_evaluation/types.hpp>
#include <belief_evaluation/rank_map.hpp>

using namespace dds::belief_evaluation;

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

TEST_F(RankMapTest, ToRelativeIsBoundsSafeForOutOfRangeSuitOrRank)
{
    RankMap map{};
    map.aggr[0] = 0b111;  // deuce, three, four outstanding

    EXPECT_EQ(map.to_relative(-1, 2), 0);
    EXPECT_EQ(map.to_relative(DDS_SUITS, 2), 0);
    EXPECT_EQ(map.to_relative(0, 1), 0);   // below the lowest legal rank
    EXPECT_EQ(map.to_relative(0, 15), 0);  // above the highest legal rank
}

TEST_F(RankMapTest, ToAbsoluteIsBoundsSafeForOutOfRangeSuitOrOrdinal)
{
    RankMap map{};
    map.aggr[0] = 0b111;

    EXPECT_EQ(map.to_absolute(-1, 1), 0);
    EXPECT_EQ(map.to_absolute(DDS_SUITS, 1), 0);
    EXPECT_EQ(map.to_absolute(0, -1), 0);
    EXPECT_EQ(map.to_absolute(0, 14), 0);  // win_ranks' second dimension is 0..13
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

TEST_F(RankMapTest, MasksAggrTo13BitsEvenWithStrayRemainCardsBitsAboveRankFourteen)
{
    // A well-formed Deal never sets a remainCards bit above rank 14, but
    // nothing in the type enforces that. Without masking after `>> 2`,
    // aggr[suit] could exceed 0x1FFF (8191) and later index rel_rank/
    // win_ranks/highest_rank (all sized [8192]) out of bounds.
    Deal deal{};
    deal.remainCards[0][0] = (1u << 2) | (1u << 20);  // deuce, plus a stray high bit

    RankMap const map = make_rank_map(deal);

    EXPECT_LE(map.aggr[0], 0x1FFFu);
    EXPECT_EQ(map.aggr[0], 0b1u);  // only the deuce should register
}
