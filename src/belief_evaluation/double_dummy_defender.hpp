#pragma once

#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/spread.hpp>

/// Forward-declared, not included: every current includer of this header
/// already needs the full `solver_context.hpp` (double_dummy_defender.cpp
/// itself, its test, and double_dummy_bound.hpp/.cpp, its solver-linked
/// sibling), so this buys nothing measurable today -- but `solver_context.hpp`
/// is a genuinely heavy header (measured directly: compiling a translation
/// unit that includes it costs roughly 25x what one with just this forward
/// declaration does, on the order of 200ms extra per TU), and a future
/// includer that only needs to *reference* a `SolverContext` -- without
/// constructing one or calling into the solver -- pays that cost on every
/// build for nothing. Kept for that future consumer rather than dropped
/// for lack of one today.
///
/// Declared here, above `namespace dds::belief_evaluation` below: this is
/// `library/src/solver_context/solver_context.hpp`'s `::SolverContext`, at
/// global scope. A forward declaration inside the namespace block would
/// instead declare a distinct, never-defined
/// `dds::belief_evaluation::SolverContext`.
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
/// contract's fate is already double-dummy decided, so its equivalence
/// classes degenerate toward uniform noise, while trick-maximising keeps a
/// usable gradient at every node. The consequence is real, not academic --
/// **a trick-maximising defender will sometimes concede the contract to
/// hold the trick count down** -- and is a specific instance of the gap
/// between maximising tricks and minimising `P_make` that this whole
/// capability exists to quantify, not a defect to work around.
class DoubleDummyDefender
{
public:
    /// `ctx` is not owned -- the caller creates, configures and outlives
    /// it, keeping transposition-table sizing and lifetime entirely the
    /// caller's decision, and keeping the solver's own threading model
    /// uncoupled from this defender's.
    explicit DoubleDummyDefender(
        SolverContext& ctx, SpreadPolicy policy = SpreadPolicy::TouchingSequence);

    /// Solves `query.layout` once per call (no result cache -- future work
    /// that needs one, e.g. a cut-introducing evaluator, shares it across
    /// callers rather than caching inside this defender) and delegates to
    /// `spread()`. A non-zero `solve_board` status returns an empty
    /// distribution, which `validate_defender_distribution` then rejects as
    /// `ProbabilitiesDoNotSumToOne` -- the existing validation path is what
    /// surfaces the error, rather than this defender inventing a second one.
    ///
    /// The returned `DefenderStrategy` captures `this` and is valid only
    /// for this `DoubleDummyDefender`'s own lifetime -- do not outlive it.
    auto as_strategy() -> DefenderStrategy;

private:
    SolverContext& ctx_;
    SpreadPolicy policy_;
};

}  // namespace dds::belief_evaluation
