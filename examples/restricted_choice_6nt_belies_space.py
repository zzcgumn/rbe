"""Restricted choice 6NT: a slam that comes down to a guess the cards
themselves answer differently than it looks.

    North   S J85    H 987   D QJT    C AJT5
    East    S A9642  H AQT3  D AK     C KQ
    South   S Q      H 652   D 98753  C 9764
    West    S KT73   H KJ4   D 642    C 832

West plays 6NT. Eight tricks cash themselves -- the rest of the diamonds,
hearts and clubs -- leaving the four-card-each ending

    North   S J85    D T      C T
    East    S A9642
    South   S Q      D 83     C 74
    West    S KT73           C 8

needing all five remaining tricks: the contract's only loser, if there is
one, is in spades. Declarer and dummy hold A K T 9 6 4 2 between them, nine
cards; North and South hold the other four -- Q J 8 5 -- split some way
that is not known.

The plan: lead low from dummy to the king in hand first (safe regardless of
the split -- nothing but the ace beats a king, and the ace is declarer's
own). Then lead low again. Whatever North follows with on *that* trick
settles everything below the two honours -- the eight and the five are the
only other outstanding cards, so this is necessarily the last small card
North can have. If North instead shows an honour here, win with the ace:
there is nothing to read, a defender who still had the other honour behind
it would simply have kept this one back too. If North shows small, the
real decision is on the table -- rise with the ace now, or duck (keeping
the ace, relying on the ten to deal with whatever is left once South's own
card, not yet seen, has settled where the last card is) -- and *that* is a
genuine probability, not a certainty, because a defender dealt both honours
together does not always show the lower one first.

This example reads that probability off the belief space and reports it,
against a bespoke defender (`randomises_queen_jack_in_second_seat`) that
names the 50/50 outright: holding the queen and the jack together, second
seat, it shows either at random; everywhere else it plays low. That makes
the restricted-choice fraction this example reads back a hand-checkable
one rather than whatever a solver's own tie-break happens to produce --
and `DoubleDummyDefender` under `SpreadPolicy.TouchingSequence` agrees with
it exactly here, which the printed output also checks. (Measured against a
defender who never hides a jack behind a queen, the read correctly comes
back certain: see the printed contrast.) It is a real, non-trivial number
here, and still not the same question as which card actually gives the
higher `P_make` over this contract as a whole -- the example reports both
and explains the gap.

Run it with:

    bazelisk run //examples:restricted_choice_6nt
"""

import dds3

import belief_space_local_evaluation as bsle
from belief_space_local_evaluation import Card

from bridge_notation import (
    NOTRUMP,
    SEAT_NAMES,
    WEST,
    SPADES,
    format_card,
    format_denomination,
    format_hand,
    parse_deal,
)
from belief_space_local_evaluation import PlaySequence, legal_cards
from belief_space_local_evaluation.play_sequence import cards_on_trick
from strategies import (
    ACE,
    JACK,
    KING,
    QUEEN,
    TEN,
    double_dummy_defender,
    pick,
)

SEED = 1

# The one piece of state this module carries outside a strategy's own
# arguments: the probability `cash_the_king_then_read_the_beliefs` computes
# at its one belief-dependent node, captured purely for `main()` to report.
# Nothing here feeds back into the card the strategy returns -- see that
# function's own docstring for why a side channel is used instead of
# reasoning the number out a second time afterwards.
_LAST_SMALL_CARD_READING = []


def restricted_choice_6nt() -> PlaySequence:
    """The deal, and the eight tricks that lead to the ending."""
    deal = parse_deal(
        "N: J85.987.QJT.AJT5 "
        "A9642.AQT3.AK.KQ "
        "Q.652.98753.9764 "
        "KT73.KJ4.642.832"
    )
    sequence = PlaySequence(deal, declarer=WEST, trump=NOTRUMP, level=6)

    sequence.play_trick("DQ DA D9 D6")  # North leads the queen of diamonds, won by East's ace.
    sequence.play_trick("H3 H2 HK H7")  # Small heart to the king.
    sequence.play_trick("HJ H8 HT H5")  # Cash the jack of hearts.
    sequence.play_trick("H4 H9 HA H6")  # Heart to the ace.
    sequence.play_trick("HQ D5 D4 C5")  # Cash the heart, discarding a diamond and a club.
    sequence.play_trick("CK C6 C3 CA")  # Knock out the ace of clubs.
    sequence.play_trick("CJ CQ C9 C2")  # Exit with a club.
    sequence.play_trick("DK D7 D2 DJ")  # Cash the king of diamonds.

    return sequence


