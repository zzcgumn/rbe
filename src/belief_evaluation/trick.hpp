#pragma once

#include <array>
#include <vector>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

// Converts between the two rank bit conventions of dds, for trick play.
// Deal::remainCards uses bit r for absolute rank r. Position::holding,
// legal_plays() and trick_winner() use bit r-2. (rank_map.hpp and
// layout_key.hpp convert for keys.) A stray >> 2 anywhere else is a bug.
constexpr auto to_compacted(unsigned deal_holding) -> unsigned
{
    // Masked to 13 bits, as in make_rank_map(). Nothing stops a Deal from
    // setting a bit above rank 14, and it must not leak into the result.
    constexpr unsigned ThirteenBitMask = 0x1FFFu;
    return (deal_holding >> 2) & ThirteenBitMask;
}

constexpr auto rank_to_bit_position(int absolute_rank) -> int
{
    return absolute_rank - 2;
}

/// The hand (0..3) on play: `first` plus the number of cards already
/// played to the current trick.
auto seat_on_play(Deal const& deal) -> int;

/// The cards `seat` may play next, as one bitmask per suit (bit r =
/// absolute rank r). Follows suit if `seat` can. Uses `legal_plays()`.
auto legal_cards(Deal const& deal, int seat) -> std::array<unsigned, DDS_SUITS>;

/// The cards `seat` may play, as a flat list.
///
/// **The order is part of the contract:** suit ascending, then rank
/// ascending. `EvaluationValue::root_children` uses this order.
auto enumerate_legal_cards(Deal const& deal, int seat) -> std::vector<Card>;

/// The hand (0..3) that wins the current trick when `card` is played as
/// its fourth card. `deal` must have exactly three cards in
/// `currentTrickSuit` and `currentTrickRank`. Uses `trick_winner()`.
auto trick_complete_winner(Deal const& deal, Card const& card) -> int;

/// The `Deal` after the seat on play plays `card`. The card leaves that
/// seat's `remainCards` and joins the current trick. If it completes the
/// trick, the trick is cleared and `first` becomes the winner.
///
/// Pure: returns a new `Deal`. It keeps no trick count; see
/// `ObservationState`.
auto play(Deal const& deal, Card const& card) -> Deal;

}  // namespace dds::belief_evaluation
