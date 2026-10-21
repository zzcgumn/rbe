#include <gtest/gtest.h>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/brute_force_cache.hpp>
#include <belief_evaluation/types.hpp>

#include "test_support.hpp"

// Tests for BruteForceCacheKey equality and hashing, and for
// BruteForceCache's find and insert.

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
    // operator== must stay exact. An exhaustive search reads every posterior
    // exactly, so two vectors that are close but not equal can give different
    // results. Treating them as one cache key would return the wrong value for
    // one of them.
    be::ObservationState const state = make_state(North);
    Deal const a = make_layout(North, be::holding({2}), be::holding({3}));

    be::BruteForceCacheKey const key_1 = be::make_brute_force_cache_key(state, {a}, {0.5});
    be::BruteForceCacheKey const key_2 = be::make_brute_force_cache_key(state, {a}, {0.5 + 1e-12});

    EXPECT_FALSE(key_1 == key_2);
}

TEST_F(BruteForceCacheTest, EqualKeysHashEqually)
{
    // The minimum any hash must uphold. operator== compares the exact
    // posterior (see EvenNearIdenticalPosteriorsNeverCompareEqual above), so
    // hash_value() must hash that exact value, not a rounded one. Rounding
    // would put close values in one bucket, where operator== still rejects
    // them. It would also pile many small, distinct posteriors at deep nodes
    // into one bucket.
    be::ObservationState const state = make_state(North);
    Deal const a = make_layout(North, be::holding({2}), be::holding({3}));

    be::BruteForceCacheKey const key_1 = be::make_brute_force_cache_key(state, {a}, {0.5});
    be::BruteForceCacheKey const key_2 = be::make_brute_force_cache_key(state, {a}, {0.5});

    EXPECT_EQ(key_1, key_2);
    EXPECT_EQ(be::hash_value(key_1), be::hash_value(key_2));
}

TEST_F(BruteForceCacheTest, NearIdenticalPosteriorsAreNotRequiredToHashToTheSameBucket)
{
    // The flip side of EqualKeysHashEqually: close but unequal posteriors
    // need not share a bucket. Hashing the exact bit pattern, they generally
    // will not. That is correct: operator== rejects them anyway, so a shared
    // bucket could only cost a wasted comparison, never gain a hit.
    be::ObservationState const state = make_state(North);
    Deal const a = make_layout(North, be::holding({2}), be::holding({3}));

    be::BruteForceCacheKey const key_1 = be::make_brute_force_cache_key(state, {a}, {0.5});
    be::BruteForceCacheKey const key_2 = be::make_brute_force_cache_key(state, {a}, {0.5 + 1e-12});

    EXPECT_NE(be::hash_value(key_1), be::hash_value(key_2));
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
    // Same trump, tricks_needed, tricks_won_by_declarer and current trick
    // (all default or empty), but declarer's remainCards differ. layout_key()
    // alone, applied only to the defender seat, would miss this.
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

TEST_F(BruteForceCacheTest, DifferentNonKeyedDefenderHoldingsProduceDifferentKeys)
{
    // deal_and_posterior packs both defenders' holdings per layout, not just
    // defender_seat's (East, here). layout_key() is unique only among layouts
    // that share one node's outstanding pool. Within one node, West's holding
    // is the rest of the pool once East's is fixed. Two one-layout keys built
    // separately, as here, are not one node, so West's holding must be
    // compared directly, not inferred by complement.
    be::ObservationState const state = make_state(North);
    Deal const a = make_layout(North, be::holding({2}), be::holding({3}));  // West holds Three
    Deal const b = make_layout(North, be::holding({2}), be::holding({4}));  // West holds Four instead

    be::BruteForceCacheKey const key_1 = be::make_brute_force_cache_key(state, {a}, {1.0});
    be::BruteForceCacheKey const key_2 = be::make_brute_force_cache_key(state, {b}, {1.0});

    EXPECT_FALSE(key_1 == key_2);
}

TEST_F(BruteForceCacheTest, DifferentRootFirstProducesDifferentKeys)
{
    // ObservationState::first, the seat that led the root's first trick, is
    // fixed for the whole recursion (advance_state in expand.cpp never assigns
    // it). The opponent-model restriction excludes only history and
    // play_record, so an opponent model may read first. key.first is a
    // different fact: the current trick's leader, which play() updates after
    // each trick. So two evaluations whose roots had different opening leaders
    // must not collide, even when they reach the same current-trick shape.
    be::ObservationState state_a = make_state(North);
    state_a.first = North;
    be::ObservationState state_b = make_state(North);
    state_b.first = East;

    Deal const a = make_layout(North, be::holding({2}), be::holding({3}));

    be::BruteForceCacheKey const key_1 = be::make_brute_force_cache_key(state_a, {a}, {1.0});
    be::BruteForceCacheKey const key_2 = be::make_brute_force_cache_key(state_b, {a}, {1.0});

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

    // The last write wins on a repeated key (see BruteForceCache::insert).
    cache.insert(key, 0.25);
    EXPECT_EQ(cache.size(), 1u);
    EXPECT_DOUBLE_EQ(*cache.find(key), 0.25);
}
