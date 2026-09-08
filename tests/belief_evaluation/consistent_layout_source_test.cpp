#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <vector>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

#include <belief_evaluation/consistent_layout_source.hpp>
#include <belief_evaluation/node.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;

using be::ConsistentLayoutSource;
using be::holding;
using be::is_consistent;
using be::layout_key;

namespace
{
    constexpr int Spades = 0;
    constexpr int Hearts = 1;
    constexpr int Diamonds = 2;
    constexpr int Clubs = 3;

    constexpr int North = 0;  // declarer in every fixture below
    constexpr int East = 1;
    constexpr int South = 2;  // dummy
    constexpr int West = 3;

    /// Ten pooled cards, five each -- C(10, 5) = 252, small enough to
    /// enumerate exhaustively, and large enough that sampling is
    /// meaningful. Not mid-trick; the mid-trick shape is covered by its
    /// own fixture below.
    auto make_ten_card_pool_root() -> Deal
    {
        Deal root{};
        root.trump = DDS_NOTRUMP;
        root.first = North;
        root.remainCards[North][Spades] = holding({14, 13});  // declarer
        root.remainCards[South][Hearts] = holding({14, 13});  // dummy
        root.remainCards[East][Diamonds] = holding({2, 3, 4});
        root.remainCards[East][Clubs] = holding({5, 6});
        root.remainCards[West][Diamonds] = holding({7, 8});
        root.remainCards[West][Clubs] = holding({9, 10, 11});
        return root;
    }

    auto make_mid_trick_root() -> Deal
    {
        Deal root{};
        root.trump = DDS_NOTRUMP;
        root.first = North;
        root.currentTrickSuit[0] = Diamonds;
        root.currentTrickRank[0] = 14;  // East already played the ace
        root.remainCards[North][Spades] = holding({13});
        root.remainCards[South][Hearts] = holding({13});
        root.remainCards[East][Diamonds] = holding({2, 3});
        root.remainCards[West][Diamonds] = holding({4, 5, 6});
        return root;
    }

    auto make_small_single_suit_root() -> Deal
    {
        Deal root{};
        root.trump = Spades;
        root.first = West;
        root.remainCards[North][Hearts] = holding({14});
        root.remainCards[South][Clubs] = holding({14});
        root.remainCards[East][Spades] = holding({2});
        root.remainCards[West][Spades] = holding({3});
        return root;
    }
}  // namespace

// --- acceptance 1: size() matches hand-derived counts, several shapes -----

TEST(ConsistentLayoutSourceTest, SizeMatchesTheHandDerivedCountOnATenCardPool)
{
    ConsistentLayoutSource const source(make_ten_card_pool_root(), North, /*seed=*/1u);
    EXPECT_EQ(source.size(), 252u);  // C(10, 5)
}

TEST(ConsistentLayoutSourceTest, SizeMatchesTheHandDerivedCountOnAMidTrickEnding)
{
    // Pool is 5 diamonds (East 2, West 3, since East already played the
    // ace to the current trick) -- C(5, 2) = 10.
    ConsistentLayoutSource const source(make_mid_trick_root(), North, /*seed=*/1u);
    EXPECT_EQ(source.size(), 10u);
}

TEST(ConsistentLayoutSourceTest, SizeMatchesTheHandDerivedCountOnATwoCardEnding)
{
    // One spade each -- C(2, 1) = 2.
    ConsistentLayoutSource const source(make_small_single_suit_root(), North, /*seed=*/1u);
    EXPECT_EQ(source.size(), 2u);
}

// --- acceptance 3: at(i) is a bijection over the whole space --------------

