"""Restricted choice 6NT: a slam that turns on one guess in spades.

    North   S J85    H 987   D QJT    C AJT5
    East    S A9642  H AQT3  D AK     C KQ
    South   S Q      H 652   D 98753  C 9764
    West    S KT73   H KJ4   D 642    C 832

West plays 6NT. After eight tricks in diamonds, hearts and clubs, this
ending is left:

    North   S J85    D T      C T
    East    S A9642
    South   S Q      D 83     C 74
    West    S KT73           C 8

Declarer needs all five tricks. The only possible loser is in spades.
Declarer and dummy hold A K T 9 7 6 4 3 2. North and South hold Q J 8 5,
split in an unknown way.

**The plan:**

1. Lead low from dummy to the king.
2. Lead the higher of declarer's two small spades. Keep the lower one as
   a spare (see `_lead_spade_or_the_lone_club`).
3. Act on North's card in the second round:
   - **An honour:** win with the ace. There is nothing to read.
   - **A small spade:** this is North's last small card. Read the belief
     space. Rise with the ace, or duck and rely on the ten. Both are
     risky: South plays to this same trick and can win it.
     A singleton honour with South also lands here. South had to show it
     in round one, and the two small cards in North then read like a 2-2
     break.
   - **A discard, because North is void:** always duck. The belief space
     says South certainly holds an honour. That is a fact about South's
     length, not a reason to rise.

The void branch covers two layouts with different outcomes:

- **South holds the queen and the jack**, with one or two small cards.
  The contract cannot make. By round three the ace and king are gone,
  and South's last honour beats the ten. Ducking loses no more than
  rising.
- **North's singleton is an honour**, and South holds the other honour
  and both small cards. Step 1 already lost this layout. Leading low to
  the king is a bet that South, not North, holds a dangerous singleton
  honour. The plan never reads the belief space for that bet. A
  double-dummy solver makes all five tricks by leading the ace first.
  After the king lead, two of five tricks are already lost. Ducking still
  saves more than rising (see `always_rise_with_the_ace`).

**The defenders.** Each is a fixed rule, so each number can be checked by
hand:

- `randomises_queen_jack_in_second_seat`: in second seat with the queen
  and the jack, shows either at random. Otherwise plays low. This is the
  restricted-choice 50/50.
- `always_shows_the_queen_from_qj_in_second_seat`: always shows the queen
  from that holding. Showing the queen then tells declarer nothing.
- A defender who always plays the lowest legal card.
- `DoubleDummyDefender`.

**The results.**

- Against `DoubleDummyDefender`, the finesse reading in the small-spade
  branch matches the 50/50 defender: 60%. But the overall `P_make` is
  lower.
- The reason: always ducking when North is void pays off only against a
  defender who sheds small cards lowest first. The other three defenders
  do. `DoubleDummyDefender` can release the key card in whichever round
  beats the plan, and here it does. Ducking never costs anything against
  it. It just gains nothing.
- Reading the belief space beats always rising with the ace against every
  defender here. The margin depends on the defender.

Run it with:

    bazelisk run //examples:restricted_choice_6nt
"""

import time
import types

import dds3

import belief_space_local_evaluation as bsle
from belief_space_local_evaluation import Card

from bridge_notation import (
    EAST,
    HEARTS,
    NORTH,
    NOTRUMP,
    SEAT_NAMES,
    SOUTH,
    WEST,
    SPADES,
    format_card,
    format_denomination,
    format_hand,
    holding,
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
    brute_force_declarer,
    double_dummy_defender,
    heuristic_defender,
    lowest_eligible_defender,
    pick,
)

SEED = 1

# The card cash_the_king_then_read_the_beliefs ducks with. It beats North's
# small cards (the eight and the five) and keeps the ace back.
NINE = 9


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
    sequence.play_trick("HQ D5 D4 C5")  # Cash the heart, discarding two diamonds and a club.
    sequence.play_trick("CK C6 C3 CA")  # Knock out the ace of clubs.
    sequence.play_trick("CJ CQ C9 C2")  # Exit with a club.
    sequence.play_trick("DK D7 D2 DJ")  # Cash the king of diamonds.

    return sequence


