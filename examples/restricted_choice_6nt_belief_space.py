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

The plan: lead low from dummy to the king in hand first, then lead low
again -- the higher of hand's two remaining small cards, specifically,
keeping the lower one as the safe spare the ten-unblocking technique
below relies on; see `_lead_spade_or_the_lone_club`'s own docstring for
exactly why. What North follows with on *that* trick settles everything
below the two honours -- the eight and the five are the only other
outstanding cards, so this is necessarily North's last possible small
card, unless North is already void. Three cases, not two:

- North shows an honour: win with the ace every time -- there is nothing
  to read, a defender who still held the other honour behind the one just
  shown would simply have kept this one back too.
- North shows their last possible small card (a real spade): the real
  decision is on the table -- rise with the ace now, or duck (relying on
  the ten, still in hand, once South's own card -- not yet seen, and
  played to *this same trick* -- has settled where the last card is).
  Genuinely risky either way: South could reveal the danger card on this
  very trick, so a wrong duck here can lose outright, on the spot. A
  singleton honour in *South's* hand also lands here: South shows it
  forced, on the very first round, and North's own two small cards are
  then read exactly like any genuine 2-2 break.
- North is already void and discards: always duck, regardless of what the
  belief space says -- it always says South certainly guards an honour
  here, but that is a fact about South's length, not a reason to rise.
  Two different layouts share this branch, and they do not share an
  outcome.

  When South holds the queen *and* the jack -- whether with one small
  card alongside them or two -- the contract cannot be made by either
  choice at this trick. South's remaining honour outranks this side's
  only card left above the small spades (the ten) once the king and the
  ace are both gone, which happens by the third round regardless of
  which way this trick goes, and South always has a spot card in hand to
  hold the second honour back that long. Ducking is "free" there only in
  the sense that it loses no worse than rising would.

  The other layout sharing this branch -- North's own singleton is the
  *honour*, with South holding the other honour plus both small cards --
  is one this plan simply loses, and it is this plan's first move that
  loses it, not its second. "Lead low to the king first" is not the
  risk-free opening its own safety (nothing beats a king) makes it look
  like: it is already a bet that *South*, not North, is the hand hiding
  a dangerous singleton honour, the same kind of bet the later
  ace-or-duck decision makes openly and reads the belief space for. This
  earlier bet is never read anywhere -- the king is led for unconditionally,
  on every call, regardless of which side the belief space would actually
  favour. Checked directly against a double-dummy solver, the bet is
  wrong exactly backwards for this layout: leading the *ace* first, not
  the king, is what makes all five tricks here; led this plan's way
  instead, the contract is already down to two tricks out of five before
  the later duck-or-rise choice is even reached, and no choice at that
  later point wins the rest back (though ducking still salvages more of
  it than rising does -- see `always_rise_with_the_ace`'s own docstring).
  `cash_the_king_then_read_the_beliefs` never asks the belief space this
  earlier question, so it does not win this layout, even though a
  declarer who did ask it, in principle, could.

This example reads the genuine (second) case's probability off the belief
space and reports it, plus the fixed (third) case's correctness, against
four defenders, each a fixed, named rule rather than a solver's own
tie-break, so each number is hand-checkable against the rule that produced
it: `randomises_queen_jack_in_second_seat`, which names the
restricted-choice 50/50 outright (holding the queen and the jack together,
second seat, shows either at random; everywhere else, plays low);
`always_shows_the_queen_from_qj_in_second_seat`, the deliberate contrast
(always the queen from that same holding, never the jack, so showing it is
no longer any tell at all); a defender who never hides a jack behind a
queen at all (plays its lowest legal card, full stop); and
`DoubleDummyDefender`.

That fourth one is the interesting case. It agrees with the bespoke 50/50
defender exactly on the genuine small-card branch -- both read 60% for
the finesse there, which the printed output checks -- but *not* overall:
the always-duck-when-void technique above only pays off against a
defender whose later play is itself predictable (always the lowest legal
card), and `DoubleDummyDefender` is not one of those. A real double-dummy
defender, holding South's length in the void branches, is not obliged to
shed its small cards low-to-high the way the other three defenders here
always do; it is free to release the danger card on whichever round
actually defeats the always-duck plan, and measured here, it does. The
technique still never costs anything against `DoubleDummyDefender`
specifically (nothing about the fix can make a node's outcome *worse*
against any defender, only fail to improve it) -- it simply does not
gain against this one the way it gains against the other three.

None of this is the same question as which card actually gives the higher
`P_make` over this contract as a whole -- the example reports both.
Reading the belief space still scores higher than always rising with the
ace against every defender measured here, including `DoubleDummyDefender`
-- but the margin is not a fixed, defender-independent fact the way the
always-duck technique's own correctness is; it depends on how predictable
the specific defender's remaining small cards turn out to be.

Run it with:

    bazelisk run //examples:restricted_choice_6nt
"""

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
    double_dummy_defender,
    heuristic_defender,
    lowest_eligible_defender,
    pick,
)

SEED = 1

# Not named in strategies.py (only the honours TEN through ACE are), but
# used the same way below: the specific duck card
# cash_the_king_then_read_the_beliefs plays, chosen to beat North's own
# small cards (the eight and the five) while still not committing the
# ace -- see that function's own docstring.
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
    """The lead shared by both declarers below, at every trick either of
    them leads (dummy's first round, hand's second, and any later round a
    duck hands back to dummy): prefer a spade to the lone club outright,
    and within spades, choose among three cases that only ever matter one
    at a time.

    - The king is still unplayed (this is the first round): lead low.
      Nothing a defender holds beats a king, so this trick itself is
      never at risk -- but that is not the same as this being a neutral,
      risk-free opening. It is a bet that *South*, not North, is the
      hand holding a dangerous singleton honour, exactly the kind of bet
      the later ace-or-duck decision makes openly and reads the belief
      space for. This earlier one is never asked -- the king is led for
      unconditionally, every time this is called, regardless of which
      side is actually more likely to be dangerous. See
      `cash_the_king_then_read_the_beliefs`'s own docstring for the one
      layout (North's own singleton is the honour) where that specific
      bet is wrong, and costs the contract outright.
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
        our_sides_cards_on_trick = (
            c.rank for position, c in enumerate(on_trick)
            if c.suit == SPADES and (state.trick_leader + position) % 4 in (state.declarer, dummy))
        if max(our_sides_cards_on_trick, default=-1) > TEN:
            # Our own side already has this trick's winner on the table,
            # with something higher than the ten (the ace, the only card
            # above ten our side ever holds once the king is gone) -- the
            # trick's outcome is already settled, so the ten is safe to
            # unload no matter what a defender has also shown. Checking
            # only "did a defender beat the ten" here, without this,
            # would keep the ten back whenever a defender's honour also
            # happened to be on the trick -- even though that honour
            # never had a chance against our own side's higher card
            # either, and keeping the ten back is exactly the mistake
            # this function exists to avoid.
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
    """Whether `on_trick[1]` -- North's response once the honour check in
    `cash_the_king_then_read_the_beliefs` has already ruled out an honour
    there -- is an actual spade, as opposed to a discard from a suit North
    has already voided (which happens whenever South holds all four
    missing cards, forcing North to zero of them).

    This is the gate on whether a node's `danger` is folded into
    `ace_vs_finesse_readings`'s own `readings` dict: a void discard is a structurally
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


def _danger_south_still_guards_an_honour(state, on_trick, view, dummy):
    """Pure function of `(state, view)` (plus `on_trick`/`dummy`, both
    already derivable from `state` and repeated here only so a caller
    that already has them need not recompute them): `None` everywhere
    except `cash_the_king_then_read_the_beliefs`'s one belief-dependent
    node -- king already gone, ace still held, and North's just-played
    card not an honour -- where it is the belief-space posterior that
    South, not yet seen this trick, still holds the queen or the jack.

    Factored out of `cash_the_king_then_read_the_beliefs` itself so that
    function can stay a genuinely pure declarer strategy -- no side
    channel of its own -- while `ace_vs_finesse_readings` below can still
    report this exact number: both call this same function, rather than
    one of them writing it somewhere the other reads it back from.
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

    # `trick_leader + 3` is South's seat here specifically because this
    # line is only reachable with `trick_leader == declarer` (West
    # leading round two) -- the sibling case, `trick_leader == dummy`
    # (East leading round one), already returned `None` above (the king
    # check). Were that short-circuit ever reordered or removed, this
    # would silently compute North's seat instead of South's, with no
    # error anywhere -- the specific shape of bug this file's review
    # history already has two instances of.
    souths_seat = (state.trick_leader + 3) % 4
    return sum(
        entry.posterior for entry in view.entries
        if entry.layout["remain_cards"][souths_seat][SPADES] & ((1 << QUEEN) | (1 << JACK)))


def cash_the_king_then_read_the_beliefs(state, view):
    """pi: lead low to the king first, then read the belief space for the
    one real decision in the suit.

    Declarer and dummy hold A K T 9 7 6 4 3 2 in spades; North and South
    hold Q J 8 5 between them. Both missing low cards (the eight and the
    five) are spent by the time North follows to the second round, so
    whatever North shows there is either an honour, North's last possible
    small card, or (whenever North is already void) a discard from a suit
    North is already void in by then.

    - North shows an honour (queen or jack): win with the ace every time.
      A defender who still held the other honour behind the one just shown
      would, under `DoubleDummyDefender`'s own spreading policy, just as
      readily have shown that one instead -- so nothing here favours
      reading it either way, and the ace can never lose to it regardless.
    - North shows their last possible small card (a genuine spade, not a
      discard -- `_norths_second_card_is_a_genuine_small_spade`): the
      belief space is asked directly whether South -- not yet seen this
      trick -- still guards one of the two honours
      (`_danger_south_still_guards_an_honour`). Above even odds, rise
      with the ace; at or below, duck (keep the ace, and let the ten,
      still in hand, settle whatever North is left holding on a later
      round -- see `_lead_spade_or_the_lone_club` and
      `_third_hand_after_the_ace_decision` for exactly how that later
      round is handled, which is not "lead low" the way every other
      round here is). This is a genuine risk either way: South, not yet
      seen, plays *this same trick*, so a wrong duck here can lose
      outright, on the spot. A singleton honour in *South's* own hand
      also lands here, not in the void branch below: South shows it
      forced, on the first round (before this one), and North's two
      remaining small cards are then read exactly like a genuine 2-2
      break.
    - North discards, already void: always duck, regardless of what
      `_danger_south_still_guards_an_honour` computes -- it is always
      1.0 here (North holding nothing forces whatever remains to be
      South's), but that is a fact about South's length, not a reason to
      rise. Two different layouts share this branch, and they do not
      share an outcome:
        - South holds both the queen and the jack (with one small card
          alongside them, or two): the contract cannot be made by either
          choice at this trick. Once the king and the ace are both gone
          -- unavoidably, by the third round of the suit, whichever one
          of rise or duck this trick chose -- South's remaining honour
          outranks the ten, this side's only card left above North and
          South's own small cards, and South still has a spot card to
          hold it back until exactly that round. Ducking does not avoid
          this; it only avoids making it worse. Both choices lose this
          layout identically, which is the only sense in which ducking
          here is "free" -- not a guarantee that it wins.
        - South holds exactly one honour, with North's own singleton
          being the other one: double-dummy, this layout *is* makeable
          -- but not by this function, and not at this trick. The
          mistake already happened one trick earlier: leading low to
          the king, this plan's fixed first move, is a bet that *South*
          is the hand with the dangerous singleton honour, and this
          layout is exactly the one where that bet is wrong. Checked
          against a double-dummy solver, leading the *ace* first (not
          the king) is what makes all five tricks here; led this plan's
          way instead, the contract is already down to two tricks out of
          five before this trick's duck-or-rise choice is even reached.
          Ducking still salvages more of what is left than rising would
          (see `always_rise_with_the_ace`'s own docstring), but neither
          choice recovers what the first trick already gave up. This
          function never reads the belief space at that first lead --
          `_lead_spade_or_the_lone_club` always leads low, regardless of
          which side is actually more likely to be dangerous -- so it
          simply does not win this layout, even though a declarer who
          asked that earlier question, in principle, could.

    A pure function of `(state, view)`, with no side channel of its own:
    see `ace_vs_finesse_readings` for how the belief this function reads
    gets reported without this function writing it anywhere itself.
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
            # _danger_south_still_guards_an_honour already returned None
            # above, and the only way it does that with the ace held is
            # North having just shown an honour -- win with it every time
            # (see this function's own docstring for why).
            return ace

        return _third_hand_after_the_ace_decision(legal, on_trick, state, dummy)

    return min(legal, key=lambda c: (c.rank, c.suit))


def always_rise_with_the_ace(state, view):
    """pi: as `cash_the_king_then_read_the_beliefs`, but never ducks --
    rise with the ace on the second round regardless of what the belief
    space says.

    Measured purely for contrast with the belief-reading declarer above.
    It is, perhaps surprisingly, the *worse* of the two overall (see the
    `P_make` the two print) -- but not because reading the belief costs
    nothing. Once North's round-two card is a genuine small card (not a
    discard), rising wins outright exactly when South still guards the
    outstanding honour behind it (the queen-jack-tight sub-case of a 2-2
    break) -- a duck loses that trick on the spot instead, to South's own
    card, played right there. The belief-reading declarer's own duck
    wins the *opposite* case instead (South having shown a singleton
    honour, with North -- not South -- left holding the other), and that
    case is weighted higher here: 60% against 40%, exactly
    `ace_vs_finesse_readings`'s own printed split for this node, not a
    free reclaim of one without risking the other.

    Separately, once North is already void, rising and ducking are not
    symmetric the way they are at the genuine small-card node above --
    but "not symmetric" is not the same as "duck wins outright" either.
    When South holds both the queen and the jack there, neither choice
    makes the contract, so rising costs nothing extra. When North's own
    singleton was the honour instead, the contract was already lost one
    trick earlier, at this plan's fixed first lead (low to the king,
    never the ace) -- and within what that first mistake leaves behind,
    ducking still salvages more than rising does, the same shape of gain
    as the genuine small-card node, just smaller. See
    `cash_the_king_then_read_the_beliefs`'s own docstring for exactly
    which layout is which, and why neither one is a free win for duck.
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

    # `on_trick[0].suit == SPADES` is load-bearing, not a sanity check:
    # without it, this also fires whenever a non-spade trick is led and
    # this defender is void in that suit (every spade, including both
    # honours, is then a legal discard too), making it randomise a
    # discard instead of following the documented "every other seat...
    # plays its lowest legal card" rule on a trick that was never a
    # second-seat spade decision at all.
    if len(on_trick) == 1 and on_trick[0].suit == SPADES:
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

    # See randomises_queen_jack_in_second_seat's own comment on this same
    # check: without the suit restriction, a non-spade trick this
    # defender is void in would also hand it both spade honours as legal
    # discards, firing this special case on a trick that is not a
    # second-seat spade decision at all.
    if len(on_trick) == 1 and on_trick[0].suit == SPADES:
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
    """The conditional probability that *the finesse* is correct, against
    rising with the ace, at `cash_the_king_then_read_the_beliefs`'s one
    belief-dependent node -- once North has followed with their last
    possible small card -- keyed by which honour South showed on the
    first round (`QUEEN` or `JACK`), against the given defender. (The
    complementary ace-is-correct probability a caller may also want is
    just one minus this; `_print_ace_vs_the_finesse` reports both.)

    Settled with a dedicated `evaluate()` call, the same way
    `spade_split_frequencies` settles the root's own prior: `probe` plays
    exactly the game `cash_the_king_then_read_the_beliefs` itself would
    (it calls that function directly for the returned card, so the two
    can never disagree about what is actually played), and separately
    reads `_danger_south_still_guards_an_honour` -- the same pure
    function `cash_the_king_then_read_the_beliefs` itself reads -- into
    a `readings` dict that lives only in this call's own closure. Nothing
    is written to module-level state anywhere in this package: the
    declarer strategy stays a pure function of `(state, view)`, and this
    reporting need is met by asking the same question again from
    outside it, not by having the strategy answer twice.
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

    belief_value = evaluate(sequence, source, cash_the_king_then_read_the_beliefs, defence)
    print(f"Cash the king, then read the beliefs:  P_make = {belief_value['p_make']:.4f}")
    _print_ace_vs_the_finesse(
        ace_vs_finesse_readings(sequence, source, defence), "the bespoke 50/50 defender")
    print(
        "    The two branches agree exactly: a defender who genuinely "
        "randomises between the queen and the jack makes either one "
        "equally uncertain to have been forced, whichever is shown.")

    always_ace_value = evaluate(sequence, source, always_rise_with_the_ace, defence)
    print(f"\nAlways rising with the ace instead:     P_make = {always_ace_value['p_make']:.4f}")
    print(
        "Lower, not higher, but not for free either: at the genuine "
        "small-card node above, rising wins outright exactly when South "
        "still guards the outstanding honour (the 40% share the reading "
        "just printed), and a duck loses that trick on the spot instead "
        "-- the belief-reading declarer's own duck is simply betting on "
        "the more likely 60% case (North left holding the danger card, "
        "not South), not avoiding a risk altogether. The void branch, "
        "where North is already out of spades, is not a clean win for "
        "duck either. When South holds the queen and the jack together "
        "there, neither choice makes the contract -- so duck costs "
        "nothing, but it does not win anything back either. When North's "
        "own singleton was the honour instead, the contract was already "
        "lost one trick earlier, at this plan's fixed first lead (low to "
        "the king, never the ace) -- a double-dummy solver makes that "
        "layout in full by leading the ace first, a bet this plan never "
        "makes. Ducking at this later trick still salvages more of what "
        "is left than rising would, but neither recovers what the first "
        "lead already gave up -- see `cash_the_king_then_read_the_beliefs`'s "
        "own docstring for exactly which layout is which.")

    ctx = dds3.SolverContext()
    double_dummy_value = evaluate(
        sequence, source, cash_the_king_then_read_the_beliefs, double_dummy_defender(ctx))
    print(
        f"\n`DoubleDummyDefender` under `SpreadPolicy.TouchingSequence` "
        f"instead: P_make = {double_dummy_value['p_make']:.4f}. It reads "
        f"the genuine small-card node exactly the way the bespoke 50/50 "
        f"defender does (`ace_vs_finesse_readings` agrees on both "
        f"branches), but the always-duck-when-void technique above does "
        f"not pay off against it the way it does against the other three "
        f"defenders this example measures: that technique relies on the "
        f"defender's own remaining cards coming out lowest-first, and a "
        f"real double-dummy defender is not obliged to play that "
        f"predictably.")

    no_tell_value = evaluate(
        sequence, source, cash_the_king_then_read_the_beliefs,
        always_shows_the_queen_from_qj_in_second_seat)
    print(
        f"\nAgainst a defender who always shows the queen from the queen "
        f"and the jack together -- never the jack -- instead: "
        f"P_make = {no_tell_value['p_make']:.4f}.")
    _print_ace_vs_the_finesse(
        ace_vs_finesse_readings(sequence, source, always_shows_the_queen_from_qj_in_second_seat),
        "the always-shows-the-queen defender")
    print(
        "    No longer close: a defender who never prefers the jack makes "
        "showing it a dead giveaway (it is certainly a bare jack), while "
        "showing the queen now absorbs every holding that could have "
        "shown either one -- the single number this example reported "
        "before adding this contrast was exactly that queen row. Still "
        "higher than always rising overall, because every other node in "
        "the tree is read on its own belief, not forced to this one "
        "node's answer.")

    never_hides_value = evaluate(
        sequence, source, cash_the_king_then_read_the_beliefs, lowest_eligible_defender)
    print(
        f"\nAnd against a defender who never hides a jack behind a queen "
        f"at all -- plays its lowest legal card, full stop -- instead: "
        f"P_make = {never_hides_value['p_make']:.4f}.")
    _print_ace_vs_the_finesse(
        ace_vs_finesse_readings(sequence, source, lowest_eligible_defender),
        "a defender who never hides a jack behind a queen")
    print(
        "    The exact mirror of the always-shows-the-queen defender "
        "above, not a repeat of it: this one prefers the lower-ranked "
        "card of a touching pair, the jack, so it is the *queen* that "
        "becomes the dead giveaway here (a bare queen is the only way to "
        "show it), while the jack now absorbs every holding that could "
        "have shown either one.")

    with_randomising_value = evaluate(
        sequence, source, cash_the_king_then_read_the_beliefs,
        heuristic_defender(ctx, bsle.make_default_defender_heuristics(
            root["trump"], randomise_touching_honours=True)))
    print(
        f"\nAgainst a caller-assembled heuristic chain (second-hand low, "
        f"third-hand high/low, ...) with randomise_touching_honours=True "
        f"instead: P_make = {with_randomising_value['p_make']:.4f}.")

    without_randomising_value = evaluate(
        sequence, source, cash_the_king_then_read_the_beliefs,
        heuristic_defender(ctx, bsle.make_default_defender_heuristics(
            root["trump"], randomise_touching_honours=False)))
    print(
        f"\nThe identical chain with randomise_touching_honours=False "
        f"instead: P_make = {without_randomising_value['p_make']:.4f}. "
        f"Higher than the row above, not a rounding difference: with "
        f"randomisation on, `second_seat_low` detects every genuine "
        f"touching pair this ending's own second-seat node ever reaches "
        f"and defers to the chain's fallback spread there, which is "
        f"exactly `DoubleDummyDefender`'s own uniform spread over the "
        f"same solved position -- so the two agree exactly (both "
        f"`P_make = 0.5159`, matching the `DoubleDummyDefender` row "
        f"above bit for bit, not merely to four decimal places). With "
        f"randomisation off, the same rule instead always shows the "
        f"lower card of that pair -- the exact information leak this "
        f"whole example is about -- and measured here, the "
        f"belief-reading declarer is able to exploit it: this one "
        f"toggle, isolated from every other difference between "
        f"defenders this example measures (nothing else in the chain "
        f"changes between these two rows), raises `P_make` by itself.")

    print(
        "\nA minimal, standalone position isolating the mechanism behind "
        "the gap just measured, on the one card it actually turns on: "
        "East leads a low spade (already played); South, second seat, "
        "holds only the queen and the jack of spades. Nothing left "
        "anywhere can beat either one, and the king between them is "
        "already gone from play, so the two are a genuine touching pair.")
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
        ctx, bsle.make_default_defender_heuristics(NOTRUMP, randomise_touching_honours=True),
    )(minimal_layout, SOUTH, minimal_state)
    print(f"    randomise_touching_honours=True:  {_format_distribution(minimal_with)}")

    minimal_without = heuristic_defender(
        ctx, bsle.make_default_defender_heuristics(NOTRUMP, randomise_touching_honours=False),
    )(minimal_layout, SOUTH, minimal_state)
    print(f"    randomise_touching_honours=False: {_format_distribution(minimal_without)}")
    print(
        "    With it on, the two are equally likely -- a defender holding "
        "the bare queen looks identical to one holding the queen with the "
        "jack behind it. With it off, the jack is certain -- the exact "
        "information leak the gap above is made of, shown directly on the "
        "one card South actually plays rather than through a P_make.")


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
