#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include <api/dll.h>
#include <utility/constants.h>

#include <belief_evaluation/layout_key.hpp>

namespace
{
    auto make_deal_with_defender_spades(int seat, unsigned spades_bits_from_rank_2) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = 0;
        deal.remainCards[seat][0] = spades_bits_from_rank_2 << 2;
        return deal;
    }
}

TEST(LayoutKey, DistinctSplitsOverASmallPoolProduceDistinctKeys)
{
    // Every 4-bit subset held by West (seat 3) must produce a distinct key.
    std::vector<std::uint64_t> keys;
    for (unsigned subset = 0; subset <= 0b1111; ++subset)
    {
        Deal const deal = make_deal_with_defender_spades(3, subset);
        keys.push_back(layout_key(deal, 3));
    }

    std::sort(keys.begin(), keys.end());
    EXPECT_EQ(std::adjacent_find(keys.begin(), keys.end()), keys.end())
        << "expected all 16 keys to be distinct";
}

TEST(LayoutKey, IdenticalSplitsProduceIdenticalKeys)
{
    Deal const a = make_deal_with_defender_spades(1, 0b1010);
    Deal const b = make_deal_with_defender_spades(1, 0b1010);
    EXPECT_EQ(layout_key(a, 1), layout_key(b, 1));
}

TEST(LayoutKey, DifferentSuitsOccupyDisjointBitRanges)
{
    Deal spades_only{};
    spades_only.remainCards[2][0] = 0b1 << 2;  // South holds deuce of spades

    Deal hearts_only{};
    hearts_only.remainCards[2][1] = 0b1 << 2;  // South holds deuce of hearts

    EXPECT_NE(layout_key(spades_only, 2), layout_key(hearts_only, 2));
}
