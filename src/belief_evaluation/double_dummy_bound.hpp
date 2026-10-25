#pragma once

#include <belief_evaluation/evaluate.hpp>

/// Forward-declared at global scope. See double_dummy_defender.hpp.
class SolverContext;

namespace dds::belief_evaluation
{

/// A LayoutBound backed by `solve_board()`: the most tricks `declarer`
/// can take from a layout, double dummy, whoever is on lead.
///
/// **`solve_board` scores for the side on lead.** When a defender is on
/// lead, the bound is `tricks_remaining(layout)` minus the score.
/// double_dummy_bound_test.cpp checks this.
///
/// **Caller precondition:** a cut may use this bound only when the paired
/// `delta` is double-dummy optimal for trick count. See
/// `EvaluateOptions::delta_is_double_dummy_optimal`. `DoubleDummyDefender`
/// qualifies under either `SpreadPolicy`, and is the intended pair.
///
/// When the solver returns no valid score, this asks again. If that fails
/// too, or the solver reports an error, it returns a value that is too
/// high. See `as_bound()`.
class DoubleDummyBound
{
public:
    /// `ctx` is not owned, as for `DoubleDummyDefender`. `declarer` is
    /// fixed for the bound's lifetime.
    explicit DoubleDummyBound(SolverContext& ctx, int declarer);

    /// Solves `layout` on every call. There is no result cache.
    ///
    /// **When the solver fails, this returns 14** (`SolverFailureSentinel`),
    /// one more than any deal can have. A bound is an upper limit, so a high
    /// value only loses pruning. A low value would fire the cut and report
    /// zero for a contract that makes. There is no other error channel.
    ///
    /// The solver fails in two ways:
    /// - a non-zero `solve_board` status;
    /// - a score outside `[0, tricks_remaining(layout)]`, such as −2 ("not
    ///   evaluated") with `status == RETURN_NO_FAULT`.
    ///
    /// **Do not clamp a missing score.** Clamping −2 gives 0, and the cut
    /// would still fire. The value must be too high.
    ///
    /// An out-of-range score is retried at `solutions = 3` before giving up.
    /// A non-zero status is not retried. `solutions = 3` scored every
    /// position measured to fail at `solutions = 1`. So tier 2 still prunes:
    /// on a four-card ending, nodes visited fall from 6283 to 3159.
    ///
    /// **Which positions fail is not known.** The tests in
    /// `double_dummy_bound_test.cpp` refute every explanation tried so far.
    /// Do not rely on one.
    auto as_bound() -> LayoutBound;

private:
    SolverContext& ctx_;
    int declarer_;
};

}  // namespace dds::belief_evaluation
