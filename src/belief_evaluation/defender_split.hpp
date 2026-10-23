#pragma once

#include <cstdint>
#include <vector>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// The defenders' cards at a root layout, as one ordered list over all
/// four suits, and how many the fixed seat holds. Everything else that
/// `is_consistent` compares is fixed by the root. So the only freedom is
/// which of these cards each defender holds.
///
/// **The fixed seat is `(declarer + 1) % DDS_HANDS`**, here and wherever
/// the module identifies a layout by one defender's holding. See
/// docs/module_map.md, convention 6.
struct DefenderPool
{
    /// Every card either defender holds at the root.
    ///
    /// Ordered by suit ascending (0=S, 1=H, 2=D, 3=C), then rank ascending.
    /// **This order must not change.** See docs/module_map.md,
    /// "Enumeration order is load-bearing".
    std::vector<Card> cards;

    /// How many of `cards` the fixed seat holds, read from the root.
    /// **Never `cards.size() / 2`:** mid-trick the two defenders' counts
    /// differ by one, and the result would look plausible but be wrong.
    int fixed_seat_count = 0;
};

/// Builds the `DefenderPool` of `root` for `declarer`. An empty pool, or a
/// `fixed_seat_count` of 0 or of the whole pool, is legal: the root has one
/// split.
auto defender_pool_decomposition(Deal const& root, int declarer) -> DefenderPool;

// --- pure combinatorics: no Deal, no DefenderPool, in sight ---------------
//
// These two functions have nothing to do with bridge. DefenderPool is
// their only caller.

/// `C(n, k)`: the number of `k`-element subsets of an `n`-element set.
/// Exact for `n` in `0..26` and `k` in `0..n`. At most 26 cards are
/// outstanding between two defenders, so this domain never overflows
/// `std::uint64_t`.
///
/// `k < 0` or `k > n` returns 0, the correct value.
/// `unrank_combination` relies on it.
///
/// `n` outside `[0, 26]` is a caller error. Asserted, and returns 0
/// rather than reading past the table.
///
/// Uses a `constexpr` Pascal's triangle, because it is on the hot path of
/// `LayoutSource::at()`.
auto binomial_coefficient(int n, int k) -> std::uint64_t;

/// Maps `index` in `[0, binomial_coefficient(n, k))` to the `k`-subset of
/// `{0, ..., n-1}` it names, as ascending indices. Every `k`-subset is
/// named exactly once.
///
/// **Colexicographic order. Do not change it:** that would change which
/// subset every index names. For each position `i` from `k` down to 1,
/// take the largest remaining `v` with `binomial_coefficient(v, i) <=
/// index`, and subtract.
///
/// `k == 0` gives the empty subset and `k == n` the whole domain, each for
/// its only valid index, 0.
auto unrank_combination(std::uint64_t index, int n, int k) -> std::vector<int>;

// --- applying a split back onto a Deal -------------------------------------

/// Returns `root` with the defenders' cards replaced. `fixed_seat_cards`
/// are indices into `pool.cards` held by the fixed seat
/// (`(declarer + 1) % DDS_HANDS`). The rest of the pool goes to the other
/// defender. That complement comes from `pool`, not from `root`, so a bug
/// in the pool shows here.
///
/// Only the two defenders' `remainCards` change. Every other field of
/// `root` is copied unchanged.
///
/// Does not check the size of `fixed_seat_cards` against
/// `pool.fixed_seat_count`. Each index is asserted in range, and an
/// out-of-range index is ignored when asserts are off.
auto apply_defender_split(
    Deal const& root, int declarer, DefenderPool const& pool, std::vector<int> const& fixed_seat_cards) -> Deal;

}  // namespace dds::belief_evaluation