def _lead_spade_or_the_lone_club(legal, state):
    """The lead for both declarers, at every trick they lead. Prefer a
    spade to the lone club. Within spades:

    - **The king is still unplayed** (round one): lead low. No defender's
      card beats the king. But this is a bet that South, not North, holds
      a dangerous singleton honour. The bet is never read from the belief
      space. See `cash_the_king_then_read_the_beliefs` for the layout
      where it loses.
    - **The king is gone and the ace is held:** lead the ace. After round
      one, keeping the ace back protects nothing. The winner of this
      trick leads the next one anyway.
    - **Otherwise:** lead the higher small card. Keep the lower one as a
      safe spare for `_third_hand_after_the_ace_decision`. If you lead
      the lower card first, the higher one can win a trick by accident.
    """
    spades = [c for c in legal if c.suit == SPADES]
    if not spades:
        return min(legal, key=lambda c: (c.rank, c.suit))

    king_still_held = bool(
        state.known_holdings["remain_cards"][state.declarer][SPADES] & (1 << KING))
    ace = pick(spades, SPADES, ACE)
    if ace is not None and not king_still_held:
        return ace

    ten = pick(spades, SPADES, TEN)
    if ten is not None and len(spades) > 1:
        small = [c for c in spades if c.rank != TEN]
        return max(small, key=lambda c: c.rank)
    return min(spades, key=lambda c: c.rank)


def _third_hand_after_the_ace_decision(legal, on_trick, state, dummy):
    """Third hand for both declarers, after the ace decision. Play the ten
    if it is useful. Otherwise play the cheapest card that wins.

    The ten is useful unless a *defender's* card on the trick beats it:

    - A defender's higher card: the trick is lost. Play low.
    - Our partner's higher card: the trick is won. Unload the ten now. If
      you keep it, a later round leaves it bare, with the ace gone, and it
      loses to the last honour.

    Without the ten, play the cheapest card that wins, not the lowest
    card. If we let this trick go, the losing hand is left on lead with
    only a loser in another suit.
    """
    ten = pick(legal, SPADES, TEN)
    if ten is not None:
        our_sides_cards_on_trick = (
            c.rank for position, c in enumerate(on_trick)
            if c.suit == SPADES and (state.trick_leader + position) % 4 in (state.declarer, dummy))
        if max(our_sides_cards_on_trick, default=-1) > TEN:
            # Our side already wins this trick with the ace. Unload the ten,
            # even if a defender's honour is also on the trick: that honour
            # cannot beat the ace.
            return ten

        beaten_by_a_defender = any(
            c.suit == SPADES and c.rank > TEN
            and (state.trick_leader + position) % 4 not in (state.declarer, dummy)
            for position, c in enumerate(on_trick))
        if not beaten_by_a_defender:
            return ten
        return min(legal, key=lambda c: (c.rank, c.suit))

    current_high = max((c.rank for c in on_trick if c.suit == SPADES), default=-1)
    winners = sorted(
        (c for c in legal if c.suit == SPADES and c.rank > current_high),
        key=lambda c: c.rank)
    if winners:
        return winners[0]
    return min(legal, key=lambda c: (c.rank, c.suit))


def _norths_second_card_is_a_genuine_small_spade(on_trick) -> bool:
    """Whether North's card, `on_trick[1]`, is a real spade rather than a
    discard. The caller has already ruled out an honour.

    North is void when South holds all four missing spades. A void node
    has a very different `danger` from a small-spade node, but it can
    share the same South-honour key. `ace_vs_finesse_readings` uses this
    test to skip void nodes. Without it, the reading would depend on which
    node the evaluator visits first.

    A separate function, so that it can be tested without `evaluate()`.
    """
    return on_trick[1].suit == SPADES


def _danger_south_still_guards_an_honour(state, on_trick, view, dummy):
    """The probability that South still holds the queen or the jack, at
    the one node where `cash_the_king_then_read_the_beliefs` reads the
    belief space. None at every other node.

    That node: the king is gone, the ace is held, and North has not played
    an honour. `on_trick` and `dummy` can be derived from `state`. They are
    parameters only to save recomputing them.

    A pure function of its arguments. Both the strategy and
    `ace_vs_finesse_readings` call it, so the strategy needs no side
    channel to report the value.
    """
    legal = state.legal_cards
    if not (state.trick_leader in (state.declarer, dummy) and state.position_in_trick == 2):
        return None
    if pick(legal, SPADES, KING) is not None:
        return None
    if pick(legal, SPADES, ACE) is None:
        return None
    if any(c.suit == SPADES and c.rank >= JACK for c in on_trick):
        return None

    # `trick_leader + 3` is South only because trick_leader is declarer
    # (West, round two) here. When dummy leads (round one), the king check
    # above has already returned None. If you reorder those checks, this
    # silently computes North's seat instead.
    souths_seat = (state.trick_leader + 3) % 4
    return sum(
        entry.posterior for entry in view.entries
        if entry.layout["remain_cards"][souths_seat][SPADES] & ((1 << QUEEN) | (1 << JACK)))


