"""Guess 6NT: a slam that comes down to locating one queen.

    North   S KJ7    H QJ     D QT65   C AJ42
    South   S T9     H AKT    D AK98   C KQ73

South plays 6NT on a club lead. Nine tricks cash themselves -- four clubs,
four diamonds, one heart -- and then declarer is in the four-card ending

    North   S KJ7    H Q
    South   S T9     H AT

needing three more. The heart ace is one. The other two have to come from
spades, against East and West holding S AQ6542 and H 92 between them (six
spades and two hearts -- the eight cards the 70 layouts split), and
which defender holds the spade queen is not known. That is the whole
problem, and it is exactly the kind of problem belief-space evaluation is
for: the answer is not a card, it is a probability over the layouts still
consistent with the play so far.

This example gets to that ending, builds the belief space, and evaluates it
twice: once with both sides playing the lowest card they are allowed to, and
once with the defenders replaced by the double-dummy solver. Declarer plays
low in both, so the difference between the two numbers is the defenders'
doing and nothing else.

Run it with:

    bazelisk run //examples:guess_6nt_belief_space
"""

import dds3

import belief_space_local_evaluation as bsle
from belief_space_local_evaluation import Card

from play_sequence import cards_on_trick, legal_cards, seat_on_play

from bridge_notation import (
    NOTRUMP,
    SEAT_NAMES,
    NORTH,
    SOUTH,
    SPADES,
    HEARTS,
    DIAMONDS,
    CLUBS,
    format_card,
    format_deal,
    format_denomination,
    parse_deal,
    ranks_in
)
from play_sequence import PlaySequence
from strategies import (
    double_dummy_defender,
    lowest_eligible_declarer,
    lowest_eligible_defender,
)

SEED = 1


def guess_6nt() -> PlaySequence:
    """The deal, and the nine tricks that lead to the ending."""
    deal = parse_deal(
        "N: KJ7.QJ.QT65.AJ42 A653.86432.J2.T9 T9.AKT.AK98.KQ73 Q842.975.743.865")
    sequence = PlaySequence(deal, declarer=SOUTH, trump=NOTRUMP, level=6)

    sequence.play_trick("C6 C4 C9 CQ")  # West leads a club, won by South.
    sequence.play_trick("CK C8 C2 CT")  # South cashes the club king.
    sequence.play_trick("C7 C5 CA H8")  # Club to dummy's ace; East pitches a heart.
    sequence.play_trick("CJ S3 C3 S8")  # Dummy's club jack; both defenders pitch spades.
    sequence.play_trick("D6 D2 DK D3")  # Diamond to the king.
    sequence.play_trick("DA D4 D5 DJ")  # Cash the diamond ace.
    sequence.play_trick("D9 D7 DQ H4")  # Diamond to the queen; East pitches a heart.
    sequence.play_trick("DT H3 D8 H5")  # The last diamond; both defenders pitch hearts.
    sequence.play_trick("HJ H6 HK H7")  # A heart to the king, and South is on play.

    return sequence

ACE, TEN, QUEEN, KING = 14, 10, 12, 13


def _pick(legal, suit, rank):
    """That card, if it is one of the legal ones -- else None."""
    for card in legal:
        if card.suit == suit and card.rank == rank:
            return card
    return None


def cash_two_hearts_and_play_a_spade(state, view):
    """pi: cash the heart ace, then the heart ten, then play a spade,
    covering the queen if it appears.

    Chosen from `legal_cards`, which has already applied the follow-suit
    rule, so every branch below is a preference among cards that are legal
    rather than a claim that the preferred card is available. Falling
    through to "lowest" is therefore always safe.

    The same function plays for declarer and for dummy -- pi is called for
    whichever of the two is on play -- so it is written in terms of what is
    on the trick, not in terms of the seat.
    """
    del view  # This line is fixed in advance; it does not read the beliefs.
    deal = state.known_holdings
    seat = seat_on_play(deal)
    legal = legal_cards(deal, seat)
    on_trick = cards_on_trick(deal)

    # An empty trick means *we* are leading. Not the same test as
    # current_trick_suit[0] == SPADES, which is also true of an empty trick,
    # spades being suit 0 -- see cards_on_trick's own docstring.
    if not on_trick:
        return (_pick(legal, HEARTS, ACE)
                or _pick(legal, HEARTS, TEN)
                or min(legal, key=lambda card: (card.rank, card.suit)))

    # Following. Cover the queen: if the spade queen is already on this
    # trick and the king is still ours to play, put it up.
    queen_played = any(c.suit == SPADES and c.rank == QUEEN for c in on_trick)
    if queen_played:
        king = _pick(legal, SPADES, KING)
        if king is not None:
            return king

    return min(legal, key=lambda card: (card.rank, card.suit))


