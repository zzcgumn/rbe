#pragma once

#include <belief_evaluation/evaluate.hpp>

/// Forward-declared, not included -- same reason as
/// `DoubleDummyDefender`'s own forward declaration: `solver_context.hpp`
/// exposes `api/dds.h`'s internal move-generation types directly, and
/// `api/dds.h` declares its own unrelated `struct Card` that collides with
/// this module's `::Card` the moment both are visible in one translation
/// unit. Keeping the full include out of this header confines the
/// collision to `double_dummy_bound.cpp`.
///
/// Declared here, above `namespace dds::belief_evaluation` below, for the
/// same reason as `double_dummy_defender.hpp`'s own copy of this note: this
/// is `::SolverContext` at global scope, not a namespaced, never-defined
/// lookalike.
class SolverContext;

namespace dds::belief_evaluation
{

/// A LayoutBound backed by `solve_board()`: the maximum tricks `declarer`
/// can take from a given layout, double dummy, regardless of which seat is
/// actually on lead at that layout.
///
/// **`solve_board`'s own score is not this, directly.** It reports tricks
/// for whichever side is on lead at the position solved -- verified
/// empirically (see double_dummy_bound_test.cpp's own orientation tests),
/// not assumed -- so a raw score is declarer's own bound only when
/// declarer or dummy happens to be on lead at `layout`. When a defender is
/// on lead instead, the raw score is the *defenders'* own best trick
/// count, and declarer's bound is derived as tricks_remaining(layout) minus
/// that (bridge is zero-sum trick by trick: every remaining trick goes to
/// exactly one side).
///
/// **The precondition this bound carries, stated for the caller who
/// obtains one from here**: `R <= DD` (the reason a cut may use this bound
/// at all) holds only when the paired `delta` is double-dummy optimal for
/// trick count -- see `EvaluateOptions::delta_is_double_dummy_optimal`'s
/// own doxygen for what that means and why it cannot be checked here.
/// `DoubleDummyDefender` (either `SpreadPolicy`) satisfies it, and pairing
/// this bound with that defender is the intended sound configuration.
class DoubleDummyBound
{
public:
    /// `ctx` is not owned, same as `DoubleDummyDefender`'s own contract --
    /// the caller creates, configures and outlives it. `declarer` is fixed
    /// for this bound's whole lifetime, matching `evaluate()`'s own single
    /// `declarer` parameter for one evaluation.
    explicit DoubleDummyBound(SolverContext& ctx, int declarer);

    /// Solves `layout` once per call (no result cache -- same reasoning as
    /// `DoubleDummyDefender`: future work that needs one shares it across
    /// callers rather than caching inside this provider). A non-zero
    /// `solve_board` status returns 14 -- one more than the most tricks any
    /// deal could ever have (13), so it can never be mistaken for a real
    /// bound by a cut that only ever compares against `tricks_needed -
    /// tricks_won` (both bounded well below that). There is no validation
    /// path here to fall into the way `DoubleDummyDefender`'s solver
    /// failure falls into `validate_defender_distribution` -- a bound
    /// provider has no distribution to reject, so this sentinel is the
    /// only signal a failure has, and it is deliberately impossible to
    /// confuse with a trick count rather than merely unlikely to be.
    /// Deliberately too *high*, not too low: a future cut only ever reads
    /// a bound as an upper limit on what declarer can take, so an
    /// unrealistically high sentinel just fails to prune (wasteful, not
    /// unsound) on a solver failure, where a too-low sentinel would make
    /// the cut fire and silently report zero for a contract that makes --
    /// exactly the trap this whole seam exists to avoid.
    auto as_bound() -> LayoutBound;

private:
    SolverContext& ctx_;
    int declarer_;
};

}  // namespace dds::belief_evaluation
