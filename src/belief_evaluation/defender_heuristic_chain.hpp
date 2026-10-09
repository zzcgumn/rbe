#pragma once

#include <optional>
#include <vector>

#include <belief_evaluation/defender_heuristic.hpp>
#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// An ordered sequence of DefenderHeuristic rules, tried in add() order.
/// The one and only way a chain is built -- there is no second
/// constructor, no denomination argument, no enum of "which built-ins to
/// include": a caller's chain is entirely and exactly whatever sequence of
/// add() calls they make.
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
