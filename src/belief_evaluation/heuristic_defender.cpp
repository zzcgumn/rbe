#include <belief_evaluation/heuristic_defender.hpp>

#include <api/solve_board.hpp>
#include <solver_context/solver_context.hpp>

namespace dds::belief_evaluation
{

HeuristicDefender::HeuristicDefender(
    SolverContext& ctx, DefenderHeuristicChain chain, SpreadPolicy fallback_policy)
    : ctx_(ctx), chain_(std::move(chain)), fallback_policy_(fallback_policy)
{
}

auto HeuristicDefender::as_strategy() -> DefenderStrategy
{
    return [this](DefenderQuery const& query) -> std::vector<WeightedCard>
    {
        FutureTricks fut{};
        // Same call, same solutions = 2 justification, as
        // DoubleDummyDefender::as_strategy() -- see that function's own
        // comment.
        int const status =
            solve_board(ctx_, query.layout, /*target=*/-1, /*solutions=*/2, /*mode=*/0, &fut);
        if (status != RETURN_NO_FAULT)
        {
            return {};
        }

        std::vector<Card> best_cards;
        best_cards.reserve(static_cast<std::size_t>(fut.cards));
        for (int i = 0; i < fut.cards; ++i)
        {
            best_cards.push_back(Card{fut.suit[i], fut.rank[i]});
        }

        DefenderHeuristicContext const ctx =
            make_defender_heuristic_context(query.layout, query.state, query.seat, fut);

        if (std::optional<Card> const card = chain_.select_card(ctx, best_cards))
        {
            return {WeightedCard{*card, 1.0}};
        }

        return spread(fut, fallback_policy_);
    };
}

}  // namespace dds::belief_evaluation
