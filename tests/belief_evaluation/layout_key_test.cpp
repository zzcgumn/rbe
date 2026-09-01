#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include <utility/constants.h>

#include <belief_evaluation/dds_types.hpp>
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

TEST(LayoutKey, RejectsAnOutOfRangeSeatWithoutUndefinedBehaviour)
{
    Deal const deal = make_deal_with_defender_spades(1, 0b1010);
    EXPECT_EQ(layout_key(deal, DDS_HANDS), 0u);
    EXPECT_EQ(layout_key(deal, -1), 0u);
}

TEST(LayoutKey, StrayBitsAboveRankFourteenDoNotLeakIntoTheNextSuitsField)
{
    // A well-formed Deal never sets remainCards bits above bit 14 (ace), but
    // nothing in the type stops it. Without masking after `>> 2`, a stray
    // high bit here would shift into spades' 13-bit field of the packed key
    // (hearts occupies bits 13..25, so anything above hearts' own 13 bits
    // once shifted would collide with spades' field at bits 0..12... in
    // practice it collides one field up, into the next suit checked below).
    Deal clean{};
    clean.remainCards[2][1] = 0b1 << 2;  // South: deuce of hearts only

    Deal with_stray_bits = clean;
    with_stray_bits.remainCards[2][1] |= 1u << 20;  // stray bit far above any legal rank

    EXPECT_EQ(layout_key(clean, 2), layout_key(with_stray_bits, 2))
        << "a stray bit above rank 14 must not change the packed key";
}
