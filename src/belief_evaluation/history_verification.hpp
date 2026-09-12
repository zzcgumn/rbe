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

    /// `history.number` is outside `[0, 52]` (`PlayTraceBin::suit`/`rank`'s
    /// own 52-element bound) or `opening_leader` is outside
    /// `[0, DDS_HANDS)`. Checked first, and before either array is ever
    /// indexed by it: `history` is caller input like any other value this
    /// function checks, and a malformed shape is reported the same way a
    /// malformed *content* is, not left to `derive_voids`'s own asserted
    /// fallback (see that function's own doxygen) to catch on this
    /// function's behalf.
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
    /// the cards themselves -- or their order -- do not.
    TrailingTrickMismatch,

    /// `opening_leader` is wrong. Replaying every complete trick in
    /// `history` from `opening_leader` (the same trick arithmetic
    /// `derive_voids` uses internally) gives a seat for the trick
    /// containing the trailing cards that disagrees with `root.first`.
    /// Since every seat `derive_voids` ever attributes a card to is that
    /// same replay offset by a fixed rotation from `opening_leader`, this
    /// single check is equivalent to checking `opening_leader` itself:
    /// a wrong one rotates every attribution by the same non-zero amount,
    /// so it can never coincidentally land back on the right seat here.
    /// Neither the card partition nor the trailing cards' own identity
    /// depends on which seat played which card, so this is the one check
    /// that catches a history whose cards and order are both right but
    /// whose seats are not -- exactly the class of error that would
    /// otherwise reach `derive_voids` and silently force a suit onto the
    /// wrong defender.
    LeaderMismatch,

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
/// 0. **The shape of the input itself.** `history.number` must be in
///    `[0, 52]` and `opening_leader` in `[0, DDS_HANDS)` (`InvalidInput` if
///    not) -- checked before either is used to index anything, since every
///    check below does exactly that.
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
/// 3. **The leader.** Replaying `history`'s complete tricks from
///    `opening_leader` must land on `root.first` as the seat leading the
///    trick the trailing cards belong to (`LeaderMismatch` if not). Neither
///    of the two checks above depends on *which seat* played which card --
///    only on which cards, in what order -- so a history with the right 52
///    cards in the right sequence but attributed to the wrong seats throughout
///    (every card shifted by the same fixed rotation, since that is the only
///    way a wrong `opening_leader` can go wrong) passes both of them. This is
///    the one check that catches it.
/// 4. **The free cross-check.** `derive_voids(history, opening_leader,
///    root.trump)` must not put declarer or dummy void in a suit `root`
///    shows them holding. This costs nothing beyond what step 1 already
///    computed and catches a *non-uniform* seat error -- cards reattributed
///    among seats in a way that is not a single fixed rotation, so step 3
///    above does not catch it either.
auto verify_history(Deal const& root, int declarer, PlayTraceBin const& history, int opening_leader)
    -> HistoryVerdict;

}  // namespace dds::belief_evaluation
