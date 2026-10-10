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

/// Identifies one (position, weighted-belief-space) pair BruteForceDeclarer's
/// own internal search may be asked to evaluate more than once. Two keys
/// compare equal only when both the position and every (deal, posterior)
/// pair agree -- deal identity alone is not enough. A BeliefView's own
/// posteriors are not uniform in general (restricted choice routinely
/// produces the same surviving deal *set* with different relative weights,
/// reached by different real play), and two such belief spaces can have
/// genuinely different best cards -- so a key built from deal identity alone
/// would silently conflate them. See `specs/replenished-belief-evaluation.md`'s
/// own `BruteForceDeclarer` entry under "Key entry points" for the fuller
/// correctness rationale.
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

    /// One (defender_seat's own holding, the *other* defender's own
    /// holding, exact posterior) triple per surviving layout, sorted so
    /// that two equal sets built in different insertion order produce an
    /// identical key (order independence). Both defenders' holdings are
    /// required, not just defender_seat's: layout_key()'s own doxygen is
    /// explicit that its packing is "unique only among layouts that
    /// share a node's outstanding-card pool... not a cross-node or
    /// global layout identity" -- within one node, defender_seat's own
    /// holding and declarer's/dummy's (above) do determine the other
    /// defender's by complement, but that complement relies on *this
    /// node's own* outstanding pool, which is nowhere else recorded in
    /// this key. Two unrelated searches (or two unrelated layouts handed
    /// to this same function independently) can agree on declarer's,
    /// dummy's and defender_seat's own holdings while the other
    /// defender's own cards genuinely differ -- reachable in practice
    /// whenever that other defender had an earlier, equally-legal choice
    /// of which card to part with -- and without this field, those are
    /// silently treated as the same node. See
    /// `DifferentNonKeyedDefenderHoldingsProduceDifferentKeys` in
    /// brute_force_cache_test.cpp.
    ///
    /// The posterior is stored and compared *exactly* -- `std::vector`'s
    /// elementwise `==` on this vector therefore requires bit-identical
    /// doubles, never a quantised approximation. An exhaustive search's
    /// own leaf values (terminal_leaf_value, cutoff_leaf_value) read
    /// every `node.p[i]` exactly, so two posterior vectors that are
    /// merely *close* -- not equal -- can legitimately produce different
    /// results; treating them as the same cache key would return the
    /// wrong value for one of them, which is a correctness bug a cache
    /// must never cause, not a precision trade-off it is allowed to
    /// make. hash_value() below is free to quantise when combining this
    /// same field into one hash value, because a hash collision only
    /// costs a slower lookup (operator== still disambiguates), while an
    /// equality collision would silently hand back the wrong cached
    /// value.
    std::vector<std::tuple<std::uint64_t, std::uint64_t, Probability>> deal_and_posterior;

    auto operator==(BruteForceCacheKey const& other) const -> bool;
};

/// For BruteForceCache's own unordered_map. May -- and does -- treat two
/// keys that are merely close as the same bucket (via quantize_posterior()
/// below): operator== still disambiguates them once there, so a hash
/// collision here only costs a slower lookup, never a wrong answer.
auto hash_value(BruteForceCacheKey const& key) -> std::size_t;

/// Rounds `posterior` to a fixed relative precision (1e-9), for
/// hash_value()'s own use only -- never for BruteForceCacheKey's
/// operator==, which compares the exact posterior (see that field's own
/// doxygen for why). Close enough that two structurally-identical
/// derivations of the same weight (floating-point noise from a different
/// evaluation order, say) hash to the same bucket, far enough apart that
/// two meaningfully different weightings (0.6 vs. 0.61, say) essentially
/// never collide.
auto quantize_posterior(Probability posterior) -> std::int64_t;

/// Builds the key for one node: see BruteForceCacheKey's own doxygen.
/// `state` and `layouts.front()` together supply every position field;
/// `layouts`/`p` are parallel, as everywhere else in this module.
auto make_brute_force_cache_key(
    ObservationState const& state, std::vector<Deal> const& layouts,
    std::vector<Probability> const& p) -> BruteForceCacheKey;

/// The instance-owned transposition table BruteForceDeclarer's own
/// internal search shares across every `play()` call made on that one
/// `BruteForceDeclarer` instance -- except when `BruteForceOptions::max_depth`
/// is set, where `as_strategy()`'s own `play` lambda clears it at the start
/// of each call instead, since a depth cutoff depends on call-relative
/// recursion depth, not on anything inherent to the position (see that
/// lambda's own `.cpp` comment). Never shared across two
/// differently-configured instances either way -- a different
/// DeclarerObjective or a different opponent model corrupts backed-up
/// values the same way mixing search objectives would in the
/// BridgeLibraries original this ports -- see
/// `specs/replenished-belief-evaluation.md`'s own `BruteForceDeclarer`
/// entry under "Key entry points" for "DeclarerObjective and what is
/// locked per instance".
///
/// Sharing across calls is sound -- a cache hit never returns a wrong
/// value -- but a hit *across two different real `play()` calls* is rare
/// in practice, not the common case this sharing might suggest.
/// `make_belief_view` (belief_view.cpp) renormalises every fresh call's
/// own root so its `p` sums to 1, but this search's own internal
/// recursion never renormalises a descendant (`expand.cpp`'s own
/// `p_i' = p_i * delta(...)`, nothing else) -- so a node several plies
/// into one call's own internal exploration generally carries a mass
/// below 1 relative to *that* call's root, while a later call reaching
/// "the same" position afresh always starts from a renormalised 1 again.
/// `BruteForceCacheKey` compares the exact posterior (by design -- see
/// its own doxygen), so these two differently-scaled views of the same
/// position produce different keys and simply miss each other. A hit
/// across calls only survives when every choice on the path held
/// probability exactly 1 on both sides (a forced sequence, or a single-
/// layout belief with a deterministic opponent model) -- precisely the
/// cases a genuinely uncertain, multi-layout belief space (the
/// restricted-choice case this cache exists to help) does not produce.
/// Reuse *within* one call's own internal recursion (a transposition
/// reached by two different move orders inside the same `play()` call)
/// is unaffected by any of this, since every node there is scaled
/// relative to the same, single root throughout.
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
