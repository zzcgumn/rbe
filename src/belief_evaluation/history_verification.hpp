#pragma once

#include <api/dds_data_types.hpp>

namespace dds::belief_evaluation
{

/// Whether a play history belongs to a root and, if not, which check
/// failed. `Consistent` is the only acceptance. `verify_history` gives the
/// order of the checks.
enum class HistoryVerdict
{
    Consistent,

    /// Malformed input: `history.number` outside `[0, 52]`,
    /// `opening_leader` or `declarer` outside `[0, DDS_HANDS)`, or a played
    /// card with suit outside `[0, DDS_SUITS)` or rank outside `[2, 14]`.
    InvalidInput,

    /// The same (suit, rank) is played twice in `history`.
    DuplicatedCard,

    /// A card played in `history` is still held by a hand in `root`.
    CardPlayedAndHeld,

    /// A card of the deck is neither in `history` nor in any hand in
    /// `root`.
    MissingCard,

    /// The number of cards after the last complete trick of `history`
    /// differs from the number played to the current trick of `root`.
    TrickLengthMismatch,

    /// The count of trailing cards matches the current trick of `root`, but
    /// the cards or their order do not.
    TrailingTrickMismatch,

    /// `opening_leader` is wrong: replaying the complete tricks from it
    /// gives a leader other than `root.first`.
    LeaderMismatch,

    /// `history` shows declarer or dummy void in a suit that `root` shows
    /// them holding. Their holdings are exact in `root`, so this is a hard
    /// contradiction.
    VoidContradiction,
};

/// Checks `history`, played from `opening_leader`, against `root` for
/// `declarer`. A failure is *reported*, not asserted: `history` is caller
/// input.
///
/// The checks run in this order. The first failure is reported.
///
/// 0. **Input shape** (`InvalidInput`), before anything is used as an
///    index.
/// 1. **The card partition:** the played cards and the cards still held
///    in `root` must be the 52 distinct cards of a deck (`DuplicatedCard`,
///    `CardPlayedAndHeld`, `MissingCard`). Cards in the current trick
///    count as played: `play()` has removed them from `remainCards`.
/// 2. **The trailing trick** (`TrickLengthMismatch`, then
///    `TrailingTrickMismatch`). It catches the right 52 cards in a sequence
///    that disagrees with the current trick of `root`.
/// 3. **The leader** (`LeaderMismatch`). **Not redundant:** the checks
///    above ignore which seat played each card. A history with every seat
///    rotated by the same amount passes them, and `derive_voids` would
///    then give a void to the wrong defender.
/// 4. **Voids** (`VoidContradiction`), at no extra cost. It catches seat
///    errors that are not a single rotation.
auto verify_history(Deal const& root, int declarer, PlayTraceBin const& history, int opening_leader)
    -> HistoryVerdict;

}  // namespace dds::belief_evaluation
