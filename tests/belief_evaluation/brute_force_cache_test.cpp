#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/brute_force_cache.hpp>
#include <belief_evaluation/types.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;

namespace
{
    constexpr int North = 0;
    constexpr int East = 1;
    constexpr int South = 2;
    constexpr int West = 3;

    constexpr int Spades = 0;

    auto make_state(int declarer, int tricks_won = 0) -> be::ObservationState
    {
        be::ObservationState state{};
        state.trump = DDS_NOTRUMP;
        state.declarer = declarer;
        state.tricks_needed = 1;
        state.tricks_won_by_declarer = tricks_won;
        return state;
    }

    auto make_layout(int declarer, unsigned east_spades, unsigned west_spades) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = declarer;
        int const dummy = (declarer + 2) % DDS_HANDS;
        deal.remainCards[declarer][Spades] = be::holding({9});
        deal.remainCards[dummy][Spades] = be::holding({8});
        deal.remainCards[East][Spades] = east_spades;
        deal.remainCards[West][Spades] = west_spades;
        return deal;
    }
}

class BruteForceCacheTest : public ::testing::Test
{
};

TEST_F(BruteForceCacheTest, SameDealsSamePosteriorsProduceEqualKeysRegardlessOfOrder)
{
    be::ObservationState const state = make_state(North);
    Deal const a = make_layout(North, be::holding({2}), be::holding({3}));
    Deal const b = make_layout(North, be::holding({3}), be::holding({2}));

    be::BruteForceCacheKey const key_1 =
        be::make_brute_force_cache_key(state, {a, b}, {0.4, 0.6});
    be::BruteForceCacheKey const key_2 =
        be::make_brute_force_cache_key(state, {b, a}, {0.6, 0.4});

    EXPECT_EQ(key_1, key_2);
}

TEST_F(BruteForceCacheTest, SameDealsDifferentPosteriorsProduceDifferentKeys)
{
    be::ObservationState const state = make_state(North);
    Deal const a = make_layout(North, be::holding({2}), be::holding({3}));
    Deal const b = make_layout(North, be::holding({3}), be::holding({2}));

    be::BruteForceCacheKey const key_1 =
        be::make_brute_force_cache_key(state, {a, b}, {0.4, 0.6});
    be::BruteForceCacheKey const key_2 =
        be::make_brute_force_cache_key(state, {a, b}, {0.2, 0.8});

    EXPECT_FALSE(key_1 == key_2);
}

TEST_F(BruteForceCacheTest, EvenNearIdenticalPosteriorsNeverCompareEqual)
{
    // operator== must stay exact: an exhaustive search's own leaf values
    // read every posterior exactly, so two vectors that are merely close
    // -- not equal -- can legitimately produce different results.
    // Treating them as the same cache key would hand back the wrong
    // value for one of them.
    be::ObservationState const state = make_state(North);
    Deal const a = make_layout(North, be::holding({2}), be::holding({3}));

    be::BruteForceCacheKey const key_1 = be::make_brute_force_cache_key(state, {a}, {0.5});
    be::BruteForceCacheKey const key_2 = be::make_brute_force_cache_key(state, {a}, {0.5 + 1e-12});

    EXPECT_FALSE(key_1 == key_2);
}

TEST_F(BruteForceCacheTest, NearIdenticalFloatingPosteriorsStillHashToTheSameBucket)
{
    // hash_value() is allowed to quantise where operator== may not: a
    // hash collision only costs a slower lookup (operator== still
    // disambiguates), so two structurally-identical derivations of the
    // same weight (floating-point noise from a different evaluation
    // order, say) landing in the same bucket is a performance property,
    // not a correctness one.
    be::ObservationState const state = make_state(North);
    Deal const a = make_layout(North, be::holding({2}), be::holding({3}));

    be::BruteForceCacheKey const key_1 = be::make_brute_force_cache_key(state, {a}, {0.5});
    be::BruteForceCacheKey const key_2 = be::make_brute_force_cache_key(state, {a}, {0.5 + 1e-12});

    EXPECT_EQ(be::hash_value(key_1), be::hash_value(key_2));
}

TEST_F(BruteForceCacheTest, MeaningfullyDifferentPosteriorsProduceDifferentKeys)
{
    be::ObservationState const state = make_state(North);
    Deal const a = make_layout(North, be::holding({2}), be::holding({3}));

    be::BruteForceCacheKey const key_1 = be::make_brute_force_cache_key(state, {a}, {0.5});
    be::BruteForceCacheKey const key_2 = be::make_brute_force_cache_key(state, {a}, {0.5 + 1e-3});

    EXPECT_FALSE(key_1 == key_2);
}

TEST_F(BruteForceCacheTest, DifferentPositionFieldsProduceDifferentKeys)
{
    Deal const a = make_layout(North, be::holding({2}), be::holding({3}));

    be::BruteForceCacheKey const key_1 =
        be::make_brute_force_cache_key(make_state(North, /*tricks_won=*/0), {a}, {1.0});
    be::BruteForceCacheKey const key_2 =
        be::make_brute_force_cache_key(make_state(North, /*tricks_won=*/1), {a}, {1.0});

    EXPECT_FALSE(key_1 == key_2);
}

TEST_F(BruteForceCacheTest, DifferentDeclarerHoldingsAtTheSamePlyProduceDifferentKeys)
{
    // Same trump/tricks_needed/tricks_won_by_declarer/current-trick shape
    // (both default/empty), but declarer's own remainCards differ --
    // regression for the fix documented in this plan's own README
    // ("correction 6"): layout_key() alone, applied only to the defender
    // seat, would miss this entirely.
    be::ObservationState const state = make_state(North);

    Deal a{};
    a.trump = DDS_NOTRUMP;
    a.first = North;
    a.remainCards[North][Spades] = be::holding({9});
    a.remainCards[South][Spades] = be::holding({8});
    a.remainCards[East][Spades] = be::holding({2});
    a.remainCards[West][Spades] = be::holding({3});

    Deal b = a;
    b.remainCards[North][Spades] = be::holding({7});  // declarer holds a different card
    b.remainCards[South][Spades] = be::holding({8});
    b.remainCards[East][Spades] = be::holding({2});
    b.remainCards[West][Spades] = be::holding({3});

    be::BruteForceCacheKey const key_1 = be::make_brute_force_cache_key(state, {a}, {1.0});
    be::BruteForceCacheKey const key_2 = be::make_brute_force_cache_key(state, {b}, {1.0});

    EXPECT_FALSE(key_1 == key_2);
}

TEST_F(BruteForceCacheTest, RoundTripsThroughFindAndInsert)
{
    be::BruteForceCache cache;
    be::BruteForceCacheKey const key =
        be::make_brute_force_cache_key(make_state(North), {make_layout(North, be::holding({2}), be::holding({3}))}, {1.0});

    EXPECT_EQ(cache.find(key), std::nullopt);
    EXPECT_EQ(cache.size(), 0u);

    cache.insert(key, 0.75);
    EXPECT_EQ(cache.size(), 1u);
    ASSERT_TRUE(cache.find(key).has_value());
    EXPECT_DOUBLE_EQ(*cache.find(key), 0.75);

    // Last write wins on a repeated key -- see BruteForceCache::insert's
    // own doxygen.
    cache.insert(key, 0.25);
    EXPECT_EQ(cache.size(), 1u);
    EXPECT_DOUBLE_EQ(*cache.find(key), 0.25);
}
