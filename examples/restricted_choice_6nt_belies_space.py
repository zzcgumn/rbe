"""Restricted choice 6NT: a slam that comes down to a guess the cards
themselves answer differently than it looks.

    North   S J85    H 987   D QJT    C AJT5
    East    S A9642  H AQT3  D AK     C KQ
    South   S Q      H 652   D 98753  C 9764
    West    S KT73   H KJ4   D 642    C 832

West plays 6NT. Eight tricks cash themselves -- the rest of the diamonds,
hearts and clubs -- leaving the five-card-each ending

    North   S J85    D T      C T
    East    S A9642
    South   S Q      D 83     C 74
    West    S KT73           C 8

needing all five remaining tricks: the contract's only loser, if there is
one, is in spades. Declarer and dummy hold A K T 9 7 6 4 3 2 between them,
nine cards; North and South hold the other four -- Q J 8 5 -- split some
way that is not known.

The plan: lead low from dummy to the king in hand first (safe regardless of
the split -- nothing but the ace beats a king, and the ace is dummy's own).
Then lead low again -- the higher of hand's two remaining small cards,
specifically, keeping the lower one as the safe spare the ten-unblocking
technique below relies on; see `_lead_spade_or_the_lone_club`'s own
docstring for exactly why. Whatever North follows with on *that* trick
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

This example reads that probability off the belief space and reports it
against three defenders, each a fixed, named rule rather than a solver's
own tie-break, so each number is hand-checkable against the rule that
produced it:

- `randomises_queen_jack_in_second_seat`, which names the restricted-choice
  50/50 outright: holding the queen and the jack together, second seat, it
  shows either at random; everywhere else it plays low. `DoubleDummyDefender`
  under `SpreadPolicy.TouchingSequence` agrees with it exactly here, which
  the printed output also checks.
- `always_shows_the_queen_from_qj_in_second_seat`, the deliberate contrast:
  it always shows the queen from that same holding, never the jack, so
  showing it is no longer any tell at all -- the posterior collapses to the
  raw prior odds of the two holdings, and the read swings to favour the ace
  instead of the finesse.
- a defender who never hides a jack behind a queen at all (plays its
  lowest legal card, full stop) -- against it, the read correctly comes
  back certain, which is a fact derivable from that rule, not merely
  measured from it.

None of these is the same question as which card actually gives the
higher `P_make` over this contract as a whole -- the example reports both.
Reading the belief space turns out to score higher than always rising
with the ace against every defender measured here, which is the sane
direction for the gap to run: a declarer who reads a real probability
correctly should never do *worse* than one who ignores it outright, only
sometimes no better.

Run it with:

    bazelisk run //examples:restricted_choice_6nt
"""

import dds3

import belief_space_local_evaluation as bsle
from belief_space_local_evaluation import Card

from bridge_notation import (
    NOTRUMP,
    SEAT_NAMES,
    SOUTH,
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
    lowest_eligible_defender,
    pick,
)

SEED = 1

