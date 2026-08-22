#pragma once

#include <array>

#include <utility/constants.h>

/// A minimal trick-play position for stating the renumbering isomorphism:
/// each hand's outstanding holding per suit, in dds's aggregate bit
/// convention (bit i = the (i+2)-th absolute rank), the trump suit, and the
/// hand on lead for the next trick. Carries no strategy and computes no
/// result — it exists only to let the renumbering isomorphism property be
/// stated and tested.
struct Position
{
    std::array<std::array<unsigned, DDS_SUITS>, DDS_HANDS> holding;
    int trump;   ///< 0..3, or DDS_NOTRUMP
    int leader;  ///< hand (0..3) on lead for the next trick
};

/// Legal cards `hand` may play, one bitmask per suit, given the suit led to
/// the current trick so far (`led_suit`, or -1 if `hand` is leading). A hand
/// must follow `led_suit` if it holds any card there; otherwise every held
/// card is legal.
auto legal_plays(Position const& position, int hand, int led_suit)
    -> std::array<unsigned, DDS_SUITS>;

/// The hand (0..3) that wins a trick of four plays, each given as
/// `(suit, bit_position)`, indexed by the order played starting from
/// `leader`. Highest trump wins if any trump was played; otherwise highest
/// card of the led suit.
auto trick_winner(
    int trump,
    int leader,
    std::array<int, 4> const& suit_played,
    std::array<int, 4> const& bit_played) -> int;

/// Removes one card from `hand`'s holding in `position`, in place.
auto play_card(Position& position, int hand, int suit, int bit_position) -> void;