def cash_the_king_then_read_the_beliefs(state, view):
    """pi: lead low to the king, then read the belief space for the one
    real decision in spades.

    By the time North plays to round two, the eight and the five are the
    only small spades left. So North shows one of three things:

    - **An honour:** win with the ace. The ace cannot lose to it.
    - **North's last small spade** (see
      `_norths_second_card_is_a_genuine_small_spade`): ask the belief
      space whether South still holds an honour
      (`_danger_south_still_guards_an_honour`). Above 0.5, rise with the
      ace. Otherwise duck with the nine, and let the ten deal with North's
      last card in a later round. Both choices are risky, because South
      plays to this same trick.
    - **A discard, because North is void:** always duck. The danger is
      always 1.0 here, but that only reflects South's length. The module
      docstring describes the two layouts in this branch. In one, the
      contract cannot make. In the other, the first lead already lost it.

    A pure function of `(state, view)`. See `ace_vs_finesse_readings` for
    how the belief it reads is reported.
    """
    legal = state.legal_cards
    on_trick = state.current_trick

    if not on_trick:
        return _lead_spade_or_the_lone_club(legal, state)

    dummy = (state.declarer + 2) % 4
    danger = _danger_south_still_guards_an_honour(state, on_trick, view, dummy)
    if danger is not None:
        if danger > 0.5 and _norths_second_card_is_a_genuine_small_spade(on_trick):
            return pick(legal, SPADES, ACE)
        nine = pick(legal, SPADES, NINE)
        return nine or min(legal, key=lambda c: (c.rank, c.suit))

    if state.trick_leader in (state.declarer, dummy) and state.position_in_trick == 2:
        king = pick(legal, SPADES, KING)
        if king is not None:
            return king

        ace = pick(legal, SPADES, ACE)
        if ace is not None:
            # With the ace held, danger is None only if North showed an
            # honour. Win with the ace.
            return ace

        return _third_hand_after_the_ace_decision(legal, on_trick, state, dummy)

    return min(legal, key=lambda c: (c.rank, c.suit))


def always_rise_with_the_ace(state, view):
    """pi: as `cash_the_king_then_read_the_beliefs`, but never duck. Always
    rise with the ace in round two.

    A contrast to the belief-reading declarer. It scores lower overall.

    When North follows with a small spade:

    - Rising wins when South still holds the other honour (South had the
      queen and the jack in a 2-2 break). Ducking loses that trick.
    - Ducking wins the opposite case: South showed a singleton honour, and
      North holds the other. That case is more likely: 60% against 40%,
      the split `ace_vs_finesse_readings` prints.

    When North is void:

    - If South holds the queen and the jack, neither choice makes.
    - If North's singleton was an honour, the first lead already lost the
      contract. Ducking still saves more tricks than rising.
    """
    del view
    legal = state.legal_cards
    on_trick = state.current_trick

    if not on_trick:
        return _lead_spade_or_the_lone_club(legal, state)

    dummy = (state.declarer + 2) % 4
    if state.trick_leader in (state.declarer, dummy) and state.position_in_trick == 2:
        king = pick(legal, SPADES, KING)
        if king is not None:
            return king

        ace = pick(legal, SPADES, ACE)
        if ace is not None:
            return ace

        return _third_hand_after_the_ace_decision(legal, on_trick, state, dummy)

    return min(legal, key=lambda c: (c.rank, c.suit))


def randomises_queen_jack_in_second_seat(layout, seat, state):
    """delta: in second seat on a spade lead, a defender with the spade
    queen and jack shows either one, 50/50. Otherwise play the lowest legal
    card.

    "Second seat" means only the leader's card is on the trick. Declarer
    plays third, and that is the decision
    `cash_the_king_then_read_the_beliefs` reads the belief space for. The
    trick is read from `layout`, like `queen_of_spades_when_it_wins`.

    Written for this ending only. It gives an exact restricted-choice
    fraction that can be checked by hand. A defender with a bare queen must
    show it. A defender with the queen and the jack shows the queen only
    half the time.
    """
    del state  # Uses the layout only.
    legal = legal_cards(layout, seat)
    on_trick = cards_on_trick(layout)

    # The spade check is needed. On a non-spade lead, a void defender can
    # legally discard either honour. Without the check, this would
    # randomise a discard.
    if len(on_trick) == 1 and on_trick[0].suit == SPADES:
        queen = pick(legal, SPADES, QUEEN)
        jack = pick(legal, SPADES, JACK)
        if queen is not None and jack is not None:
            return [(queen, 0.5), (jack, 0.5)]

    return [(min(legal, key=lambda c: (c.rank, c.suit)), 1.0)]