TEST(ConsistentLayoutSourceTest, AtIsABijectionOverTheWholeSpaceOnTheTenCardPool)
{
    Deal const root = make_ten_card_pool_root();
    ConsistentLayoutSource const source(root, North, /*seed=*/1u);
    std::uint64_t const total = *source.size();
    ASSERT_EQ(total, 252u);

    std::set<std::uint64_t> keys;
    for (std::uint64_t index = 0; index < total; ++index)
    {
        Deal const layout = source.at(index);
        // Every layout is consistent with the root (decision 1's central
        // claim, re-checked here on the assembled type -- see task 06's
        // own background on why the composition is tested again).
        EXPECT_TRUE(is_consistent(layout, root, North, South)) << "index=" << index;
        keys.insert(layout_key(layout, East));
    }
    EXPECT_EQ(keys.size(), total);  // every index produced a distinct layout
}

// --- acceptance 2: every layout is a legal position, whole space ----------

TEST(ConsistentLayoutSourceTest, EveryLayoutOverTheWholeSpaceIsALegalPosition)
{
    Deal const root = make_ten_card_pool_root();
    ConsistentLayoutSource const source(root, North, /*seed=*/1u);
    std::uint64_t const total = *source.size();

    int const east_root_count = be::card_count(root, East);
    int const west_root_count = be::card_count(root, West);

    for (std::uint64_t index = 0; index < total; ++index)
    {
        Deal const layout = source.at(index);
        EXPECT_EQ(be::card_count(layout, East), east_root_count) << "index=" << index;
        EXPECT_EQ(be::card_count(layout, West), west_root_count) << "index=" << index;
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            EXPECT_EQ(layout.remainCards[East][suit] & layout.remainCards[West][suit], 0u)
                << "index=" << index << " suit=" << suit;
        }
    }
}

// --- acceptance 4: deterministic ---------------------------------------------

TEST(ConsistentLayoutSourceTest, AtIsDeterministicAcrossRepeatedCallsOnOneSource)
{
    ConsistentLayoutSource const source(make_ten_card_pool_root(), North, /*seed=*/7u);
    for (std::uint64_t index = 0; index < 20; ++index)
    {
        Deal const first = source.at(index);
        Deal const second = source.at(index);
        EXPECT_EQ(layout_key(first, East), layout_key(second, East)) << "index=" << index;
    }
}

TEST(ConsistentLayoutSourceTest, AtIsDeterministicAcrossTwoSeparatelyConstructedSourcesWithTheSameSeed)
{
    Deal const root = make_ten_card_pool_root();
    ConsistentLayoutSource const a(root, North, /*seed=*/7u);
    ConsistentLayoutSource const b(root, North, /*seed=*/7u);
    for (std::uint64_t index = 0; index < 20; ++index)
    {
        EXPECT_EQ(layout_key(a.at(index), East), layout_key(b.at(index), East)) << "index=" << index;
    }
}

// --- acceptance 5: different seeds give different orders, same set -------

TEST(ConsistentLayoutSourceTest, TwoSeedsGiveDifferentOrdersOfTheSameSetOfLayouts)
{
    Deal const root = make_ten_card_pool_root();
    ConsistentLayoutSource const a(root, North, /*seed=*/1u);
    ConsistentLayoutSource const b(root, North, /*seed=*/2u);
    std::uint64_t const total = *a.size();
    ASSERT_EQ(total, *b.size());

    std::vector<std::uint64_t> keys_a;
    std::vector<std::uint64_t> keys_b;
    for (std::uint64_t index = 0; index < total; ++index)
    {
        keys_a.push_back(layout_key(a.at(index), East));
        keys_b.push_back(layout_key(b.at(index), East));
    }
    EXPECT_NE(keys_a, keys_b);  // different order

    std::set<std::uint64_t> const set_a(keys_a.begin(), keys_a.end());
    std::set<std::uint64_t> const set_b(keys_b.begin(), keys_b.end());
    EXPECT_EQ(set_a, set_b);  // same set of layouts
}

// --- acceptance 6: out-of-range behaviour, stated and pinned --------------

TEST(ConsistentLayoutSourceTest, OutOfRangeIndexIsAnAssertedCallerError)
{
    ConsistentLayoutSource const source(make_small_single_suit_root(), North, /*seed=*/1u);
    ASSERT_EQ(source.size(), 2u);
    EXPECT_DEATH({ source.at(2); }, "");
}
