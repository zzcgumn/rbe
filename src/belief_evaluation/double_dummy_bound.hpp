#pragma once

#include <belief_evaluation/evaluate.hpp>

/// Forward-declared rather than included, and declared here at global
/// scope, for the same reasons as `double_dummy_defender.hpp`'s own copy —
/// see that header.
class SolverContext;

namespace dds::belief_evaluation
{

/// A LayoutBound backed by `solve_board()`: the maximum tricks `declarer`
/// can take from a given layout, double dummy, regardless of which seat is
/// actually on lead at that layout.
///
/// **`solve_board`'s own score is not this, directly.** It reports tricks
/// for whichever side is on lead at the position solved — verified
/// empirically in double_dummy_bound_test.cpp, not assumed. When a defender
/// is on lead the raw score is the *defenders'* best trick count, and
/// declarer's bound is `tricks_remaining(layout)` minus it, bridge being
/// zero-sum trick by trick.
///
/// **The precondition a caller takes on**: `R <= DD`, which is what lets a
/// cut use this bound at all, holds only when the paired `delta` is
/// double-dummy optimal for trick count — see
/// `EvaluateOptions::delta_is_double_dummy_optimal`. `DoubleDummyDefender`
/// satisfies it under either `SpreadPolicy`, and pairing the two is the
/// intended configuration.
///
/// On positions the solver answers "not evaluated" for it asks again rather
/// than guessing, and reports a deliberately too-high sentinel only if that
/// also declines — see `as_bound()`'s own "Unscorable positions".
class DoubleDummyBound
{
public:
    /// `ctx` is not owned, as with `DoubleDummyDefender`. `declarer` is
    /// fixed for this bound's whole lifetime, matching `evaluate()`'s own
    /// single `declarer` per evaluation.
    explicit DoubleDummyBound(SolverContext& ctx, int declarer);

    /// Solves `layout` once per call, with no result cache — same
    /// reasoning as `DoubleDummyDefender`.
    ///
    /// A non-zero `solve_board` status returns 14: one more than any deal
    /// could ever have, so it cannot be mistaken for a real bound by a cut
    /// comparing against tricks still needed. Unlike
    /// `DoubleDummyDefender`, there is no validation path for a failure to
    /// fall into — a bound provider has no distribution to reject — so this
    /// sentinel is the only signal, and is deliberately too *high*: a cut
    /// reads a bound as an upper limit, so a high sentinel merely fails to
    /// prune, where a low one would fire the cut and silently report zero
    /// for a contract that makes.
    ///
    /// **Unscorable positions also reach that sentinel, by a second
    /// route.** A non-zero status is not the only way `solve_board` declines
    /// to answer: at `solutions = 1`, on positions this evaluator reaches, it
    /// can return `score[0] == -2` — "not evaluated", not a trick count —
    /// carrying `status == RETURN_NO_FAULT`, so the status check above does
    /// not see it. The raw score is therefore range-checked against
    /// `[0, tricks_remaining(layout)]` before the orientation conversion, and
    /// anything outside it becomes `SolverFailureSentinel` too. Both
    /// conversions read the same raw score, so one check covers declarer,
    /// dummy and defender on lead alike.
    ///
    /// Note that this is *not* a clamp, and a clamp would not do: clamping
    /// −2 into the range gives 0, which is below every `tricks_needed >= 1`,
    /// so the cut would still fire on every affected node while the bug
    /// looked fixed. The replacement value has to be too high.
    ///
    /// Before giving up, it asks again at `solutions = 3`, which scores every
    /// position measured to decline at `solutions = 1`. So the sentinel is now
    /// the floor rather than the usual answer, and the pruning is not lost:
    /// measured on a four-card ending, tier 2 takes nodes visited from 6283 to
    /// 3159 with 197 cuts, and a retry on the positions that need one is
    /// cheaper than searching the subtrees they cut.
    ///
    /// **Which positions the solver answers this way is still not
    /// established.** The retry sidesteps the question rather than answering
    /// it. Do not trust a trigger story, including the narrower one this
    /// header used to give and the one in `double_dummy_bound_test.cpp`'s own
    /// fixture note: a parameterised table in that file refutes both, showing
    /// `nodes == 0` coinciding with a correct score, the same holdings
    /// scoring correctly when only `first` changes, a two-suit position
    /// failing, and a forced play succeeding.
    auto as_bound() -> LayoutBound;

private:
    SolverContext& ctx_;
    int declarer_;
};

}  // namespace dds::belief_evaluation
