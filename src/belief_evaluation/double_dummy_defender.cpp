#include <belief_evaluation/double_dummy_defender.hpp>

// api/dds.h -- pulled in transitively by solver_context.hpp (whose own
// public interface exposes dds.h's internal move-generation types
// directly) and by api/solve_board.hpp -- declares its own unrelated
// "simple card representation" also named `struct Card`, alongside ExtCard
// and MoveType, the ones actually used by the solver's own internals
// (moves.cpp, heuristic_sorting.hpp). `Card` itself need not be used
// anywhere to collide: the moment both headers are visible in one
// translation unit, both declare an unqualified global `::Card`, which is
// a hard redefinition error regardless of whether either one is ever
// referenced. That collides head-on with this module's own `::Card`
// (belief_evaluation/types.hpp), used throughout this module's whole
// public surface -- as they must both be visible here, since this file is
// the one place that both calls the solver and builds a WeightedCard
// result from it.
//
// No local rename is needed, though: dds_types.hpp (see its own doxygen)
// centralizes exactly this rename, and this file's own header (included
// above, first) already chains through defender_strategy.hpp ->
// types.hpp -> dds_types.hpp, so api/dds.h's Card has already been
// renamed to DdsInternalCard by the time solve_board.hpp /
// solver_context.hpp are reached below. That does mean the include above
// must stay first in this file for the rename to still be in effect here
// -- double_dummy_defender.hpp forward-declares SolverContext for exactly
// this reason, so it does not trigger an earlier, unmangled inclusion of
// its own.
#include <api/solve_board.hpp>
#include <solver_context/solver_context.hpp>

DoubleDummyDefender::DoubleDummyDefender(SolverContext& ctx, SpreadPolicy policy)
    : ctx_(ctx), policy_(policy)
{
}

auto DoubleDummyDefender::as_strategy() -> DefenderStrategy
{
    return [this](DefenderQuery const& query) -> std::vector<WeightedCard>
    {
        FutureTricks fut{};
        // solutions = 2: measured directly (see
        // SolutionsTwoReportsEveryScoreTiedCandidateAcrossSuits in this
        // module's tests) that it reports every entry tied for the maximum
        // score -- across suits, not just within one -- with the same
        // scores and equals groups solutions = 3 gives those same entries.
        // It also collapses a touching sequence to one representative entry
        // per sequence, exactly as spread() needs. The entries solutions = 3
        // additionally returns (every legal card, not just the tied ones)
        // are strictly sub-optimal and are discarded by spread()'s own
        // score filter regardless (both SpreadPolicy values only ever keep
        // entries at the maximum score), so solutions = 2 already reports
        // everything spread() consumes, at less solver cost per query.
        int const status =
            solve_board(ctx_, query.layout, /*target=*/-1, /*solutions=*/2, /*mode=*/0, &fut);
        if (status != RETURN_NO_FAULT)
        {
            // Not silently empty-and-ignored: expand_defender_node's call
            // to validate_defender_distribution rejects an empty
            // distribution as ProbabilitiesDoNotSumToOne, so this surfaces
            // as a real ExpandDefenderResult::error rather than being
            // swallowed.
            return {};
        }
        return spread(fut, policy_);
    };
}
