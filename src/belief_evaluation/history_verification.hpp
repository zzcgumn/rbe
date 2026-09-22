#pragma once

#include <api/dds_data_types.hpp>

namespace dds::belief_evaluation
{

/// Whether a supplied play history belongs to a supplied root, and if not,
/// which check caught it. `Consistent` is the only acceptance value; every
/// other names a distinct, independently testable cause. `verify_history`
/// documents the order the checks run in.
enum class HistoryVerdict
{
    Consistent,

    /// Malformed input: `history.number` outside `[0, 52]`,
    /// `opening_leader` or `declarer` outside `[0, DDS_HANDS)`, or a played
    /// card's suit outside `[0, DDS_SUITS)` or rank outside `[2, 14]`.
    InvalidInput,

    /// The same (suit, rank) appears twice among `history`'s played cards.
    DuplicatedCard,

    /// A card `history` says was played is still held by some hand in
    /// `root`.
    CardPlayedAndHeld,

    /// Neither `history` nor any hand in `root` accounts for some card of
    /// the deck.
    MissingCard,

    /// The number of cards `history` leaves trailing after its last
    /// complete trick does not match how many cards `root` shows played to
    /// its own trick in progress.
    TrickLengthMismatch,

    /// The trailing cards' *count* matches `root`'s trick in progress, but
    /// the cards themselves — or their order — do not.
    TrailingTrickMismatch,

    /// `opening_leader` is wrong: replaying `history`'s complete tricks
    /// from it lands on a seat that disagrees with `root.first`.
    LeaderMismatch,

    /// `history` derives declarer or dummy void in a suit `root` shows that
    /// seat still holding. A hard contradiction, since both holdings are
    /// exact in `root` — unlike a defender's, whose split `root` does not
    /// itself commit to.
    VoidContradiction,
};

/// Checks `history` (played from `opening_leader`) against `root`, for
/// `declarer`. A rejection is *reported*, not asserted: `history` is caller
/// input, like an illegal card from a callback.
///
/// The checks run in this order, the first failure reported:
///
/// 0. **The shape of the input** (`InvalidInput`) — before any of it is
///    used to index anything, since every check below does exactly that.
/// 1. **The card partition**: every played card and every card `root`
///    still shows held must together be the 52 distinct cards of a deck
///    (`DuplicatedCard`, `CardPlayedAndHeld`, `MissingCard`). Cards of the
///    trick in progress belong to `history`'s side of the partition, since
///    `play()` has already removed them from `root`'s `remainCards`.
/// 2. **The trailing trick** (`TrickLengthMismatch`, then
///    `TrailingTrickMismatch`) — catching what the partition cannot: the
///    same 52 cards in a sequence that disagrees with `root` about what is
///    in progress.
/// 3. **The leader** (`LeaderMismatch`). **Load-bearing, not redundant**:
///    neither check above depends on *which seat* played which card, so a
///    history with the right cards in the right order but every seat
///    shifted by the same rotation passes both — and would otherwise reach
///    `derive_voids` and force a suit onto the wrong defender. Since every
///    seat `derive_voids` attributes a card to is that same replay offset
///    by a fixed rotation, checking this one seat is equivalent to
///    checking `opening_leader` itself.
/// 4. **The free cross-check** (`VoidContradiction`), costing nothing
///    beyond what step 1 computed: it catches a *non-uniform* seat error,
///    cards reattributed in a way that is not a single fixed rotation, so
///    step 3 does not catch it either.
auto verify_history(Deal const& root, int declarer, PlayTraceBin const& history, int opening_leader)
    -> HistoryVerdict;

}  // namespace dds::belief_evaluation
