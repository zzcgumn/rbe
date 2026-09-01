#pragma once

#include <vector>

#include <belief_evaluation/dds_types.hpp>
#include <belief_evaluation/defender_strategy.hpp>

/// Which set of cards a DoubleDummyDefender spreads probability over, given
/// a solved position's `FutureTricks`. The two differ in more than
/// behaviour -- they differ in what *licenses* the resulting uniform
/// distribution, and a caller choosing between them is choosing which
/// justification applies to their defender.
enum class SpreadPolicy
{
    /// Spread uniformly over the touching-card group of the single
    /// canonical best card (the highest `score`, dds's own tie-break
    /// order): `{rank[k]} u bits(equals[k])` for that one `k`.
    ///
    /// The cards in one touching-card group are literally interchangeable
    /// given the layout -- playing the king or the queen from a held KQ
    /// leaves positions that are isomorphic under the renumbering
    /// isomorphism (a strictly order-preserving bijection within the suit,
    /// under which every rule of trick-taking is preserved). The uniform
    /// distribution over that group is therefore not a modelling guess but
    /// the canonical distribution over an equivalence class the theory
    /// already licenses, and the elementary weight-based (`W_{pi,delta}`)
    /// derivation in `docs/replenished_belief_evaluation/algorithm.md`
    /// covers it unchanged. This is what restricted choice actually is.
    TouchingSequence,

    /// Spread uniformly over the union of every candidate's touching-card
    /// group, for every candidate tied on the maximum score, across suits.
    ///
    /// These cards are equally *good* but not otherwise equivalent -- the
    /// resulting positions are not isomorphic, and the distribution's shape
    /// depends on how many suits happen to tie. This is the case
    /// `algorithm.md` warns about when it says a heuristic defender is "not
    /// guaranteed to stay within the requirements for the `W_{pi,delta}`
    /// based formulation": the more general `Omega = S x B` construction is
    /// what covers it, not the elementary derivation. A caller selecting
    /// this policy has moved to that advanced justification.
    AllOptimal,
};

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