def cash_the_king_then_read_the_beliefs(state, view):
    """pi: lead low to the king first (fixed -- the ace guarding it is
    declarer's own, so nothing a defender holds can beat it), then read the
    belief space for the one real decision in the suit.

    Declarer and dummy hold A K T 9 6 4 2 in spades; North and South hold
    Q J 8 5 between them. Both missing low cards (the eight and the five)
    are spent by the time North follows to the second round, so whatever
    North shows there is either an honour or North's last possible small
    card -- there is no third case.

    - North shows an honour (queen or jack): win with the ace every time.
      A defender who still held the other honour behind the one just shown
      would, under `DoubleDummyDefender`'s own spreading policy, just as
      readily have shown that one instead -- so nothing here favours
      reading it either way, and the ace can never lose to it regardless.
    - North shows small (their last possible small card): the belief space
      is asked directly whether South -- not yet seen this trick -- still
      guards one of the two honours. Above even odds, rise with the ace;
      at or below, duck (keep the ace, and let the ten, still in hand,
      settle whatever North is left holding on a later round).

    `view.entries` is read, and its result stashed in the module-level
    `_LAST_SMALL_CARD_READING`, only for `main()` to report afterwards --
    not to influence this call's own return value, so the strategy stays a
    pure function of `(state, view)` as the evaluator requires. A second,
    separate evaluate() call, built the ordinary way from scratch, would
    have to re-derive the identical number; capturing it here instead of
    recomputing it is the plainer way to get it into the report.

    The ten is played explicitly, once it is declarer's only remaining
    high card, exactly when nothing yet on the trick beats it -- third
    hand still has to decide without seeing the fourth hand's card, same as
    every other 3rd-hand decision here. Past that, third hand plays the
    cheapest card that still wins rather than its own lowest: the generic
    "always play low" habit used everywhere else in this package is wrong
    specifically here, because letting an uncontested trick stand up over
    our own side's lead strands the losing hand on lead with nothing left
    but a loser in another suit.
    """
    legal = state.legal_cards
    on_trick = state.current_trick

    if not on_trick:
        spades = [c for c in legal if c.suit == SPADES]
        if spades:
            return min(spades, key=lambda c: c.rank)
        return min(legal, key=lambda c: (c.rank, c.suit))

    dummy = (state.declarer + 2) % 4
    if state.trick_leader in (state.declarer, dummy) and state.position_in_trick == 2:
        king = pick(legal, SPADES, KING)
        if king is not None:
            return king

        ace = pick(legal, SPADES, ACE)
        if ace is not None:
            if any(c.suit == SPADES and c.rank >= JACK for c in on_trick):
                return ace

            north = (state.trick_leader + 1) % 4
            south = (state.trick_leader + 3) % 4
            danger = sum(
                entry.posterior for entry in view.entries
                if entry.layout["remain_cards"][south][SPADES] & ((1 << QUEEN) | (1 << JACK)))
            if any(
                entry.layout["remain_cards"][south][SPADES] == 0
                and entry.layout["remain_cards"][north][SPADES] == (1 << JACK)
                for entry in view.entries
            ):
                # This node's layouts include the real one: South already
                # void (their only spade was the queen, round one), North
                # down to the bare jack. Captured by matching the *current*
                # remaining holdings rather than which of North's two small
                # cards came first, because that ordering is itself the
                # defender's own choice, not a fact about the real hand.
                _LAST_SMALL_CARD_READING.append(1.0 - danger)
            if danger > 0.5:
                return ace
            nine = pick(legal, SPADES, 9)
            return nine or min(legal, key=lambda c: (c.rank, c.suit))

        ten = pick(legal, SPADES, TEN)
        if ten is not None:
            if not any(c.suit == SPADES and c.rank > TEN for c in on_trick):
                return ten
            return min(legal, key=lambda c: (c.rank, c.suit))

        current_high = max((c.rank for c in on_trick if c.suit == SPADES), default=-1)
        winners = sorted(
            (c for c in legal if c.suit == SPADES and c.rank > current_high),
            key=lambda c: c.rank)
        if winners:
            return winners[0]

    return min(legal, key=lambda c: (c.rank, c.suit))


