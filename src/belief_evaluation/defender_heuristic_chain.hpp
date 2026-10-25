#pragma once

#include <optional>
#include <vector>

#include <belief_evaluation/defender_heuristic.hpp>
#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// An ordered list of DefenderHeuristic rules, tried in add() order.
/// add() is the only way to build one.
class DefenderHeuristicChain
{
public:
    /// Appends `heuristic` to the end of the chain.
    auto add(DefenderHeuristic heuristic) -> void;

    /// Tries each heuristic in add() order; returns the first non-nullopt
    /// result, or nullopt if every one deferred.
    auto select_card(DefenderHeuristicContext const& ctx, std::vector<Card> const& best_cards) const
        -> std::optional<Card>;

private:
    std::vector<DefenderHeuristic> heuristics_;
};

}  // namespace dds::belief_evaluation
