#pragma once

#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/spread.hpp>

/// Forward-declared, not included: `solver_context.hpp`'s own public
/// interface (SearchContext::best_move() and friends) exposes api/dds.h's
/// internal move-generation types directly, and api/dds.h declares its own
/// unrelated `struct Card` that collides head-on with this module's own
/// `::Card` (belief_evaluation/types.hpp) the moment both are visible in
/// one translation unit. Keeping the full solver_context.hpp include out of
/// this header confines that collision to double_dummy_defender.cpp, the
/// one file that actually needs both -- see its own top-of-file comment for
/// how it resolves it.
class SolverContext;

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