def always_rise_with_the_ace(state, view):
    """pi: as `cash_the_king_then_read_the_beliefs`, but never ducks --
    rise with the ace on the second round regardless of what the belief
    space says.

    Measured purely for contrast with the belief-reading declarer above.
    Not because the belief space is wrong about the odds (see the printed
    reading, and the certain case against the lowest-card defender in
    `test_restricted_choice_6nt.py`), but because this slam needs every
    remaining trick: there is no later trick where a kept-back ace gets to
    react to anything, since whichever side wins the second round leads
    the third one too. A probability genuinely in the finesse's favour for
    *that one trick* can still cost tricks overall once nothing is ever
    gained by keeping the ace back and something is always risked by
    doing so.
    """
    del view
    legal = state.legal_cards
    on_trick = state.current_trick

    if not on_trick:
        spades = [c for c in legal if c.suit == SPADES]
        if spades:
            return min(spades, key=lambda c: c.rank)
        return min(legal, key=lambda c: (c.rank, c.suit))

    dummy = (state.declarer + 2) % 4
    if state.trick_leader in (state.declarer, dummy) and state.position_in_trick == 2:
        king = pick(legal, SPADES, KING)
        if king is not None:
            return king

        ace = pick(legal, SPADES, ACE)
        if ace is not None:
            return ace

        ten = pick(legal, SPADES, TEN)
        if ten is not None:
            if not any(c.suit == SPADES and c.rank > TEN for c in on_trick):
                return ten
            return min(legal, key=lambda c: (c.rank, c.suit))

        current_high = max((c.rank for c in on_trick if c.suit == SPADES), default=-1)
        winners = sorted(
            (c for c in legal if c.suit == SPADES and c.rank > current_high),
            key=lambda c: c.rank)
        if winners:
            return winners[0]

    return min(legal, key=lambda c: (c.rank, c.suit))


def randomises_queen_jack_in_second_seat(layout, seat, state):
    """delta: in second seat, a defender holding both the spade queen and
    the spade jack shows either with equal probability; every other
    defender, and every other seat, follows with its lowest legal card.

    "Second seat" means exactly one card is already on the trick -- the
    leader's -- so `seat` is the next to call, before declarer's own
    third-hand decision is made (the decision
    `cash_the_king_then_read_the_beliefs` actually reads the belief space
    for). Read from `layout` rather than `state`, the same way
    `queen_of_spades_when_it_wins` does: a defender strategy is allowed to
    see its own seat's cards and the trick in progress directly, and
    `cards_on_trick`/`layout["first"]` already give trick position without
    re-deriving it from `state`.

    Written for this ending specifically -- "the queen and the jack" names
    two fixed cards, not a general rule for any touching pair -- so that
    the probability `cash_the_king_then_read_the_beliefs` reads back is an
    exact, hand-checkable restricted-choice fraction: a defender dealt the
    bare queen is forced to show it; a defender dealt the queen *with* the
    jack behind it shows the queen only half the time. Unlike
    `DoubleDummyDefender`, which spreads over touching equals according to
    its own solver-driven policy, this one names the 50/50 outright.
    """
    del state  # Conditions on the layout alone, like queen_of_spades_when_it_wins.
    legal = legal_cards(layout, seat)
    on_trick = cards_on_trick(layout)

    if len(on_trick) == 1:
        queen = pick(legal, SPADES, QUEEN)
        jack = pick(legal, SPADES, JACK)
        if queen is not None and jack is not None:
            return [(queen, 0.5), (jack, 0.5)]

    return [(min(legal, key=lambda c: (c.rank, c.suit)), 1.0)]


