#pragma once

#include <belief_evaluation/defender_heuristic.hpp>

namespace dds::belief_evaluation
{

/// delta rule: in second seat, following suit, as a defender, play the
/// lowest double-dummy-optimal card.
///
/// With `randomize_touching_honors` (default true), the rule defers when
/// that card touches another candidate (by `ctx.fut.equals`). The chain's
/// fallback spread then chooses between them: restricted choice. It
/// defers only when both hold:
/// - the touching group is the one `spread()` will pick
///   (`canonical_best(ctx.fut)`). `fut` may hold several tied groups;
/// - `ctx.fallback_policy` is `SpreadPolicy::TouchingSequence`.
///   `AllOptimal` spreads over more than this group.
///
/// Otherwise it returns the lowest card, as with
/// `randomize_touching_honors=false`.
///
/// **Caller obligation:** `DefenderHeuristicChain::select_card()` tries
/// every later rule before the fallback. A later rule that fires here
/// stops the restricted choice. Put `second_seat_low` last among rules
/// that can fire at the same point.
///
/// **Precondition for every rule that reads `ctx.fut.equals` with
/// `best_cards`:** the two are parallel. `best_cards[i]` is
/// `Card{ctx.fut.suit[i], ctx.fut.rank[i]}` for every `i`.
/// `HeuristicDefender` builds them that way. A caller who builds them by
/// hand must too.
auto second_seat_low(bool randomize_touching_honors = true) -> DefenderHeuristic;

}  // namespace dds::belief_evaluation
