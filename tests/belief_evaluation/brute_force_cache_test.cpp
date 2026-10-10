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

TEST_F(BruteForceCacheTest, EqualKeysHashEqually)
{
    // The minimum any hash function must uphold: operator== compares
    // the exact posterior (see EvenNearIdenticalPosteriorsNeverCompareEqual
    // just above), so hash_value() must hash that same exact value, not
    // a rounded approximation of it -- an earlier design quantised
    // posteriors before hashing, aiming to put "near-identical" values
    // in the same unordered_map bucket, but operator== would still
    // reject them as unequal once there, so that rounding bought
    // nothing it claimed to while actively hurting lookups elsewhere
    // (many small, genuinely distinct posteriors at deep nodes round to
    // the same quantised value and pile into one bucket).
    be::ObservationState const state = make_state(North);
    Deal const a = make_layout(North, be::holding({2}), be::holding({3}));

    be::BruteForceCacheKey const key_1 = be::make_brute_force_cache_key(state, {a}, {0.5});
    be::BruteForceCacheKey const key_2 = be::make_brute_force_cache_key(state, {a}, {0.5});

    EXPECT_EQ(key_1, key_2);
    EXPECT_EQ(be::hash_value(key_1), be::hash_value(key_2));
}

TEST_F(BruteForceCacheTest, NearIdenticalPosteriorsAreNotRequiredToHashToTheSameBucket)
{
    // The flip side of EqualKeysHashEqually: two merely-close-but-unequal
    // posteriors are *not* required to collide into the same bucket
    // (unlike the old, quantising design) -- hashing the exact bit
    // pattern, they generally will not, and that is correct: operator==
    // would reject them as unequal regardless, so sharing a bucket could
    // only ever cost a wasted comparison, never gain a hit.
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

TEST_F(BruteForceCacheTest, DifferentNonKeyedDefenderHoldingsProduceDifferentKeys)
{
    // deal_and_posterior packs *both* defenders' own holdings per
    // layout, not just defender_seat's (East, here) -- layout_key()'s
    // own doxygen is explicit that its packing is "unique only among
    // layouts that share a node's outstanding-card pool... not a
    // cross-node or global layout identity": within make_layout's own
    // fixed declarer/dummy holdings (Nine/Eight) and a fixed East
    // holding, West's own holding is exactly the rest of the pool for
    // *that one node* -- but two one-layout keys built independently,
    // as here, are not "one node", so West's own holding must be
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
    // ObservationState::first -- the seat that led this whole
    // evaluation's own root's first trick -- is fixed for the entire
    // recursion (advance_state, expand.cpp, never assigns it) and is not
    // one of the two fields the opponent-model restriction excludes
    // (only history/play_record are): a caller-supplied opponent model
    // may legitimately read and condition on it. key.first tracks a
    // different fact -- representative.first, the *current* trick's own
    // leader, which play() updates to the winner every time a trick
    // resolves -- so two evaluations whose roots began with a different
    // opening leader, but which later reach "the same" current-trick
    // shape, must not collide here.
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

    // Last write wins on a repeated key -- see BruteForceCache::insert's
    // own doxygen.
    cache.insert(key, 0.25);
    EXPECT_EQ(cache.size(), 1u);
    EXPECT_DOUBLE_EQ(*cache.find(key), 0.25);
}