def always_shows_the_queen_from_qj_in_second_seat(layout, seat, state):
    """delta: in second seat on a spade lead, a defender with the spade
    queen and jack always shows the queen. Otherwise play the lowest legal
    card.

    A contrast to `randomises_queen_jack_in_second_seat`. Here a bare queen
    and a queen with the jack play the same card. So showing the queen
    tells declarer nothing, and the posterior is the prior odds of the two
    holdings. Against the 50/50 defender, the posterior favours the bare
    queen.
    """
    del state  # Uses the layout only.
    legal = legal_cards(layout, seat)
    on_trick = cards_on_trick(layout)

    # The spade check is needed. See randomises_queen_jack_in_second_seat.
    if len(on_trick) == 1 and on_trick[0].suit == SPADES:
        queen = pick(legal, SPADES, QUEEN)
        jack = pick(legal, SPADES, JACK)
        if queen is not None and jack is not None:
            return [(queen, 1.0)]

    return [(min(legal, key=lambda c: (c.rank, c.suit)), 1.0)]


def spade_split_frequencies(sequence, source) -> dict:
    """The prior probability at the root of four spade splits: 2-2, and
    South with QJ tight, a singleton jack, or a singleton queen (the real
    deal).

    Uses a throwaway `evaluate()` call. The root `BeliefView` is the same
    for any pi and delta. `probe` reads it on its first call, then plays
    low only so that the call completes. The result is discarded.
    """
    frequencies = {}

    def probe(state, view):
        if not state.current_trick and not frequencies:
            for entry in view.entries:
                souths_spades = entry.layout["remain_cards"][SOUTH][SPADES]
                frequencies["2-2"] = frequencies.get("2-2", 0.0) + (
                    entry.posterior if bin(souths_spades).count("1") == 2 else 0.0)
                frequencies["south QJ tight"] = frequencies.get("south QJ tight", 0.0) + (
                    entry.posterior if souths_spades == (1 << QUEEN) | (1 << JACK) else 0.0)
                frequencies["south singleton J"] = frequencies.get("south singleton J", 0.0) + (
                    entry.posterior if souths_spades == (1 << JACK) else 0.0)
                frequencies["south singleton Q"] = frequencies.get("south singleton Q", 0.0) + (
                    entry.posterior if souths_spades == (1 << QUEEN) else 0.0)
        legal = state.legal_cards
        return min(legal, key=lambda c: (c.rank, c.suit))

    evaluate(sequence, source, probe, randomises_queen_jack_in_second_seat)
    return frequencies


def ace_vs_finesse_readings(sequence, source, delta) -> dict:
    """The probability that the finesse is right, at the one node where
    `cash_the_king_then_read_the_beliefs` reads the belief space. Keyed by
    the honour South showed in round one (`QUEEN` or `JACK`). The
    probability that the ace is right is one minus this.

    Runs one `evaluate()` call. `probe` calls
    `cash_the_king_then_read_the_beliefs` for every card, so it plays the
    same game. It also calls `_danger_south_still_guards_an_honour` and
    stores the result in a local `readings` dict. So the strategy stays a
    pure function, with no module-level state.
    """
    readings = {}

    def probe(state, view):
        card = cash_the_king_then_read_the_beliefs(state, view)

        on_trick = state.current_trick
        if on_trick:
            dummy = (state.declarer + 2) % 4
            danger = _danger_south_still_guards_an_honour(state, on_trick, view, dummy)
            if danger is not None:
                souths_first_card = state.history[1]
                if (souths_first_card.suit == SPADES and souths_first_card.rank in (QUEEN, JACK)
                        and _norths_second_card_is_a_genuine_small_spade(on_trick)):
                    readings.setdefault(souths_first_card.rank, 1.0 - danger)

        return card

    evaluate(sequence, source, probe, delta)
    return readings


