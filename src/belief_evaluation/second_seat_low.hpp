#pragma once

#include <belief_evaluation/defender_heuristic.hpp>

namespace dds::belief_evaluation
{

/// delta rule: in second seat, following suit, as a defender, play the
/// lowest double-dummy-optimal card -- unless `randomise_touching_honours`
/// (default true) and that lowest candidate touches another same-suit
/// candidate (per `ctx.fut.equals`, read against `best_cards` under the
/// precondition below), in which case this rule defers so the chain's own
/// fallback spread -- restricted choice -- picks between them instead.
///
/// **Precondition shared by every rule that reads `ctx.fut.equals`
/// against `best_cards`**: the two must be parallel, i.e.
/// `best_cards[i]` is `Card{ctx.fut.suit[i], ctx.fut.rank[i]}` for every
/// `i`. `HeuristicDefender` (the only production caller) builds both from
/// the same solve this way; a caller assembling a context and a candidate
/// set by hand for a rule that reads `fut` must keep them parallel too.
auto second_seat_low(bool randomise_touching_honours = true) -> DefenderHeuristic;

}  // namespace dds::belief_evaluation
