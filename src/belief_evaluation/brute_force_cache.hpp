#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// One (position, weighted belief space) pair that the search of
/// BruteForceDeclarer may meet more than once. Two keys are equal only
/// when the position and every (deal, posterior) pair agree.
///
/// Deal identity alone is not enough. Restricted choice often gives the
/// same set of deals with different weights, and those can have different
/// best cards. See the spec, `BruteForceDeclarer` under "Key entry
/// points".
///
/// **Omits `ObservationState::history` and `::play_record`.** So two
/// `play()` calls on one line of play can share a value. This is sound
/// only when the opponent model answers from the position alone. See
/// BruteForceDeclarer.
struct BruteForceCacheKey
{
    /// The position, apart from the defenders' cards: trump, declarer,
    /// tricks_needed, tricks_won_by_declarer, the current trick, and the
    /// exact holdings of declarer and dummy (from layout_key()).
    ///
    /// The holdings are needed: two subtrees can share trick counts and
    /// the current trick but hold different cards. The fields are compared
    /// exactly in operator==. Packing them into one 64-bit value could
    /// make two positions equal. hash_value() combines them, which is fine
    /// for a hash.
    int trump = 0;
    int declarer = 0;
    int tricks_needed = 0;
    int tricks_won_by_declarer = 0;

    /// The leader of the *current* trick (`Deal::first`). Not
    /// `ObservationState::first`: see `root_first`.
    int first = 0;
    int current_trick_suit[3] = {0, 0, 0};
    int current_trick_rank[3] = {0, 0, 0};

    /// `ObservationState::first`: the leader of the root's first trick,
    /// fixed for the whole recursion. An opponent model may read it, so
    /// two roots with different opening leaders are different nodes.
    int root_first = 0;
    std::uint64_t declarer_holding_key = 0;
    std::uint64_t dummy_holding_key = 0;

    /// One triple per surviving layout: the holding of defender_seat, the
    /// holding of the other defender, and the exact posterior. Sorted, so
    /// insertion order does not matter.
    ///
    /// Both defender holdings are needed. layout_key() is unique only
    /// within one outstanding pool, and the key does not record the pool.
    /// Without the second holding, two layouts that differ only in the
    /// other defender's cards would collide. See
    /// `DifferentNonKeyedDefenderHoldingsProduceDifferentKeys` in
    /// brute_force_cache_test.cpp.
    ///
    /// **The posterior is compared exactly, bit for bit.** Leaf values
    /// read every `node.p[i]`, so two close posteriors can give different
    /// values. Treating them as one key would return a wrong value.
    std::vector<std::tuple<std::uint64_t, std::uint64_t, Probability>> deal_and_posterior;

    auto operator==(BruteForceCacheKey const& other) const -> bool;
};

/// The hash for BruteForceCache. It hashes the exact bits of each
/// posterior, which keeps it consistent with operator==. Rounding would put
/// many distinct small posteriors in one bucket and slow every lookup.
auto hash_value(BruteForceCacheKey const& key) -> std::size_t;

/// Builds the key for one node. `state` and `layouts.front()` supply the
/// position fields. `layouts` and `p` are parallel.
auto make_brute_force_cache_key(
    ObservationState const& state, std::vector<Deal> const& layouts,
    std::vector<Probability> const& p) -> BruteForceCacheKey;

/// The transposition table of one BruteForceDeclarer, shared by all its
/// `play()` calls. With `BruteForceOptions::max_depth` set, each call
/// clears it first. Never shared between instances: a different
/// DeclarerObjective or opponent model gives wrong values. See the spec,
/// `BruteForceDeclarer` under "Key entry points".
///
/// Hits *between* two `play()` calls are rare. Each call's root is
/// renormalised to mass 1, but descendants are not. So the same position
/// usually has different posteriors, and a different key. Hits *within*
/// one call are not affected.
class BruteForceCache
{
public:
    auto find(BruteForceCacheKey const& key) const -> std::optional<double>;

    /// The last write wins on a repeated key. Two different values for one
    /// key would be a bug upstream.
    auto insert(BruteForceCacheKey const& key, double value) -> void;

    auto size() const -> std::size_t;

private:
    struct Hasher
    {
        auto operator()(BruteForceCacheKey const& key) const -> std::size_t
        {
            return hash_value(key);
        }
    };

    std::unordered_map<BruteForceCacheKey, double, Hasher> table_;
};

}  // namespace dds::belief_evaluation
