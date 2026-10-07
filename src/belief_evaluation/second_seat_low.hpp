#pragma once

#include <belief_evaluation/defender_heuristic.hpp>

namespace dds::belief_evaluation
{

/// delta rule: in second seat, following suit, as a defender, play the
/// lowest double-dummy-optimal card -- unless `randomize_touching_honors`
/// (default true) and that lowest candidate touches another same-suit
/// candidate (per `ctx.fut.equals`, read against `best_cards` under the
/// precondition below), in which case this rule defers so the chain's own
/// fallback spread -- restricted choice -- picks between them instead,
/// *and* that same group is the one `spread()` itself resolves to
/// (`canonical_best(ctx.fut)`, see `spread.hpp`) -- `fut` may hold more
/// than one touching group tied for best score, and `spread()` always
/// picks its own canonical one regardless of which candidate this rule
/// happens to call "lowest". When the two diverge, deferring would
/// randomise the wrong pair (or not randomise at all, if the canonical
/// entry does not touch anything), so this rule returns its own answer
/// directly in that case instead -- the same card
/// `randomize_touching_honors=false` would give.
///
/// **Precondition shared by every rule that reads `ctx.fut.equals`
/// against `best_cards`**: the two must be parallel, i.e.
/// `best_cards[i]` is `Card{ctx.fut.suit[i], ctx.fut.rank[i]}` for every
/// `i`. `HeuristicDefender` (the only production caller) builds both from
/// the same solve this way; a caller assembling a context and a candidate
/// set by hand for a rule that reads `fut` must keep them parallel too.
auto second_seat_low(bool randomize_touching_honors = true) -> DefenderHeuristic;

}  // namespace dds::belief_evaluation
