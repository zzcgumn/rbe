#pragma once

#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/spread.hpp>

/// Forward-declared rather than included: `solver_context.hpp` is heavy
/// (measured at roughly 25x the compile time of a translation unit with
/// just this declaration, ~200ms per TU). Every includer today already
/// needs the full header, so this buys nothing yet; it is kept for a future
/// includer that only references a `SolverContext` without calling into the
/// solver.
///
/// Declared here, outside the namespace below, because this is
/// `::SolverContext` at global scope. Inside the namespace it would declare
/// a distinct, never-defined type.
class SolverContext;

namespace dds::belief_evaluation
{

/// A DefenderStrategy that solves `query.layout` double dummy
/// (`solve_board`) and spreads probability over the result via `policy`
/// (see spread.hpp).
///
/// **This is not best defence against a contract.** It maximises tricks
/// (`target = -1`), not the contract threshold: a threshold-aware
/// double-dummy defender is indifferent across every line once the
/// contract's fate is already decided, so its equivalence classes
/// degenerate toward uniform noise, while trick-maximising keeps a usable
/// gradient at every node. The consequence is real — **it will sometimes
/// concede the contract to hold the trick count down** — and is an instance
/// of the gap between maximising tricks and minimising `P_make` that this
/// capability exists to quantify, not a defect to work around.
class DoubleDummyDefender
{
public:
    /// `ctx` is not owned — the caller creates, configures and outlives it,
    /// keeping transposition-table sizing and the solver's threading model
    /// out of this defender's hands.
    explicit DoubleDummyDefender(
        SolverContext& ctx, SpreadPolicy policy = SpreadPolicy::TouchingSequence);

    /// Solves `query.layout` once per call and delegates to `spread()`.
    /// There is no result cache: future work needing one shares it across
    /// callers rather than hiding it in this defender. A non-zero
    /// `solve_board` status returns an empty distribution, which
    /// `validate_defender_distribution` then rejects — the existing
    /// validation path surfaces the error rather than a second one.
    ///
    /// The returned `DefenderStrategy` captures `this` and must not outlive
    /// this `DoubleDummyDefender`.
    auto as_strategy() -> DefenderStrategy;

private:
    SolverContext& ctx_;
    SpreadPolicy policy_;
};

}  // namespace dds::belief_evaluation
