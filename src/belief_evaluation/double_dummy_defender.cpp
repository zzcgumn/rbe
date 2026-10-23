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
        // solutions = 2 reports every entry tied for the best score, across
        // suits, with the same scores and equals groups as solutions = 3
        // (SolutionsTwoReportsEveryScoreTiedCandidateAcrossSuits). solutions = 3
        // adds only sub-optimal entries, which spread() discards. So 2 is enough,
        // and cheaper.
        int const status =
            solve_board(ctx_, query.layout, /*target=*/-1, /*solutions=*/2, /*mode=*/0, &fut);
        if (status != RETURN_NO_FAULT)
        {
            // expand_defender_node reports an empty distribution as
            // DistributionEmpty, so a solver failure is not swallowed.
            return {};
        }
        return spread(fut, policy_);
    };
}

}  // namespace dds::belief_evaluation
