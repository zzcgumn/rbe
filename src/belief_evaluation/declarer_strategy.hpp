#pragma once

#include <functional>

#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

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
    /// **Must be a pure function of its arguments.** The evaluator walks
    /// the tree in its own order and revisits sibling subtrees, so a
    /// strategy accumulating state across calls silently returns different
    /// cards for the same node and corrupts the result. Only deterministic
    /// strategies are supported.
    ///
    /// The likely accidental violation is a *seeded* strategy drawing from
    /// one PRNG stream across the whole search: the card then depends on
    /// how many decisions preceded it in traversal order. Derive the
    /// choice from the state instead — `choice = hash(seed, state) mod n`
    /// — which is stable under cuts, caching and sibling evaluations.
    ///
    /// The returned rank is **absolute**, never relative to the node's
    /// outstanding-card pool. A strategy reasoning in relative terms
    /// converts with one `RankMap::to_absolute` call before returning.
    std::function<Card(ObservationState const&, BeliefView const&)> play;

    /// Optional. Declares what `play` consults *beyond* the position the
    /// evaluator already keys on — nothing more, since the evaluator
    /// supplies the position component of any cache key itself. Never
    /// called today; there is no cache yet.
    ///
    /// Three states, not two: unset disables reuse entirely, an empty key
    /// is the strongest declaration available ("nothing beyond the
    /// position", so maximal reuse), and a non-empty key names the rest. A
    /// key coarser than `play`'s real dependence produces a wrong
    /// probability, not a slow one, and nothing checks it — so this must be
    /// pure on the same terms as `play`.
    std::function<StateKey(ObservationState const&, BeliefView const&)> state_key;
};

}  // namespace dds::belief_evaluation
