#pragma once

#include <array>

#include <api/dds_data_types.hpp>

namespace dds::belief_evaluation
{

/// For each suit, about how many tricks the side of `defender_seat` (that
/// seat and its partner) can cash by leading the suit again and again. It
/// assumes either defender can lead each round: entries are never a
/// problem. The opponents are the other two seats. Declarer and dummy are
/// not distinguished.
///
/// **Never an overestimate; may underestimate.** This is a simple
/// top-tricks count, not a double-dummy suit solver.
///
/// The method:
/// 1. Take the side's `cap` highest cards, where `cap` is the length of
///    the longer defending hand. Each round uses a card from each hand.
/// 2. Count a run of those cards, highest first. A card wins its round when
///    the opponents can no longer follow, or when it beats the highest
///    card either opponent holds. Every hand with cards in the suit plays
///    one each round, so an opponent's hand is empty after as many rounds
///    as it had cards.
/// 3. Stop at the first card that does neither. Once an opponent wins a
///    round, the lead passes to them, and nothing later is certain.
///
/// Step 2 compares against one fixed highest card, not the real play
/// order. So some positions count lower than the true answer. Never
/// higher.
///
/// `trump` is `DDS_NOTRUMP` for no trumps (see `defender_heuristic.hpp`).
/// When `suit` is not `trump` and an opponent holds a trump, the count is
/// capped at that opponent's length in `suit`. After that, they can ruff.
///
/// Reads `layout.remainCards` directly. `RankMap` holds only the combined
/// outstanding cards, not each seat's holding.
auto suit_top_tricks(Deal const& layout, int defender_seat, int trump) -> std::array<int, 4>;

}  // namespace dds::belief_evaluation
