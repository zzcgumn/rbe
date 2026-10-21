#pragma once

#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/spread.hpp>

/// Forward-declared, not included: `solver_context.hpp` is slow to compile.
///
/// Declared at global scope, outside the namespace, because the type is
/// `::SolverContext`. Inside the namespace it would declare a different,
/// undefined type.
class SolverContext;

namespace dds::belief_evaluation
{

/// A DefenderStrategy that solves `query.layout` double dummy
/// (`solve_board`) and spreads probability over the result by `policy`.
/// See spread.hpp.
///
/// **Not best defence against a contract.** It maximises tricks
/// (`target = -1`). **It will sometimes give up the contract to hold down
/// the trick count.** A defender that only cares about the contract is
/// indifferent once the contract's fate is decided, so its choices would
/// be close to noise. This gap between tricks and `P_make` is what the
/// library measures. It is not a defect.
///
/// **It also assumes a double-dummy declarer.** Against a declarer with a
/// fixed line, or one reading a belief space, a defence tailored to that
/// declarer can do better. In `examples/`, against
/// `cash_two_hearts_and_play_a_spade`, this defender lets the contract make
/// in 55/70 layouts. A simple "cover when it wins" rule allows 35/70. See
/// `examples/test_guess_6nt_belief_space.py` for how both figures are
/// checked.
///
/// `ctx` is not owned and is not thread-safe: use one SolverContext per
/// thread. The caller creates and configures it, and keeps it alive.
class DoubleDummyDefender
{
public:
    explicit DoubleDummyDefender(
        SolverContext& ctx, SpreadPolicy policy = SpreadPolicy::TouchingSequence);

    /// Solves `query.layout` on every call and passes the result to
    /// `spread()`. There is no result cache. A non-zero `solve_board`
    /// status returns an empty distribution, which
    /// `validate_defender_distribution` rejects.
    ///
    /// The returned `DefenderStrategy` captures `this`. It must not outlive
    /// this `DoubleDummyDefender`.
    auto as_strategy() -> DefenderStrategy;

private:
    SolverContext& ctx_;
    SpreadPolicy policy_;
};

}  // namespace dds::belief_evaluation