def main() -> None:
    sequence = guess_6nt()
    root = sequence.current_deal

    print(f"Contract: {sequence.level}{format_denomination(root['trump'])} "
          f"by {SEAT_NAMES[sequence.declarer]}\n")
    print(sequence.format_tricks())
    print(f"\nDeclarer has {sequence.tricks_won_by_declarer} tricks and needs "
          f"{sequence.tricks_needed} more from:\n")
    print(format_deal(root))
    print(f"\n{SEAT_NAMES[root['first']]} to play.\n")

    # The play history is what makes this the *right* belief space rather than
    # merely a plausible one: it rules out the layouts in which a defender
    # holds a suit they have already shown out of. Omitting it is not an error
    # and raises nothing -- it silently answers a different question. See "The
    # play history: needed, not merely optional" in
    # docs/belief_space_local_evaluation.md.
    source = bsle.ExhaustiveLayoutSource(
        root, sequence.declarer, SEED,
        history=sequence.history, opening_leader=sequence.opening_leader)
    unconstrained = bsle.ExhaustiveLayoutSource(root, sequence.declarer, SEED)

    print(f"History verdict:            {source.history_verdict()}")
    print(f"Belief space, with history: {source.size()} layouts")
    print(f"       ... without history: {unconstrained.size()} layouts")
    if source.size() == unconstrained.size():
        # True here, and worth printing rather than quietly agreeing: both
        # defenders showed out of clubs and diamonds, but no club or diamond
        # is left to hold, so on this root the history rules out nothing the
        # root did not already. That is a property of this ending, not a
        # reason to stop passing the history -- move the root one trick
        # earlier and the two numbers part company.
        print("       (equal here: every suit shown out of is already exhausted)")
    print()

    # Declarer plays low throughout, in both runs below. Holding pi fixed is
    # what makes the two numbers comparable: every difference between them is
    # the defenders' doing.
    playing_low = evaluate(
        sequence, source, cash_two_hearts_and_play_a_spade, lowest_eligible_defender)

    # The same position against defenders who can see through the backs of
    # the cards: DoubleDummyDefender solves each layout and spreads its
    # probability over the tied-for-best cards. Remember what it optimises --
    # tricks, not the contract -- so this is not "best defence against 6NT".
    ctx = dds3.SolverContext()
    double_dummy = evaluate(
        sequence, source, cash_two_hearts_and_play_a_spade, double_dummy_defender(ctx))

    report("Defenders play low", playing_low)
    report("Defenders play double dummy", double_dummy)

    print("Cost of double-dummy defence: "
          f"{double_dummy['p_make'] - playing_low['p_make']:+.4f}")


def evaluate(sequence, source, pi, delta, **options) -> dict:
    """Run one evaluation and hand back the value, failing loudly."""
    result = bsle.evaluate(
        sequence.current_deal, sequence.declarer, sequence.tricks_needed,
        source, pi, delta, **options)
    if "error" in result:
        raise SystemExit(f"evaluation failed: {result['error']}")
    return result["by_strategy"][1]  # Keyed by pi's strategy id, always 1 here.


def report(heading: str, value: dict) -> None:
    print(f"{heading}: P_make = {value['p_make']:.4f}")

    # At a declarer root these are *alternatives*, not a partition: each is
    # what the contract is worth if declarer plays that card now and follows
    # the same lowest-card rule afterwards. They do not sum to P_make, and
    # P_make is whichever one pi actually chose.
    for card, p_make in sorted(value["root_children"], key=lambda entry: -entry[1]):
        print(f"    lead {format_card(card)}   {p_make:.4f}")
    print()


if __name__ == "__main__":
    main()
