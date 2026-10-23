#pragma once

#include <array>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

namespace dds::belief_evaluation
{

/// For each seat (0..3), the suits it has shown out of in `history`.
/// `[seat][suit]` is true once the play proves the seat has no more cards
/// of `suit`.
using VoidsBySeat = std::array<std::array<bool, DDS_SUITS>, DDS_HANDS>;

/// Derives `VoidsBySeat` from `history` alone. A seat that plays a card of
/// another suit than the one led is void in the led suit. A discard and a
/// ruff count the same. Trick winners, needed to know who leads next, come
/// from `trump`, `opening_leader` and the cards, as in `trick_winner`.
///
/// `history` may end mid-trick. The trailing cards show voids too.
///
/// **Never rejects `history`.** A history that does not belong to a root
/// gives wrong voids for it. `verify_history` catches that.
///
/// Malformed input is handled in two ways:
/// - `history.number` outside `[0, 52]` or `opening_leader` outside
///   `[0, DDS_HANDS)`: asserted and clamped.
/// - A malformed card: silently replaced by suit 0, rank 2, before it is
///   used as an index. Not asserted, as in `RankMap::to_relative`.
///
/// Declarer's and dummy's voids are derived too. Only defender voids narrow
/// the enumeration, but `verify_history` uses all four.
auto derive_voids(PlayTraceBin const& history, int opening_leader, int trump) -> VoidsBySeat;

}  // namespace dds::belief_evaluation
