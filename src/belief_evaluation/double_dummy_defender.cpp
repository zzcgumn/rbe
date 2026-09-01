#include <belief_evaluation/double_dummy_defender.hpp>

#include <api/solve_board.hpp>
#include <solver_context/solver_context.hpp>

namespace dds::belief_evaluation
{

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

}  // namespace dds::belief_evaluation