def main() -> None:
    sequence = restricted_choice_6nt()
    root = sequence.current_deal
    declarer, dummy = sequence.declarer, (sequence.declarer + 2) % 4

    print(f"Contract: {sequence.level}{format_denomination(root['trump'])} "
          f"by {SEAT_NAMES[declarer]}\n")
    print(sequence.format_tricks())
    print(f"\nDeclarer has {sequence.tricks_won_by_declarer} tricks and needs "
          f"{sequence.tricks_needed} more from:\n")

    lho, rho = (declarer + 1) % 4, (declarer + 3) % 4
    pool = [root["remain_cards"][lho][s] | root["remain_cards"][rho][s] for s in range(4)]
    for seat, label in ((dummy, "dummy"), (declarer, "declarer")):
        print(f"{SEAT_NAMES[seat]:>5} ({label:<8}) {format_hand(root['remain_cards'][seat])}")
    print(f"{'N/S':>5} ({'pool':<8}) {format_hand(pool)}")
    print(f"\n{SEAT_NAMES[root['first']]} to play. How the "
          f"{format_card(Card(SPADES, QUEEN))} and the "
          f"{format_card(Card(SPADES, JACK))} split between North and South "
          f"is what the belief space is over.\n")

    record = bsle.PlayRecord(sequence.history, sequence.opening_leader)
    source = bsle.ExhaustiveLayoutSource(root, declarer, SEED, record=record)
    unconstrained = bsle.ExhaustiveLayoutSource(root, declarer, SEED)

    print(f"Belief space, with history: {source.size()} layouts")
    print(f"       ... without history: {unconstrained.size()} layouts")
    if source.size() == unconstrained.size():
        print("       (equal here: every suit shown out of is already exhausted)")
    print()

    defence = randomises_queen_jack_in_second_seat

    _LAST_SMALL_CARD_READING.clear()
    belief_value = evaluate(sequence, source, cash_the_king_then_read_the_beliefs, defence)
    print(f"Cash the king, then read the beliefs:  P_make = {belief_value['p_make']:.4f}")

    if _LAST_SMALL_CARD_READING:
        finesse_probability = _LAST_SMALL_CARD_READING[0]
        print(
            f"\nWhen North follows to the second round with their last "
            f"possible small card, the belief space puts the finesse "
            f"(South does not also guard an honour) right "
            f"{finesse_probability:.0%} of the time -- not a certainty, "
            f"because a defender dealt the queen with the jack behind it "
            f"only shows the queen half the time, exactly the "
            f"restricted-choice asymmetry this number is supposed to "
            f"capture.")

    always_ace_value = evaluate(sequence, source, always_rise_with_the_ace, defence)
    print(f"\nAlways rising with the ace instead:     P_make = {always_ace_value['p_make']:.4f}")
    print(
        "The percentage play for that one trick and the right technique "
        "for a contract that cannot afford any loser are different "
        "questions: whichever side wins the second round leads the third "
        "one too, so a kept-back ace never gets a later trick to react in "
        "-- there is nothing to gain by keeping it, and the finesse's own "
        "miss-rate is pure downside once that is true.")

    ctx = dds3.SolverContext()
    double_dummy_value = evaluate(
        sequence, source, cash_the_king_then_read_the_beliefs, double_dummy_defender(ctx))
    print(
        f"\n`DoubleDummyDefender` under `SpreadPolicy.TouchingSequence` "
        f"agrees with the bespoke 50/50 exactly here: P_make = "
        f"{double_dummy_value['p_make']:.4f}.")


def evaluate(sequence, source, pi, delta, **options) -> dict:
    """Run one evaluation and hand back the value, failing loudly."""
    result = bsle.evaluate(
        sequence.current_deal, sequence.declarer, sequence.tricks_needed,
        source, pi, delta, **options)
    if "error" in result:
        raise SystemExit(f"evaluation failed: {result['error']}")
    return result["by_strategy"][1]  # Keyed by pi's strategy id, always 1 here.


if __name__ == "__main__":
    main()
