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
/// **It is not currently a sound one — see `as_bound()`'s own "Known
/// unsoundness".** Until that is fixed, this bound can fire the cut on a
/// live node, so an evaluation using it may report 0.0 for a contract that
/// makes. Prefer running without `bound` (which costs pruning only) unless
/// you have checked the positions your search reaches.
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
    /// **Known unsoundness: that guard is bypassed on the success path.**
    /// `solutions = 1` asks the solver for the best card, and on positions
    /// this evaluator reaches it can return `score[0] == -2` — "not
    /// evaluated", not a trick count — with `status == RETURN_NO_FAULT`. The
    /// sentinel above therefore never applies and the −2 is returned as if it
    /// were a bound. Being negative it is below any `tricks_needed`, so
    /// `tier2_dead()` fires on a live node and `evaluate()` reports 0.0 for a
    /// contract that makes. Measured; `solutions = 3` scores every affected
    /// position correctly.
    ///
    /// **Which positions trigger it is not established.** Do not trust a
    /// trigger story, including the narrower one this header used to give and
    /// the one in `double_dummy_bound_test.cpp`'s own fixture note: a
    /// parameterised table in that file refutes both, showing `nodes == 0`
    /// coinciding with a correct score, the same holdings scoring correctly
    /// when only `first` changes, a two-suit position failing, and a forced
    /// play succeeding.
    ///
    /// The safe fix does not need the trigger: treat a score outside
    /// `[0, tricks_remaining(layout)]` as `SolverFailureSentinel`, restoring
    /// the "too high, never too low" property the paragraph above promises.
    /// Recovering the lost pruning does need it, or a retry at
    /// `solutions = 3`.
    auto as_bound() -> LayoutBound;

private:
    SolverContext& ctx_;
    int declarer_;
};

}  // namespace dds::belief_evaluation
