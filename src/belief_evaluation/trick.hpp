#pragma once

#include <array>

#include <api/dll.h>
#include <utility/constants.h>

#include <belief_evaluation/types.hpp>

// The only two places in the module that convert between dds's two rank bit
// conventions: Deal::remainCards sets bit r for absolute rank r, while
// Position::holding, legal_plays() and trick_winner() use bit r-2 (rank_map.hpp
// and layout_key.hpp cross the same boundary for the aggregate/index side; this
// is the trick-mechanics side). A stray >> 2 anywhere else in this module is a
// bug, not a local idiom — it belongs here or nowhere.
constexpr auto to_compacted(unsigned deal_holding) -> unsigned
{
    // Masked to 13 significant bits for the same reason make_rank_map()
    // masks its own >> 2: a well-formed Deal never sets a remainCards bit
    // above rank 14, but nothing in the type enforces that, and an
    // unmasked stray bit here would leak past the 13 ranks a suit can
    // hold into whatever a caller does with the result next.
    constexpr unsigned ThirteenBitMask = 0x1FFFu;
    return (deal_holding >> 2) & ThirteenBitMask;
}

constexpr auto rank_to_bit_position(int absolute_rank) -> int
{
    return absolute_rank - 2;
}

/// The hand (0..3) on play: `first` advanced by however many cards have
/// already been played to the trick in progress.
auto seat_on_play(Deal const& deal) -> int;

/// The cards `seat` may legally play next, one bitmask per suit in Deal's own
/// bit convention (bit r = absolute rank r), honouring the suit led to the
/// trick in progress if `seat` holds any card of it. Delegates to
/// `legal_plays()` rather than re-deriving the follow-suit rule.
auto legal_cards(Deal const& deal, int seat) -> std::array<unsigned, DDS_SUITS>;

/// The hand (0..3) that wins the trick in progress in `deal` once `card` is
/// played as its fourth card. `deal` must already carry exactly three played
/// cards in `currentTrickSuit` / `currentTrickRank`. Delegates to
/// `trick_winner()`.
auto trick_complete_winner(Deal const& deal, Card const& card) -> int;

/// The `Deal` after `seat_on_play(deal)` plays `card`: removed from that
/// seat's `remainCards`, and either appended to the trick in progress, or —
/// when `card` completes the trick — the trick is resolved, `currentTrick*`
/// is cleared and `first` becomes the winner. Pure: `deal` is unchanged and a
/// new `Deal` is returned. Carries no trick counter; who won and how many
/// tricks that makes is the caller's business (see `ObservationState`).
auto play(Deal const& deal, Card const& card) -> Deal;
