#pragma once

#include <vector>

#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// The true lowest rank of the touching run that `best_cards[index]`
/// represents. `solve_board` reports a run as its *highest* card, with the
/// lower cards in that entry's `equals` bitmask. **A rule that wants the
/// lowest card of a group must call this.** `best_cards[index].rank` names
/// only the highest.
///
/// Returns `best_cards[index].rank` when `index` is outside `fut`, or the
/// entry's `equals` is empty.
///
/// Precondition: `best_cards` and `fut` are parallel (see
/// `DefenderHeuristicContext::fut`).
///
/// Within one suit, a hand's touching runs do not overlap. So a search by
/// `best_cards[i].rank` finds the right index, and only the result needs
/// this call. Across suits that is not true: resolve each candidate first
/// (see `discard_keep_winners`).
auto true_lowest_rank(std::vector<Card> const& best_cards, FutureTricks const& fut, std::size_t index) -> int;

}  // namespace dds::belief_evaluation
