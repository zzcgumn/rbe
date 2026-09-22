#pragma once

#include <cstdint>
#include <vector>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// The outstanding defender cards at a root layout, flattened into one
/// ordered sequence across all four suits, and how many of them the fixed
/// seat holds there. This is the whole input a defender-split enumeration
/// is a function of: everything else `is_consistent` compares is fixed by
/// the root, and the only freedom left is which of these cards each
/// defender holds.
struct DefenderPool
{
    /// Every card either defender holds at the root — the union, suit by
    /// suit, of both defenders' `remainCards`.
    ///
    /// Canonically ordered: suits ascending (0=S, 1=H, 2=D, 3=C), then
    /// ranks ascending within a suit. **Load-bearing** — see
    /// docs/module_map.md, "Enumeration order is load-bearing". Do not
    /// change the direction once anything depends on it.
    std::vector<Card> cards;

    /// How many of `cards` the fixed seat holds at the root. Read directly
    /// from the root layout, never derived as half of `cards.size()`:
    /// mid-trick the two defenders' counts legitimately differ by one, and
    /// a symmetric assumption gets that case wrong while still producing a
    /// plausible-looking result.
    int fixed_seat_count = 0;
};

/// Builds `root`'s `DefenderPool` for `declarer`. An empty pool, or a
/// `fixed_seat_count` of 0 or of the whole pool, are legal inputs — each
/// describes a root with only one legal split — and are answered cleanly
/// rather than asserted against.
auto defender_pool_decomposition(Deal const& root, int declarer) -> DefenderPool;

// --- pure combinatorics: no Deal, no DefenderPool, in sight ---------------
//
// The two functions below have nothing to do with bridge. They live here
// rather than in a sibling file because DefenderPool is their only caller
// today; a second, unrelated caller is the point to move them.

/// `C(n, k)`: the number of `k`-element subsets of an `n`-element set.
/// Exact for every `n` in `0..26` and `k` in `0..n` — the whole domain this
/// module calls it in, since at most 26 cards are ever outstanding between
/// two defenders, and `C(26, 13)` is six orders of magnitude inside
/// `std::uint64_t`. No overflow guard: the domain excludes overflow, and a
/// guard would invite widening the type to fix a problem that cannot occur.
///
/// `k < 0` or `k > n` returns 0, the mathematically correct value —
/// `unrank_combination`'s loop relies on it at its search boundary.
///
/// `n` outside `[0, 26]` is a caller error, not a defined zero: the backing
/// table is sized to exactly that range. Asserted, and returns 0 rather
/// than reading past the end where the assert is compiled away.
///
/// Backed by a `constexpr` Pascal's triangle, not a per-call recurrence:
/// `unrank_combination` calls this `O(n)` times per index, on a
/// `LayoutSource::at()` hot path.
auto binomial_coefficient(int n, int k) -> std::uint64_t;

/// Maps `index` in `[0, binomial_coefficient(n, k))` to the `k`-subset of
/// `{0, ..., n-1}` it names, as ascending indices. A bijection: every
/// `k`-subset is named exactly once.
///
/// **Colexicographic order**, fixed here. Lex would serve equally well as a
/// bijection, but the two number the same subsets differently, so switching
/// later silently changes which subset every existing index names. The
/// construction is the standard combinatorial number system: for each
/// position from `k` down to 1, take the largest remaining `v` with
/// `binomial_coefficient(v, i) <= index`, and subtract.
///
/// `k == 0` returns the empty subset and `k == n` the whole domain, each
/// for its only valid index, 0.
auto unrank_combination(std::uint64_t index, int n, int k) -> std::vector<int>;

// --- applying a split back onto a Deal -------------------------------------

/// Applies `fixed_seat_cards` — indices into `pool.cards`, naming what the
/// fixed seat holds — to `root`, returning the resulting `Deal`. Unnamed
/// pooled cards go to the other defender, with the complement derived from
/// `pool` rather than from `root`'s other-defender holding, so a bug in the
/// pool surfaces here rather than being masked.
///
/// Built by copy-and-overwrite: only the two defenders' `remainCards` are
/// replaced, so trump, `first`, both `currentTrick*` arrays and declarer's
/// and dummy's holdings stay byte-identical — including across a future
/// field `Deal` gains, which field-by-field reconstruction would drop.
///
/// Trusts its caller for legality: it does not check `fixed_seat_cards`
/// against `pool.fixed_seat_count`, since a caller unranking with
/// `k = pool.fixed_seat_count` can only ever produce the right size. Each
/// individual index *is* asserted in range, and ignored rather than
/// trusted where the assert is compiled away, so a bad one is never an
/// out-of-bounds write.
auto apply_defender_split(
    Deal const& root, int declarer, DefenderPool const& pool, std::vector<int> const& fixed_seat_cards) -> Deal;

}  // namespace dds::belief_evaluation
