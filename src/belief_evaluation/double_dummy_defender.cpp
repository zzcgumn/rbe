#include <belief_evaluation/double_dummy_defender.hpp>

// api/dds.h -- pulled in transitively by solver_context.hpp (whose own
// public interface exposes dds.h's internal move-generation types
// directly) and by api/solve_board.hpp -- declares its own unrelated
// "simple card representation" also named `struct Card` (see
// api/dds.h:141-150), alongside ExtCard and MoveType, the ones actually
// used by the solver's own internals (moves.cpp, heuristic_sorting.hpp).
// `Card` itself need not be used anywhere to collide: the moment both
// headers are visible in one translation unit, both declare an unqualified
// global `::Card`, which is a hard redefinition error regardless of
// whether either one is ever referenced. That collides head-on with
// belief_evaluation::Card (belief_evaluation/types.hpp), used throughout
// this module's whole public surface, the moment both headers are visible
// in the same translation unit -- as they must be here, since this file is
// the one place that both calls the solver and builds a WeightedCard
// result from it. Neither Card is ours to rename: dds.h's is load-bearing
// well beyond this module, and this module's is load-bearing throughout its
// own public API. Renaming dds.h's *locally*, for the duration of these two
// includes only, is the smallest fix that touches neither header -- these
// must be the first inclusion of solver_context.hpp / api/dds.h in this
// translation unit for the rename to actually take effect (double_dummy_
// defender.hpp forward-declares SolverContext for exactly this reason, so
// it does not trigger an earlier, unmangled inclusion).
#define Card DdsInternalCard
#include <api/solve_board.hpp>
#include <solver_context/solver_context.hpp>
#undef Card

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
