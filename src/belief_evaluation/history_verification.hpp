#pragma once

#include <api/dds_data_types.hpp>

namespace dds::belief_evaluation
{

/// Whether a supplied play history actually belongs to a supplied root, and
/// if not, which check caught it. `Consistent` is the only acceptance value;
/// every other value names a distinct, independently testable rejection
/// cause -- see `verify_history`'s own doxygen for what each one checks.
enum class HistoryVerdict
{
    Consistent,

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
    /// the cards themselves -- or their order -- do not.
    TrailingTrickMismatch,

    /// `history` derives declarer or dummy void in a suit `root` shows that
    /// seat still holding. Declarer's and dummy's holdings are exact in
    /// `root`, so this is a hard contradiction rather than a matter of
    /// interpretation -- unlike a defender's, whose split `root` does not
    /// itself commit to (see `DefenderPool`'s own doxygen).
    VoidContradiction,
};

/// Checks `history` (played from `opening_leader`) against `root`, for
/// `declarer` (dummy is `(declarer + 2) % DDS_HANDS`). A rejection is
/// *reported*, not asserted: `history` is caller input, exactly like a
/// declarer's or defender's illegal card, and this module's posture is that
/// caller input is reported while an internal invariant failure asserts.
///
/// The checks run in this order, the first failure reported and the rest
/// left unevaluated:
///
/// 1. **The card partition.** Every played card and every card `root` still
///    shows held must together be exactly the 52 distinct cards of a deck --
///    no duplicate among the played cards (`DuplicatedCard`), no card both
///    played and still held (`CardPlayedAndHeld`), and nothing left over on
///    either side once both are accounted for (`MissingCard`). Cards of the
///    trick in progress are already removed from `root`'s own `remainCards`
///    by the time a root is built (see `play()`), so they belong to
///    `history`'s side of the partition and not `root`'s -- confirmed
///    against `play()`'s own behaviour, not assumed.
/// 2. **The trailing trick.** `root`'s own trick in progress (its
///    `currentTrickSuit`/`currentTrickRank`, 0 to 3 cards) must equal
///    `history`'s own trailing cards, in order -- first their *count*
///    (`TrickLengthMismatch` if not), then the cards themselves
///    (`TrailingTrickMismatch` if the count matches but the cards or their
///    order do not). This catches what the partition check above cannot: the
///    same 52 cards, played in a sequence that disagrees with `root` about
///    what is currently in progress.
/// 3. **The free cross-check.** `derive_voids(history, opening_leader,
///    root.trump)` must not put declarer or dummy void in a suit `root`
///    shows them holding. This costs nothing beyond what step 1 already
///    computed and catches an order error that happens to preserve the card
///    partition -- exactly the case steps 1 and 2 between them might let
///    through.
auto verify_history(Deal const& root, int declarer, PlayTraceBin const& history, int opening_leader)
    -> HistoryVerdict;

}  // namespace dds::belief_evaluation
