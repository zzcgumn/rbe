#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// Identifies one (position, weighted-belief-space) pair BruteForceDeclarer's
/// own internal search may be asked to evaluate more than once. Two keys
/// compare equal only when both the position and every (deal, posterior)
/// pair agree -- deal identity alone is not enough. A BeliefView's own
/// posteriors are not uniform in general (restricted choice routinely
/// produces the same surviving deal *set* with different relative weights,
/// reached by different real play), and two such belief spaces can have
/// genuinely different best cards -- so a key built from deal identity alone
/// would silently conflate them. See brute_force_strategy.md's own
/// "Correctness" section.
///
/// Deliberately omits `ObservationState::history` and `::play_record` --
/// this is what lets two different real `play()` calls along one
/// actually-played line, which routinely differ in both, still share a
/// cached value when every field below agrees. That is only sound when the
/// opponent model answers from position alone; see
/// BruteForceDeclarer's own class doxygen for the caller precondition this
/// omission depends on.
struct BruteForceCacheKey
{
    /// Everything that describes "this position" independent of which
    /// defender holds what: trump, declarer's seat, tricks_needed,
    /// tricks_won_by_declarer, the current-trick-in-progress shape, and --
    /// not redundant with the per-layout entries below -- declarer's and
    /// dummy's own exact holdings (packed exactly, via layout_key(), not
    /// hashed -- see that function's own "exact identity" doxygen). Two
    /// different subtrees of this search can share a trick count and
    /// current-trick shape while holding different residual declarer/dummy
    /// cards; those are different positions, not transpositions, and must
    /// not collide here. Stored as exact fields, compared exactly in
    /// operator==, rather than combined into one hashed scalar: combining
    /// two 52-bit layout_key() values plus the smaller fields into a single
    /// 64-bit integer cannot be injective, so two genuinely different
    /// positions could share a combined value and `operator==` would then
    /// wrongly treat them as the same node -- the one thing a cache key's
    /// own equality must never do. hash_value() below still combines all of
    /// these (lossily, which is fine for a hash) for the unordered_map's
    /// own bucketing; only equality needs the exact fields.
    int trump = 0;
    int declarer = 0;
    int tricks_needed = 0;
    int tricks_won_by_declarer = 0;
    int first = 0;
    int current_trick_suit[3] = {0, 0, 0};
    int current_trick_rank[3] = {0, 0, 0};
    std::uint64_t declarer_holding_key = 0;
    std::uint64_t dummy_holding_key = 0;

    /// One (deal hash, quantised posterior) pair per surviving layout,
    /// sorted by deal hash so that two equal sets built in different
    /// insertion order produce an identical key (order independence). Each
    /// deal hash is layout_key()'s own exact packing too, not a lossy hash,
    /// so this vector's own equality (std::vector's elementwise ==) is
    /// exact as well.
    std::vector<std::pair<std::uint64_t, std::int64_t>> deal_and_quantized_posterior;

    auto operator==(BruteForceCacheKey const& other) const -> bool;
};

/// For BruteForceCache's own unordered_map.
auto hash_value(BruteForceCacheKey const& key) -> std::size_t;

/// Rounds `posterior` to a fixed relative precision (1e-9) before packing
/// it for use in a cache key: close enough that two structurally-identical
/// derivations of the same weight (floating-point noise from a different
/// evaluation order, say) always produce the same quantised value, far
/// enough apart that two meaningfully different weightings (0.6 vs. 0.61,
/// say) never collapse to the same one.
auto quantize_posterior(Probability posterior) -> std::int64_t;

/// Builds the key for one node: see BruteForceCacheKey's own doxygen.
/// `state` and `layouts.front()` together supply every field
/// `position_hash` reads; `layouts`/`p` are parallel, as everywhere else in
/// this module.
auto make_brute_force_cache_key(
    ObservationState const& state, std::vector<Deal> const& layouts,
    std::vector<Probability> const& p) -> BruteForceCacheKey;

/// The externally-owned transposition table BruteForceDeclarer's own
/// internal search shares across every evaluate() call that one strategy
/// instance is used in. Never shared across two differently-configured
/// instances (a different DeclarerObjective or a different opponent model
/// corrupts backed-up values the same way mixing search objectives would
/// in the BridgeLibraries original this ports -- see
/// brute_force_strategy.md's own "DeclarerObjective and what is locked per
/// instance").
class BruteForceCache
{
public:
    auto find(BruteForceCacheKey const& key) const -> std::optional<double>;

    /// Last write wins on a repeated key -- this cache has no notion of two
    /// callers disagreeing about one key's value being anything other than
    /// a bug upstream, so there is nothing to reconcile here.
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
