#pragma once

#include <vector>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/defender_strategy.hpp>

namespace dds::belief_evaluation
{

/// Which set of cards a DoubleDummyDefender spreads probability over, given
/// a solved position's `FutureTricks`. The two differ in more than
/// behaviour: they differ in what *licenses* the resulting uniform
/// distribution, so choosing between them is choosing which justification
/// applies to your defender.
enum class SpreadPolicy
{
    /// Spread uniformly over the touching-card group of the single
    /// canonical best card (the highest `score`, dds's own tie-break
    /// order): `{rank[k]} u bits(equals[k])` for that one `k`.
    ///
    /// Cards in one touching group are interchangeable given the layout:
    /// king or queen from a held KQ leaves isomorphic positions under the
    /// renumbering bijection. The uniform distribution over the group is
    /// therefore the canonical distribution over an equivalence class the
    /// theory already licenses, not a modelling guess, and algorithm.md's
    /// elementary `W_{pi,delta}` derivation covers it unchanged. This is
    /// what restricted choice actually is.
    TouchingSequence,

    /// Spread uniformly over the union of every candidate's touching-card
    /// group, for every candidate tied on the maximum score, across suits.
    ///
    /// These cards are equally *good* but not otherwise equivalent: the
    /// resulting positions are not isomorphic, and the distribution's shape
    /// depends on how many suits happen to tie. This is what algorithm.md
    /// warns about in saying a heuristic defender is "not guaranteed to
    /// stay within the requirements for the `W_{pi,delta}` based
    /// formulation"; the more general `Omega = S x B` construction is what
    /// covers it. Selecting this policy moves to that justification.
    AllOptimal,
};

/// The index `spread()`'s own `SpreadPolicy::TouchingSequence` branch
/// treats as the single canonical entry: the highest `fut.score`,
/// breaking ties by keeping the first entry reached (dds's own
/// ordering). Exported so a caller choosing whether to defer to
/// `spread(fut, SpreadPolicy::TouchingSequence)` can check in advance
/// whether doing so actually reaches the group it means -- `fut` may
/// hold more than one disjoint touching group tied for best score, and
/// this is the one `spread()` itself will pick, not necessarily the one
/// any particular caller has in mind. See `second_seat_low`'s own
/// doxygen for the rule this matters to.
auto canonical_best(FutureTricks const& fut) -> int;

/// The candidate cards `policy` spreads probability over, given a solved
/// position's `fut`, each with equal probability (`1 / candidate count`).
/// `fut.suit`/`fut.rank` name each candidate entry's card; `fut.equals`
/// (already in `Deal`'s own bit convention -- see `to_compacted` in
/// `trick.hpp` for the other, compacted, convention this is *not*) names
/// the other cards in that entry's touching sequence, excluding the entry's
/// own card. Deduplicates: two `fut` entries in the same sequence (as dds
/// may report when asked for every candidate's rank) must not double the
/// weight of the cards they share.
///
/// Pure and solver-free: takes an already-solved `FutureTricks`, no
/// `solve_board` call. `fut.cards == 0` returns an empty distribution.
auto spread(FutureTricks const& fut, SpreadPolicy policy) -> std::vector<WeightedCard>;

}  // namespace dds::belief_evaluation
