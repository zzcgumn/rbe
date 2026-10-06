#include <belief_evaluation/defender_heuristic_chain.hpp>

namespace dds::belief_evaluation
{

auto DefenderHeuristicChain::add(DefenderHeuristic heuristic) -> void
{
    heuristics_.push_back(std::move(heuristic));
}

auto DefenderHeuristicChain::select_card(
    DefenderHeuristicContext const& ctx, std::vector<Card> const& best_cards) const -> std::optional<Card>
{
    for (auto const& heuristic : heuristics_)
    {
        if (auto card = heuristic(ctx, best_cards))
        {
            return card;
        }
    }
    return std::nullopt;
}

}  // namespace dds::belief_evaluation
