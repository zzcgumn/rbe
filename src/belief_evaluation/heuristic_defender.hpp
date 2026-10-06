#pragma once

#include <belief_evaluation/defender_heuristic_chain.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/spread.hpp>

/// Forward-declared rather than included -- see double_dummy_defender.hpp's
/// own doxygen on this same declaration for why.
class SolverContext;

namespace dds::belief_evaluation
{

/// A `DefenderStrategy` backed by a caller-assembled chain of small,
/// independently-testable rules (second-hand low, third-hand high/low,
/// fourth-hand low, ruff small, discard keeping winners), falling back to
/// `DoubleDummyDefender`'s own `spread()` when every rule in the chain
/// defers. A second `DefenderStrategy` implementation, not a replacement
/// for `DoubleDummyDefender` -- both are usable anywhere `evaluate()`'s
/// `delta` argument is accepted.
///
/// **The chain is entirely caller-assembled.** This class never calls
/// `make_default_defender_heuristics()` implicitly and has no notion of
/// "the default chain" to special-case -- a `DefenderHeuristicChain`
/// built by hand, in any order, with any subset of the built-in rules and
/// any caller-supplied rules spliced in, behaves identically to the
/// convenience one. See `default_defender_heuristics.hpp`.
///
/// `ctx` is not owned, and is not thread-safe -- the same contract
/// `DoubleDummyDefender` already carries; see that class's own doxygen.
class HeuristicDefender
{
public:
    explicit HeuristicDefender(
        SolverContext& ctx, DefenderHeuristicChain chain, SpreadPolicy fallback_policy = SpreadPolicy::TouchingSequence);

    /// Solves `query.layout` once per call (same `solve_board` call
    /// `DoubleDummyDefender::as_strategy()` makes), builds the candidate
    /// set and context, and delegates to the chain -- a single determined
    /// card at probability 1.0 if any rule fires, else `spread()`'s own
    /// distribution over the solved position. A non-zero `solve_board`
    /// status returns an empty distribution, exactly as
    /// `DoubleDummyDefender::as_strategy()` already does.
    auto as_strategy() -> DefenderStrategy;

private:
    SolverContext& ctx_;
    DefenderHeuristicChain chain_;
    SpreadPolicy fallback_policy_;
};

}  // namespace dds::belief_evaluation
