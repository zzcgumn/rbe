#pragma once

#include <vector>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/defender_strategy.hpp>

namespace dds::belief_evaluation
{

/// The set of cards a DoubleDummyDefender spreads probability over, given
/// the `FutureTricks` of a solved position. The two policies rest on
/// different theory, so choose the one whose justification fits your
/// defender.
enum class SpreadPolicy
{
    /// Uniform over the touching cards of the one canonical best card
    /// (highest `score`, dds tie-break order): `{rank[k]} u bits(equals[k])`
    /// for that `k`.
    ///
    /// Touching cards are interchangeable: king or queen from KQ leaves
    /// isomorphic positions. So the uniform spread is the canonical
    /// distribution over an equivalence class, not a modelling guess. The
    /// `W_{pi,delta}` derivation in algorithm.md covers it. This is
    /// restricted choice.
    TouchingSequence,

    /// Uniform over the touching cards of every candidate tied on the best
    /// score, in any suit.
    ///
    /// These cards are equally *good* but not equivalent. The positions are
    /// not isomorphic, and the spread depends on how many suits tie.
    /// algorithm.md warns that such a defender may fall outside the
    /// `W_{pi,delta}` formulation. The general `Omega = S x B` construction
    /// covers it.
    AllOptimal,
};

/// The index that `spread()` treats as the canonical best entry under
/// `SpreadPolicy::TouchingSequence`: the highest `fut.score`, with ties
/// going to the first entry in dds order.
///
/// `fut` may hold several touching groups tied for best. Call this to see
/// which one `spread()` will pick before deferring to it. See
/// `second_seat_low`.
auto canonical_best(FutureTricks const& fut) -> int;

/// The cards `policy` spreads over, each with probability
/// `1 / candidate count`.
///
/// `fut.suit` and `fut.rank` name each candidate. `fut.equals` names the
/// other cards in its touching sequence, in the bit convention of `Deal`
/// (not the compacted one of `to_compacted`). Duplicates are removed: two
/// entries in one sequence do not double the weight of shared cards.
///
/// Pure: it takes a solved `FutureTricks` and calls no solver.
/// `fut.cards == 0` returns an empty distribution.
auto spread(FutureTricks const& fut, SpreadPolicy policy) -> std::vector<WeightedCard>;

}  // namespace dds::belief_evaluation