def _print_ace_vs_the_finesse(readings_by_souths_honour, label) -> None:
    """Print the probability that the ace or the finesse is right, against
    one defender. One line per branch: South showed the queen, or the jack,
    and North followed with two small spades.

    The two numbers on each line are `danger` and its complement, so they
    sum to one.
    """
    print(f"\nAce vs finesse, against {label}, once North has shown the "
          f"last possible small card:")
    for rank, honour in ((QUEEN, "queen"), (JACK, "jack")):
        finesse = readings_by_souths_honour.get(rank)
        if finesse is None:
            continue
        print(
            f"    {honour} from South, two small from North: "
            f"ace {1.0 - finesse:.0%}  --  finesse {finesse:.0%}")


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

    frequencies = spade_split_frequencies(sequence, source)
    print("Spade splits before any card of this ending, from the belief space:")
    print(f"  2-2 break:               {frequencies['2-2']:.2%}")
    print(f"  South QJ tight:          {frequencies['south QJ tight']:.2%}")
    print(f"  South singleton jack:    {frequencies['south singleton J']:.2%}")
    print(f"  South singleton queen:   {frequencies['south singleton Q']:.2%}  (the real deal)")
    print()

    defence = randomises_queen_jack_in_second_seat

    belief_value = evaluate(sequence, source, cash_the_king_then_read_the_beliefs, defence)
    print(f"Cash the king, then read the beliefs:  P_make = {belief_value['p_make']:.4f}")
    _print_ace_vs_the_finesse(
        ace_vs_finesse_readings(sequence, source, defence), "the bespoke 50/50 defender")
    print(
        "    The two branches agree. When the defender randomises, the "
        "queen and the jack are equally likely to have been forced.")

    always_ace_value = evaluate(sequence, source, always_rise_with_the_ace, defence)
    print(f"\nAlways rising with the ace instead:     P_make = {always_ace_value['p_make']:.4f}")
    print(
        "Lower. When North follows small, rising wins if South still holds "
        "the other honour: the 40% case above. Ducking bets on the more "
        "likely 60% case, where North holds it.\n"
        "When North is void, ducking is no clean win either. If South holds "
        "the queen and the jack, neither choice makes. If North's singleton "
        "was an honour, the first lead (low to the king) already lost the "
        "contract. Leading the ace first would make it. See the module "
        "docstring.")

    ctx = dds3.SolverContext()
    double_dummy_value = evaluate(
        sequence, source, cash_the_king_then_read_the_beliefs, double_dummy_defender(ctx))
    print(
        f"\n`DoubleDummyDefender` under `SpreadPolicy.TouchingSequence` "
        f"instead: P_make = {double_dummy_value['p_make']:.4f}. Its "
        f"reading at the small-spade node matches the 50/50 defender. But "
        f"ducking when North is void gains nothing against it. That gain "
        f"needs a defender who plays small cards lowest first, and a "
        f"double-dummy defender need not.")

    no_tell_value = evaluate(
        sequence, source, cash_the_king_then_read_the_beliefs,
        always_shows_the_queen_from_qj_in_second_seat)
    print(
        f"\nAgainst a defender who always shows the queen from the queen "
        f"and the jack: P_make = {no_tell_value['p_make']:.4f}.")
    _print_ace_vs_the_finesse(
        ace_vs_finesse_readings(sequence, source, always_shows_the_queen_from_qj_in_second_seat),
        "the always-shows-the-queen defender")
    print(
        "    The branches now differ. This defender never shows the jack "
        "from the pair, so a jack must be bare. The queen covers every "
        "holding that could show either card. Reading the beliefs still "
        "beats always rising, because each node is read on its own "
        "belief.")

    never_hides_value = evaluate(
        sequence, source, cash_the_king_then_read_the_beliefs, lowest_eligible_defender)
    print(
        f"\nAgainst a defender who never hides a jack behind a queen, and "
        f"always plays the lowest legal card: "
        f"P_make = {never_hides_value['p_make']:.4f}.")
    _print_ace_vs_the_finesse(
        ace_vs_finesse_readings(sequence, source, lowest_eligible_defender),
        "a defender who never hides a jack behind a queen")
    print(
        "    The exact mirror of the always-shows-the-queen defender. This "
        "one plays the jack from the pair, so a queen must be bare. The "
        "jack covers every holding that could show either card.")

    with_randomizing_value = evaluate(
        sequence, source, cash_the_king_then_read_the_beliefs,
        heuristic_defender(ctx, bsle.make_default_defender_heuristics(
            root["trump"], randomize_touching_honors=True)))
    print(
        f"\nAgainst a caller-assembled heuristic chain (second hand low, "
        f"third hand high or low, ...) with randomize_touching_honors=True: "
        f"P_make = {with_randomizing_value['p_make']:.4f}.")

    without_randomizing_value = evaluate(
        sequence, source, cash_the_king_then_read_the_beliefs,
        heuristic_defender(ctx, bsle.make_default_defender_heuristics(
            root["trump"], randomize_touching_honors=False)))
    print(
        f"\nThe same chain with randomize_touching_honors=False: "
        f"P_make = {without_randomizing_value['p_make']:.4f}. Higher than "
        f"the row above.\n"
        f"With randomisation on, `second_seat_low` defers at every "
        f"touching pair to the fallback spread of `DoubleDummyDefender`. "
        f"So the row above is `P_make = 0.5159`, matching the "
        f"`DoubleDummyDefender` row above bit for bit. With randomisation "
        f"off, the rule always shows the lower card of the pair. That leaks "
        f"which card the defender holds, and the belief-reading declarer "
        f"exploits it. Nothing else in the chain changes between the two "
        f"rows.")

    brute_force_start = time.perf_counter()
    opponent_model_calls = 0
    _internal_opponent_model = double_dummy_defender(ctx)

    def _counting_double_dummy_defender(layout, seat, state):
        nonlocal opponent_model_calls
        opponent_model_calls += 1
        return _internal_opponent_model(layout, seat, state)

    # The limits keep this run to a few seconds, so the test stays fast.
    # An unlimited search over all 252 layouts was measured once: 104.18s
    # and 1,759,826 opponent-model calls. The output cites that figure; it
    # is not re-run.
    brute_force_value = evaluate(
        sequence, source,
        brute_force_declarer(
            ctx, objective=bsle.DeclarerObjective.MaximizeProbabilityToMake,
            opponent_model=_counting_double_dummy_defender, max_layouts=3, max_depth=3),
        double_dummy_defender(ctx))
    brute_force_seconds = time.perf_counter() - brute_force_start
    print(
        f"\nA brute-force (lookahead) declarer against `DoubleDummyDefender`, "
        f"limited to max_layouts=3, max_depth=3: "
        f"P_make = {brute_force_value['p_make']:.4f} "
        f"(searched in {brute_force_seconds:.2f}s, "
        f"{opponent_model_calls} internal opponent-model calls).\n"
        f"This is far from both 0.5159 (the `DoubleDummyDefender` row "
        f"above) and 0.5952 (an unlimited search). With this limit, only 3 "
        f"of 252 layouts survive at each pruned node. Posteriors here are "
        f"often equal, so the choice of those 3 is close to arbitrary.\n"
        f"An unlimited search over the same 252 layouts, measured once, "
        f"gave P_make = 0.5952 in 104.18s and 1,759,826 opponent-model "
        f"calls. So `max_layouts` and `max_depth` trade search quality for "
        f"speed.")

    print(
        "\nA minimal position that shows the randomize_touching_honors "
        "effect on one card. East has led a low spade. South, in second "
        "seat, holds only the spade queen and jack. Nothing left can beat "
        "them, so they are a touching pair.")
    minimal_layout = {
        "trump": NOTRUMP,
        "first": EAST,
        "remain_cards": [
            [holding(4), holding(4), 0, 0],          # North: spade 4, heart 4
            [0, holding(3), 0, 0],                   # East: spade already played; one heart left
            [holding(QUEEN, JACK), 0, 0, 0],         # South: the queen and the jack, nothing else
            [holding(5), holding(5), 0, 0],          # West (dummy): spade 5, heart 5
        ],
        "current_trick_suit": (SPADES, 0, 0),
        "current_trick_rank": (2, 0, 0),             # East's two, already played
    }
    minimal_state = types.SimpleNamespace(declarer=EAST)

    def _format_distribution(weighted):
        return ", ".join(f"{format_card(card)} {probability:.0%}" for card, probability in weighted)

    minimal_with = heuristic_defender(
        ctx, bsle.make_default_defender_heuristics(NOTRUMP, randomize_touching_honors=True),
    )(minimal_layout, SOUTH, minimal_state)
    print(f"    randomize_touching_honors=True:  {_format_distribution(minimal_with)}")

    minimal_without = heuristic_defender(
        ctx, bsle.make_default_defender_heuristics(NOTRUMP, randomize_touching_honors=False),
    )(minimal_layout, SOUTH, minimal_state)
    print(f"    randomize_touching_honors=False: {_format_distribution(minimal_without)}")
    print(
        "    With it on, both cards are equally likely, so a bare queen "
        "looks the same as the queen with the jack. With it off, the jack "
        "is certain. That is the information leak behind the P_make gap "
        "above.")


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
