#pragma once

#include <belief_evaluation/defender_heuristic_chain.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/spread.hpp>

/// Forward-declared, not included. See double_dummy_defender.hpp.
class SolverContext;

namespace dds::belief_evaluation
{

/// A `DefenderStrategy` backed by a chain of small rules (second hand low,
/// third hand high or low, fourth hand low, ruff small, discard keeping
/// winners). When every rule defers, it uses `spread()`, as
/// `DoubleDummyDefender` does. Either class can be the `delta` of
/// `evaluate()`.
///
/// **The caller builds the chain.** This class never calls
/// `make_default_defender_heuristics()`. Any chain, in any order, with
/// built-in or caller rules, behaves the same way. See
/// `default_defender_heuristics.hpp`.
///
/// The ctx rules of DoubleDummyDefender apply.
class HeuristicDefender
{
public:
    explicit HeuristicDefender(
        SolverContext& ctx, DefenderHeuristicChain chain, SpreadPolicy fallback_policy = SpreadPolicy::TouchingSequence);

    /// Solves `query.layout` on every call, as `DoubleDummyDefender` does,
    /// and asks the chain. If a rule fires, that card at probability 1.0.
    /// Otherwise the `spread()` distribution. A non-zero `solve_board` status
    /// returns an empty distribution.
    ///
    /// The returned `DefenderStrategy` captures `this`. It must not outlive
    /// this `HeuristicDefender`.
    auto as_strategy() -> DefenderStrategy;

private:
    SolverContext& ctx_;
    DefenderHeuristicChain chain_;
    SpreadPolicy fallback_policy_;
};

}  // namespace dds::belief_evaluation