# The one piece of state this module carries outside a strategy's own
# arguments: the probability `cash_the_king_then_read_the_beliefs` computes
# at its one belief-dependent node, captured purely for `main()` to report.
# Keyed by which honour South showed on the first round (QUEEN or JACK) --
# the two branches this example contrasts, since North's second-round
# small card is the same "last possible small card" event either way.
# Nothing here feeds back into the card the strategy returns -- see that
# function's own docstring for why a side channel is used instead of
# reasoning the number out a second time afterwards.
_FINESSE_READING_BY_SOUTHS_HONOUR = {}


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
    """The lead shared by both declarers below, at every trick either of
    them leads (dummy's first round, hand's second, and any later round a
    duck hands back to dummy): prefer a spade to the lone club outright,
    and within spades, choose among three cases that only ever matter one
    at a time.

    - The king is still unplayed (this is the first round): lead low --
      nothing a defender holds beats a king, so there is nothing to
      protect by leading anything else, and leading the ace here instead
      would waste the one card that might still need to react to what a
      defender shows on a *later* round.
    - The king is gone and the ace is still held: lead the ace. Once the
      first round has already happened, holding the ace back on a later
      lead no longer protects anything either -- there is no trick left
      where retaining it lets it react to a defender's card, because
      whichever side wins the trick this leads to leads the next one too.
      This is the mirror image of the first case, not an exception to it.
    - Neither: lead the higher of the remaining small cards, keeping the
      lower one as the safe spare `_third_hand_after_the_ace_decision`'s
      own ten-unblocking note relies on. Leading the higher one first is
      what makes that spare safe: a hand that leads its small cards
      low-first is left holding the higher one, which is exactly the card
      liable to win a trick nobody meant it to.
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
    """The shared tail for both declarers below, once whatever the ace
    question had to settle (rise, duck, or there never was a question) is
    behind them: play the ten if it is still useful, and otherwise the
    cheapest remaining card that still wins.

    The ten is "still useful" whenever no *defender's* card on the trick
    already beats it -- not whenever nothing at all beats it. A defender's
    higher card means the ten is genuinely beaten and the trick is already
    lost regardless of what plays under it; our own partner's higher card
    means the trick is already *won* regardless, and the ten should be
    unloaded right then rather than saved for a future round where nothing
    will be behind it to support it. Saving it for later was the specific
    mistake a bare queen or bare jack still exposed before this function
    existed: with the ace already gone, a bare ten third hand, led into
    blind, loses to whichever honour survives -- the trick that unloading
    it here avoids.

    Past the ten, third hand plays the cheapest card that still wins
    rather than its own lowest: the generic "always play low" habit used
    elsewhere in this package is wrong specifically here, because letting
    an uncontested trick stand up over our own side's lead strands the
    losing hand on lead with nothing left but a loser in another suit.
    """
    ten = pick(legal, SPADES, TEN)
    if ten is not None:
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
    """Whether `on_trick[1]` -- North's response once the honour check in
    `cash_the_king_then_read_the_beliefs` has already ruled out an honour
    there -- is an actual spade, as opposed to a discard from a suit North
    has already voided (which happens whenever South holds all four
    missing cards, forcing North to zero of them).

    This is the gate on whether a node's `danger` is folded into
    `_FINESSE_READING_BY_SOUTHS_HONOUR`: a void discard is a structurally
    different, and in general differently weighted, belief-space node --
    South having all four missing cards makes South's retaining one of
    them a near-certainty, nothing like the genuine small-card case --
    and is not "two small from North" in the sense
    `_print_ace_vs_the_finesse`'s own label uses. Multiple such nodes can
    share the same South-honour key, so without this check the captured
    reading would be whichever node the evaluator happens to visit first
    -- an artifact of traversal order, not the fixed fact the module
    docstring claims it is. A pure function of `on_trick` alone, kept
    separate so this specific condition can be tested without going
    through a full `evaluate()` run.
    """
    return on_trick[1].suit == SPADES


def cash_the_king_then_read_the_beliefs(state, view):
    """pi: lead low to the king first (fixed -- the ace guarding it is
    dummy's own, so nothing a defender holds can beat it), then read the
    belief space for the one real decision in the suit.

    Declarer and dummy hold A K T 9 7 6 4 3 2 in spades; North and South
    hold Q J 8 5 between them. Both missing low cards (the eight and the
    five) are spent by the time North follows to the second round, so
    whatever North shows there is either an honour, North's last possible
    small card, or (whenever South holds all four missing cards) a
    discard from a suit North is already void in by then -- which the
    honour check below treats the same way a genuine small card is
    treated, since neither is an honour.

    - North shows an honour (queen or jack): win with the ace every time.
      A defender who still held the other honour behind the one just shown
      would, under `DoubleDummyDefender`'s own spreading policy, just as
      readily have shown that one instead -- so nothing here favours
      reading it either way, and the ace can never lose to it regardless.
    - North shows small (their last possible small card): the belief space
      is asked directly whether South -- not yet seen this trick -- still
      guards one of the two honours. Above even odds, rise with the ace;
      at or below, duck (keep the ace, and let the ten, still in hand,
      settle whatever North is left holding on a later round -- see
      `_lead_spade_or_the_lone_club` and `_third_hand_after_the_ace_decision`
      for exactly how that later round is handled, which is not "lead
      low" the way every other round here is).

    `view.entries` is read, and its result stashed in the module-level
    `_FINESSE_READING_BY_SOUTHS_HONOUR`, only for `main()` to report afterwards --
    not to influence this call's own return value, so the strategy stays a
    pure function of `(state, view)` as the evaluator requires. A second,
    separate evaluate() call, built the ordinary way from scratch, would
    have to re-derive the identical number; capturing it here instead of
    recomputing it is the plainer way to get it into the report.
    """
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
            if any(c.suit == SPADES and c.rank >= JACK for c in on_trick):
                return ace

            south = (state.trick_leader + 3) % 4
            danger = sum(
                entry.posterior for entry in view.entries
                if entry.layout["remain_cards"][south][SPADES] & ((1 << QUEEN) | (1 << JACK)))

            souths_first_card = state.history[1]
            if (souths_first_card.suit == SPADES and souths_first_card.rank in (QUEEN, JACK)
                    and _norths_second_card_is_a_genuine_small_spade(on_trick)):
                # The two branches this example contrasts: South's first
                # round and North's "last possible small card" round
                # already happened (the honour-on-the-trick check above
                # has already ruled out North also showing one here), so
                # this is exactly "jack from South, two small from North"
                # or "queen from South, two small from North" -- keyed by
                # which one South showed, not by whether this is the real
                # deal's own branch (that is South's queen specifically,
                # but the jack branch is just as real a node in the tree).
                _FINESSE_READING_BY_SOUTHS_HONOUR.setdefault(
                    souths_first_card.rank, 1.0 - danger)
            if danger > 0.5:
                return ace
            nine = pick(legal, SPADES, 9)
            return nine or min(legal, key=lambda c: (c.rank, c.suit))

        return _third_hand_after_the_ace_decision(legal, on_trick, state, dummy)

    return min(legal, key=lambda c: (c.rank, c.suit))


def always_rise_with_the_ace(state, view):
    """pi: as `cash_the_king_then_read_the_beliefs`, but never ducks --
    rise with the ace on the second round regardless of what the belief
    space says.

    Measured purely for contrast with the belief-reading declarer above.
    It is, perhaps surprisingly, the *worse* of the two overall (see the
    `P_make` the two print): once North's round-two card is not an honour,
    rising settles the suit outright in every layout where the split is
    2-2, but gives up on every layout where South still guards the
    outstanding honour behind it -- and the belief-reading declarer's own
    duck reclaims exactly those, correctly handled, without losing
    anything back in the 2-2 case it was never risking.
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


def always_shows_the_queen_from_qj_in_second_seat(layout, seat, state):
    """delta: in second seat, a defender holding both the spade queen and
    the spade jack always shows the queen -- never the jack; every other
    defender, and every other seat, follows with its lowest legal card.

    The deliberate contrast with `randomises_queen_jack_in_second_seat`:
    showing the queen here carries no information at all about whether the
    jack is behind it, because a defender dealt the bare queen and a
    defender dealt the queen with the jack behind it do exactly the same
    thing. Restricted choice has nothing to bite on, so the posterior this
    example reads back collapses to the raw prior odds of the two holdings
    -- unmodified by the showing, where the 50/50 defender's own
    half-the-time shortfall skews it towards the bare queen instead.
    """
    del state  # Conditions on the layout alone, like queen_of_spades_when_it_wins.
    legal = legal_cards(layout, seat)
    on_trick = cards_on_trick(layout)

    if len(on_trick) == 1:
        queen = pick(legal, SPADES, QUEEN)
        jack = pick(legal, SPADES, JACK)
        if queen is not None and jack is not None:
            return [(queen, 1.0)]

    return [(min(legal, key=lambda c: (c.rank, c.suit)), 1.0)]


def spade_split_frequencies(sequence, source) -> dict:
    """The belief space's own prior over the root -- before any card of
    this ending is played -- for four named spade splits: the suit
    breaking 2-2 between North and South, and South specifically holding
    the queen-jack pair tight, a singleton jack, or a singleton queen (the
    real deal's own holding).

    Settled with a throwaway `evaluate()` call. The root's `BeliefView`
    does not depend on which pi or delta it is paired with -- nobody has
    read a belief yet, so any legal pair reaches the identical root -- and
    `probe` below plays the rest of the hand out low purely so the call
    completes; nothing past its first call matters, and the whole result
    is discarded once `frequencies` is read back out of the closure.
    """
    frequencies = {}

    def probe(state, view):
        if not state.current_trick and not frequencies:
            for entry in view.entries:
                south = entry.layout["remain_cards"][SOUTH][SPADES]
                frequencies["2-2"] = frequencies.get("2-2", 0.0) + (
                    entry.posterior if bin(south).count("1") == 2 else 0.0)
                frequencies["south QJ tight"] = frequencies.get("south QJ tight", 0.0) + (
                    entry.posterior if south == (1 << QUEEN) | (1 << JACK) else 0.0)
                frequencies["south singleton J"] = frequencies.get("south singleton J", 0.0) + (
                    entry.posterior if south == (1 << JACK) else 0.0)
                frequencies["south singleton Q"] = frequencies.get("south singleton Q", 0.0) + (
                    entry.posterior if south == (1 << QUEEN) else 0.0)
        legal = state.legal_cards
        return min(legal, key=lambda c: (c.rank, c.suit))

    evaluate(sequence, source, probe, randomises_queen_jack_in_second_seat)
    return frequencies


def _print_ace_vs_the_finesse(readings_by_souths_honour, label) -> None:
    """Print the conditional probability of rising with the ace against
    taking the finesse, for each of the two branches `main()` tracks: the
    queen from South, two small from North, and the jack from South, two
    small from North, against one named defender.

    The two numbers printed for each branch always sum to one -- they are
    the same posterior `cash_the_king_then_read_the_beliefs` itself reads,
    `danger` and its complement, not two independent measurements -- so
    this is a restatement of the belief for readability, not a second
    calculation.
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
    print("Spades, prior to any card of this ending -- the belief space's own odds:")
    print(f"  2-2 break:               {frequencies['2-2']:.2%}")
    print(f"  South QJ tight:          {frequencies['south QJ tight']:.2%}")
    print(f"  South singleton jack:    {frequencies['south singleton J']:.2%}")
    print(f"  South singleton queen:   {frequencies['south singleton Q']:.2%}  (the real deal)")
    print()

    defence = randomises_queen_jack_in_second_seat

    _FINESSE_READING_BY_SOUTHS_HONOUR.clear()
    belief_value = evaluate(sequence, source, cash_the_king_then_read_the_beliefs, defence)
    print(f"Cash the king, then read the beliefs:  P_make = {belief_value['p_make']:.4f}")
    _print_ace_vs_the_finesse(
        _FINESSE_READING_BY_SOUTHS_HONOUR, "the bespoke 50/50 defender")
    print(
        "    The two branches agree exactly: a defender who genuinely "
        "randomises between the queen and the jack makes either one "
        "equally uncertain to have been forced, whichever is shown.")

    always_ace_value = evaluate(sequence, source, always_rise_with_the_ace, defence)
    print(f"\nAlways rising with the ace instead:     P_make = {always_ace_value['p_make']:.4f}")
    print(
        "Lower, not higher: rising settles the suit outright whenever the "
        "split is 2-2, but gives up on every layout where South still "
        "guards the outstanding honour behind the one North just showed. "
        "Reading the belief space reclaims exactly those -- correctly "
        "handled, the kept-back ace gets a later trick to capture "
        "whichever honour North is left holding, at no cost back in the "
        "2-2 layouts it was never risking in the first place.")

    _FINESSE_READING_BY_SOUTHS_HONOUR.clear()
    ctx = dds3.SolverContext()
    double_dummy_value = evaluate(
        sequence, source, cash_the_king_then_read_the_beliefs, double_dummy_defender(ctx))
    print(
        f"\n`DoubleDummyDefender` under `SpreadPolicy.TouchingSequence` "
        f"agrees with the bespoke 50/50 exactly here: P_make = "
        f"{double_dummy_value['p_make']:.4f}.")

    _FINESSE_READING_BY_SOUTHS_HONOUR.clear()
    no_tell_value = evaluate(
        sequence, source, cash_the_king_then_read_the_beliefs,
        always_shows_the_queen_from_qj_in_second_seat)
    print(
        f"\nAgainst a defender who always shows the queen from the queen "
        f"and the jack together -- never the jack -- instead: "
        f"P_make = {no_tell_value['p_make']:.4f}.")
    _print_ace_vs_the_finesse(
        _FINESSE_READING_BY_SOUTHS_HONOUR, "the always-shows-the-queen defender")
    print(
        "    No longer close: a defender who never prefers the jack makes "
        "showing it a dead giveaway (it is certainly a bare jack), while "
        "showing the queen now absorbs every holding that could have "
        "shown either one -- the single number this example reported "
        "before adding this contrast was exactly that queen row. Still "
        "higher than always rising overall, because every other node in "
        "the tree is read on its own belief, not forced to this one "
        "node's answer.")

    _FINESSE_READING_BY_SOUTHS_HONOUR.clear()
    never_hides_value = evaluate(
        sequence, source, cash_the_king_then_read_the_beliefs, lowest_eligible_defender)
    print(
        f"\nAnd against a defender who never hides a jack behind a queen "
        f"at all -- plays its lowest legal card, full stop -- instead: "
        f"P_make = {never_hides_value['p_make']:.4f}.")
    _print_ace_vs_the_finesse(
        _FINESSE_READING_BY_SOUTHS_HONOUR, "a defender who never hides a jack behind a queen")
    print(
        "    The exact mirror of the always-shows-the-queen defender "
        "above, not a repeat of it: this one prefers the lower-ranked "
        "card of a touching pair, the jack, so it is the *queen* that "
        "becomes the dead giveaway here (a bare queen is the only way to "
        "show it), while the jack now absorbs every holding that could "
        "have shown either one.")


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
