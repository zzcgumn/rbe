#pragma once

#include <exception>
#include <functional>

#include <belief_evaluation/types.hpp>
#include <belief_evaluation/validation.hpp>

namespace dds::belief_evaluation
{

/// Declarer's strategy: an identity (`id`), the card choice (`play`), and
/// what `play` depends on beyond the position (`state_key`).
///
/// The evaluator calls `play` once per card, whenever declarer or dummy is
/// on play. When declarer wins a trick and leads to the next, `play` is
/// called twice, with different states.
struct DeclarerStrategy
{
    StrategyId id;  ///< unique among strategies compared together

    /// Chooses the next card for declarer or dummy, whichever is on play.
    ///
    /// **Must be a pure function of its arguments.** The evaluator visits
    /// nodes in its own order and revisits subtrees. A strategy that keeps
    /// state between calls returns different cards for the same node, and
    /// the result is silently wrong. Only deterministic strategies are
    /// supported.
    ///
    /// A common mistake is a seeded strategy with one PRNG stream for the
    /// whole search. Derive the choice from the state instead:
    /// `choice = hash(seed, state) mod n`.
    ///
    /// The returned rank is **absolute**. Convert a relative rank with
    /// `RankMap::to_absolute` before returning.
    std::function<Card(ObservationState const&, BeliefView const&)> play;

    /// Optional. Declares what `play` reads *beyond* the position. The
    /// evaluator adds the position to any cache key itself. Never called:
    /// there is no cache.
    ///
    /// Three states:
    /// - unset: no reuse;
    /// - empty key: `play` reads nothing beyond the position (most reuse);
    /// - non-empty key: names what else `play` reads.
    ///
    /// **A key coarser than what `play` really reads gives a wrong
    /// probability, and nothing checks it.** It must be pure, like `play`.
    std::function<StateKey(ObservationState const&, BeliefView const&)> state_key;
};

/// Thrown by a `DeclarerStrategy::play` implementation, never by the
/// evaluator. It reports that a delegate configured by the caller broke
/// its contract, so `play` cannot choose a card.
///
/// Do not throw it for a bad `state` or `view`, or for a bad card returned
/// by `play`: `validate_declarer_card` checks that anyway.
///
/// `seat` and `offending_layout` belong to *the delegate*: its seat and
/// the layout it was asked about. They are not `play`'s seat or the node's
/// `known_holdings`. The delegate is often a defender-shaped callback.
///
/// `expand_declarer_node` (expand.cpp) catches this type and reports it as
/// an ordinary `ValidationError` in the `EvaluationResult`, with this
/// `seat` and `offending_layout`. So it never leaves `evaluate()`. A caller
/// who calls `play` directly sees the exception. `play` has no other error
/// channel.
///
/// A strategy may derive from it, as `BruteForceOpponentModelError` does.
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
