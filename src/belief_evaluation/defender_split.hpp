#pragma once

#include <cstdint>
#include <vector>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// The outstanding defender cards at a root layout, flattened into one
/// ordered sequence across all four suits, and how many of them the fixed
/// seat -- `(declarer + 1) % DDS_HANDS`, the same seat `BeliefNode::root_keys`
/// and `scan_for_replenishment` fix on -- holds there. This is the whole
/// input a defender-split enumeration is a function of: everything else
/// `is_consistent` compares (trump, `first`, the current trick, declarer's
/// and dummy's holdings) is fixed by the root, and the only freedom left is
/// which of these pooled cards each defender holds.
///
/// `cards` is every card either defender holds at the root: the union,
/// suit by suit, of both defenders' `remainCards` -- equivalent to
/// `node.cpp`'s own (unexported) `defender_pool()` called once per suit and
/// flattened into individual `Card`s, and verified against it directly in
/// this module's own tests.
struct DefenderPool
{
    /// Canonically ordered: suits ascending (0=S, 1=H, 2=D, 3=C, matching
    /// `Card::suit`'s own convention), and within a suit, ranks ascending
    /// (2 low .. 14 high -- the same direction `Deal::remainCards`' bit
    /// position already orders them in, bit *r* for absolute rank *r*, with
    /// no shift). This order is load-bearing: a caller mapping an index to
    /// a position in this sequence relies on two decompositions of the same
    /// root producing the same order every time, which is what makes
    /// `LayoutSource::at` deterministic once built on top of this. Do not
    /// change the direction once anything depends on it.
    std::vector<Card> cards;

    /// How many of `cards` the fixed seat holds at the root this was built
    /// from. Read directly from the root layout via `std::popcount` across
    /// its four `remainCards` entries, never derived as half of
    /// `cards.size()`: mid-trick, the two defenders' remaining counts can
    /// legitimately differ by one (one defender has already played to the
    /// trick in progress), and a symmetric assumption gets that case wrong
    /// while still producing a plausible-looking, non-empty result.
    int fixed_seat_count = 0;
};

/// Builds `root`'s `DefenderPool` for `declarer`. An empty pool, or a
/// `fixed_seat_count` of 0 or of the whole pool, are legal inputs -- both
/// describe a root with only one legal defender split -- and are answered
/// cleanly rather than asserted against.
auto defender_pool_decomposition(Deal const& root, int declarer) -> DefenderPool;

// --- pure combinatorics: no Deal, no DefenderPool, in sight ---------------
//
// The two functions below have nothing to do with bridge -- they are the
// general k-subset machinery a defender-split enumeration is built on top
// of. Kept in this file rather than a sibling because there is exactly one
// caller relationship between them and DefenderPool today and splitting
// them out would buy nothing; if a second, unrelated caller ever wants
// only these two, that is the point to move them, not before.

/// `C(n, k)`: the number of `k`-element subsets of an `n`-element set.
/// Exact for every `n` in `0..26` and `k` in `0..n` -- the domain this
/// module ever calls it in, since thirteen tricks means at most 26 cards
/// are ever outstanding between two defenders, and `C(26, 13) = 10,400,600`
/// is six orders of magnitude inside `std::uint64_t`. No overflow guard:
/// the domain excludes it, and a guard here would be dead code inviting
/// the wrong fix (widening the type) to a problem that cannot occur.
///
/// `k < 0` or `k > n` returns 0, the mathematically correct value, rather
/// than being treated as an error -- `unrank_combination`'s own loop relies
/// on this at its search boundary.
///
/// Backed by a `constexpr` Pascal's triangle computed once, not a
/// recurrence evaluated per call: `unrank_combination` calls this `O(n)`
/// times per index and is on a `LayoutSource::at()`'s hot path once one is
/// built on top of it, so a per-call recurrence would be measurable waste
/// for no simplicity gain.
auto binomial_coefficient(int n, int k) -> std::uint64_t;

/// Maps `index` in `[0, binomial_coefficient(n, k))` to the `k`-subset of
/// `{0, ..., n-1}` it names, returned as ascending indices into that
/// domain. A bijection: as `index` runs over the whole range, every
/// `k`-subset is named exactly once.
///
/// **Colexicographic (colex) order**, chosen and fixed here -- lex would
/// serve equally well as a bijection, but the two number the same subsets
/// differently, so switching later silently changes which subset every
/// existing index names. The construction: for each position from `k` down
/// to 1, find the largest remaining candidate `v` with
/// `binomial_coefficient(v, i) <= index`, take it, and subtract that many
/// combinations from `index` before moving to the next position. This is
/// the standard combinatorial-number-system unranking, not an invention.
///
/// `k == 0` returns the empty subset for the only valid index, 0
/// (`binomial_coefficient(n, 0) == 1`); `k == n` returns
/// `{0, ..., n-1}`, again the only valid index.
auto unrank_combination(std::uint64_t index, int n, int k) -> std::vector<int>;

}  // namespace dds::belief_evaluation
