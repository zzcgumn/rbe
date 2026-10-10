#pragma once

#include <exception>
#include <functional>

#include <belief_evaluation/types.hpp>
#include <belief_evaluation/validation.hpp>

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

/// Thrown from a `DeclarerStrategy::play` implementation -- never by the
/// evaluator itself -- to report that it could not produce a card at all,
/// because of a `ValidationError`-shaped contract violation it detected in
/// one of its own internal, caller-configured delegates. Never for a
/// malformed `state`/`view` argument, which `play` must still handle like
/// any other input, and never for anything the evaluator's own validation
/// already covers (a card `play` itself returns is checked by
/// `validate_declarer_card` regardless; throwing has nothing to add there).
///
/// `seat` and `offending_layout` name *that internal delegate's own* seat
/// and the exact layout it was asked to decide for -- not `play`'s own
/// seat or the node's `known_holdings`, which `evaluate()` already has
/// without this type's help and which would misreport where the violation
/// actually happened (a declarer-side strategy's internal delegate is
/// typically itself a defender-shaped callback, deciding for a different
/// seat over a more specific layout than the outer node's own).
///
/// `expand_declarer_node` (expand.cpp) catches exactly this type at
/// `evaluate()`'s own call boundary and converts it into the same
/// `ValidationError`-carrying `EvaluationResult` any other declarer-side
/// contract violation produces -- carrying `seat`/`offending_layout`
/// through rather than substituting the node's own, so this never crosses
/// `evaluate()`'s own public entry point, upholding "a user callback's
/// contract violation is reported in the result, never thrown" *with
/// accurate context* (`specs/replenished-belief-evaluation.md`) for every
/// caller who reaches a `DeclarerStrategy` only through `evaluate()`. A
/// caller invoking `play` directly, bypassing `evaluate()` -- as this
/// module's own tests do, to exercise exactly this path -- sees it
/// propagate as an ordinary C++ exception instead. There is no third
/// option with `play`'s own `Card(ObservationState const&, BeliefView
/// const&)` signature, which has no error channel of its own; changing
/// that signature is a larger, cross-cutting interface decision this type
/// deliberately avoids forcing.
///
/// A concrete `DeclarerStrategy` implementation may derive from this to add
/// its own richer context for the direct-caller case -- see
/// `BruteForceDeclarer`'s own `BruteForceOpponentModelError`, which adds
/// nothing beyond a more specific `what()`, `seat`/`offending_layout`
/// already living here.
struct DeclarerStrategyContractViolation : std::exception
{
    ValidationError validation;
    int seat;
    Deal offending_layout;

    DeclarerStrategyContractViolation(ValidationError validation, int seat, Deal const& offending_layout)
    : validation(validation), seat(seat), offending_layout(offending_layout)
    {
    }

    auto what() const noexcept -> char const* override
    {
        return "a DeclarerStrategy's own internal delegate violated its contract";
    }
};

}  // namespace dds::belief_evaluation
