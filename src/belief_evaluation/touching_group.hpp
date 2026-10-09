#pragma once

#include <vector>

#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// The true lowest rank of the touching run `best_cards[index]` represents,
/// resolved against `fut.equals` -- `solve_board` collapses a touching run
/// to one canonical entry (the *highest* member of the run), folding every
/// lower member into that one entry's own `equals` bitmask. A rule that
/// wants "the lowest card in this group" must resolve it here rather than
/// reading `best_cards[index].rank` directly, which only ever names the
/// representative -- the bug `second_seat_low` originally had, and the one
/// every other rule comparing candidates by raw rank shares.
///
/// Falls back to `best_cards[index].rank` verbatim when `index` is outside
/// `fut`'s own range or that entry's `equals` is empty -- the group is just
/// that one card, nothing to resolve.
///
/// Same precondition as every rule that reads `fut` against `best_cards`:
/// the two must be parallel, i.e. `best_cards[i] == Card{fut.suit[i],
/// fut.rank[i]}` for every `i` (see `DefenderHeuristicContext::fut`'s own
/// doxygen). Safe to call with a raw index found by comparing
/// `best_cards[i].rank` directly across entries confined to one suit: two
/// touching runs held by one hand in one suit are disjoint, ordered rank
/// intervals, so ordering by representative rank already agrees with
/// ordering by true lowest rank -- only the *value* returned for the
/// winning index needs resolving, not the search that finds it. This does
/// not hold across different suits (see `discard_keep_winners`, the one
/// rule that compares candidates from different suits against each other),
/// where each candidate's true rank must be resolved before comparing.
auto true_lowest_rank(std::vector<Card> const& best_cards, FutureTricks const& fut, std::size_t index) -> int;

}  // namespace dds::belief_evaluation
