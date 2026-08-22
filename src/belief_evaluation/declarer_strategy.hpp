#pragma once

#include <functional>

#include <belief_evaluation/types.hpp>

/// Declarer's decision strategy. A struct rather than a bare callable because
/// it needs somewhere to hang an identity (`id`) and somewhere to declare
/// what it depends on beyond position (`state_key`).
///
/// The evaluator calls `play` once per card: whenever the seat on play is
/// declarer or dummy (derivable from `ObservationState`), never with two
/// cards bundled for a trick boundary. At a boundary where declarer wins and
/// leads next, `play` is simply called twice with different states.
struct DeclarerStrategy
{
    StrategyId id;  ///< unique among strategies compared together

    /// Chooses declarer's or dummy's next card, whichever seat is on play.
    ///
    /// **Must be a pure function of its arguments.** The evaluator walks the
    /// tree in its own order and revisits sibling subtrees; a strategy that
    /// accumulates state across calls will silently return different cards
    /// for the same node and corrupt the result. This library only supports
    /// deterministic strategies.
    ///
    /// The most likely accidental violation is a *seeded* strategy: drawing
    /// from one PRNG stream across the whole search makes the card returned
    /// at a node depend on how many decisions preceded it in traversal
    /// order, rather than on the node itself. That breaks silently under
    /// early cuts (which change how many draws precede a given node), under
    /// any future cache keyed on `state_key`, and makes `state_key`
    /// impossible to write honestly in the first place. The remedy is to
    /// derive the choice from the state instead of from a stream —
    /// `choice = hash(seed, state) mod n` — which gives the same card for
    /// the same state regardless of traversal order, cuts, caching, or
    /// sibling evaluations.
    ///
    /// The returned rank is **absolute**, never relative to the node's
    /// outstanding-card pool. A strategy reasoning in relative terms
    /// converts with one `RankMap::to_absolute` call before returning.
    std::function<Card(ObservationState const&, BeliefView const&)> play;

    /// Optional. Declares what `play` consults *beyond* the position the
    /// evaluator already keys on — nothing more, since the evaluator
    /// supplies the position component of any cache key itself.
    ///
    /// Unset disables reuse for this strategy entirely. Set but returning an
    /// empty key is the strongest declaration available: "nothing beyond the
    /// position", i.e. maximal reuse. A key coarser than `play`'s real
    /// dependence produces a wrong probability, not a slow one, so this
    /// function must be pure on the same terms as `play`.
    std::function<StateKey(ObservationState const&, BeliefView const&)> state_key;
};
