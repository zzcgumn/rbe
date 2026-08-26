#include <belief_evaluation/double_dummy_defender.hpp>

// api/dds.h -- pulled in transitively by solver_context.hpp (whose own
// public interface exposes dds.h's internal move-generation types
// directly) and by api/solve_board.hpp -- declares its own unrelated
// "simple card representation" also named `struct Card` (see
// api/dds.h:141-150, used throughout the solver's own internals in
// moves.cpp and heuristic_sorting.hpp). That collides head-on with
// belief_evaluation::Card (belief_evaluation/types.hpp), used throughout
// this module's whole public surface, the moment both headers are visible
// in the same translation unit -- as they must be here, since this file is
// the one place that both calls the solver and builds a WeightedCard
// result from it. Neither Card is ours to rename: dds.h's is load-bearing
// well beyond this plan, and this module's is load-bearing throughout its
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
        // solutions = 3, not 2: measured directly (see this file's tests)
        // that solutions = 2 silently omits every candidate that is not
        // already score-optimal -- a legal card strictly worse than the
        // best is simply absent from fut, with no score of its own to
        // compare against. AllOptimal happens not to need those entries
        // (it only ever keeps score-optimal candidates anyway), but
        // relying on that coincidence would be fragile, and the touching-
        // sequence collapse spread() depends on (one representative entry
        // per sequence, via `equals`, not one entry per card) turned out to
        // hold under solutions = 3 too -- confirmed directly, not assumed.
        int const status =
            solve_board(ctx_, query.layout, /*target=*/-1, /*solutions=*/3, /*mode=*/0, &fut);
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
