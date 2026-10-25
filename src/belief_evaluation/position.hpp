#pragma once

#include <array>

#include <api/dds_constants.hpp>

namespace dds::belief_evaluation
{

/// A minimal trick-play position, used only to state and test the
/// renumbering isomorphism. It holds each hand's outstanding cards per
/// suit (bit i = absolute rank i + 2), the trump suit, and the hand on
/// lead to the next trick.
struct Position
{
    std::array<std::array<unsigned, DDS_SUITS>, DDS_HANDS> holding;
    int trump;   ///< 0..3, or DDS_NOTRUMP
    int leader;  ///< hand (0..3) on lead for the next trick
};

/// The cards `hand` may play, one bitmask per suit. `led_suit` is the suit
/// led to the current trick, or -1 if `hand` leads. A hand must follow
/// `led_suit` if it can; otherwise every held card is legal.
auto legal_plays(Position const& position, int hand, int led_suit)
    -> std::array<unsigned, DDS_SUITS>;

/// The hand (0..3) that wins a trick of four plays. Each play is
/// `(suit, bit_position)`, in order from `leader`. The highest trump wins
/// if any trump was played; otherwise the highest card of the led suit.
auto trick_winner(
    int trump,
    int leader,
    std::array<int, 4> const& suit_played,
    std::array<int, 4> const& bit_played) -> int;

/// Removes one card from `hand`'s holding in `position`, in place.
auto play_card(Position& position, int hand, int suit, int bit_position) -> void;

}  // namespace dds::belief_evaluation
