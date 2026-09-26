"""Strategies to evaluate against each other, and the empty templates to
copy when writing your own.

`evaluate()` takes a declarer strategy pi and a defender strategy delta and
computes `P_make` for *that pair*. Both are plain callables:

    pi(state, view) -> Card
    delta(layout, seat, state) -> [(Card, probability), ...]

**Both must be pure functions of their arguments.** The evaluator walks the
tree in its own order and revisits sibling subtrees, so a strategy that
carries state between calls -- a PRNG stream being the usual one -- returns
different cards for the same node and silently corrupts the result. Neither
strategy here holds any state; derive variety from the arguments if you need
it.

The asymmetry in the two signatures is not an oversight. pi sees `state` (what
is *commonly known*) and a `BeliefView` (the belief space, as probabilities),
because declarer does not know the layout. delta is handed `layout` itself --
the actual deal -- because a defender's own cards are not hidden from them,
and is called once per layout.
"""

from bridge_notation import HEARTS, SPADES
from belief_space_local_evaluation import legal_cards, seat_on_play

from play_sequence import cards_on_trick, trick_leader


def lowest_eligible_card(deal: dict, seat: int):
    """The lowest legal card, ranks compared first and suits used only to
    break a tie (spades lowest).

    "Lowest" is unambiguous while following suit and a choice when
    discarding, where nothing makes one four lower than another. Ties going
    to spades is arbitrary but fixed -- the point is that it is a function of
    the position alone, so the same node always yields the same card.
    """
    legal = legal_cards(deal, seat)
    if not legal:
        raise ValueError(f"seat {seat} holds nothing")
    return min(legal, key=lambda card: (card.rank, card.suit))


def lowest_eligible_declarer(state, view):
    """pi: declarer (and dummy) always play the lowest eligible card.

    A deliberately terrible declarer, and a useful one: it makes no use of
    `view` at all, so `P_make` under it is a reference value every other
    strategy here can be read against, and any difference between two runs
    is down to delta alone. Not a floor -- a strategy can do worse than
    playing low, and one that leads its own winners will.

    `state.known_holdings` is exact for declarer and dummy, which are the only
    seats pi is ever asked about -- a defender's entry there is the *pool* of
    both defenders' cards, not one hand, and reading it as a hand is the
    classic way to write a pi that quietly cheats.
    """
    del view  # A lowest-card rule conditions on the position, not the beliefs.
    return lowest_eligible_card(state.known_holdings, seat_on_play(state.known_holdings))


def lowest_eligible_defender(layout, seat, state):
    """delta: both defenders always play the lowest eligible card, with
    certainty.

    The returned list is a distribution over the cards this defender might
    play. A card the defender will never play is **left out**, not given
    probability 0: the evaluator treats "probability > 0" as the test for
    whether a layout survives, so a zero-weighted card is not a no-op.
    """
    del state  # Deterministic: this defender conditions on their own cards only.
    return [(lowest_eligible_card(layout, seat), 1.0)]


TEN, JACK, QUEEN, KING, ACE = 10, 11, 12, 13, 14


def _lowest(legal):
    return min(legal, key=lambda card: (card.rank, card.suit))


def pick(legal, suit, rank):
    """That card, if it is one of the legal ones -- else None."""
    for card in legal:
        if card.suit == suit and card.rank == rank:
            return card
    return None


def _spade_queen_cannot_be_beaten(layout: dict) -> bool:
    """Whether the spade queen, played now by the seat on play, takes the
    trick. The seat is derived from `layout` rather than passed: it is
    `first` advanced by the cards already on the trick, and taking it as an
    argument would let a caller disagree with the position.

    Three things have to hold. Spades must be the suit of the trick --
    contract 6NT, so there is no trump to ruff with, and a queen thrown on
    another suit's trick never wins. Nothing already played can be higher.
    And no seat still to play can beat it, which in spades means holding the
    ace or the king.

    That last test reads the other hands out of `layout`, which a defender
    strategy is given in full. It is the same licence `DoubleDummyDefender`
    takes: a model defender is allowed to see through the cards, and the
    result is a statement about the position rather than a guess from one
    seat's knowledge. A strategy meant to be realistic would have to work
    from `state` instead.

    It asks whether the queen *can* be beaten, not whether it will be --
    a seat holding the king might play low. Assuming otherwise would make
    this claim depend on declarer's strategy, and a defender that is only
    right against one declarer is not much of a defender.
    """
    on_trick = cards_on_trick(layout)
    if on_trick and on_trick[0].suit != SPADES:
        return False
    if any(c.suit == SPADES and c.rank > QUEEN for c in on_trick):
        return False

    for position in range(len(on_trick) + 1, 4):
        later = (layout["first"] + position) % 4
        higher = (1 << ACE) | (1 << KING)
        if layout["remain_cards"][later][SPADES] & higher:
            return False
    return True


def queen_of_spades_when_it_wins(layout, seat, state):
    """delta: play the spade queen when it takes the trick, otherwise low.

    Written against this example's ending, where locating that one card is
    the whole problem -- not a general defensive rule.
    """
    del state  # Conditions on the layout alone.
    legal = legal_cards(layout, seat)
    queen = pick(legal, SPADES, QUEEN)
    if queen is not None and _spade_queen_cannot_be_beaten(layout):
        return [(queen, 1.0)]
    return [(_lowest(legal), 1.0)]


def finesse_the_queen_from_the_beliefs(state, view):
    """pi: as `cash_two_hearts_and_play_a_spade`, but the spade guess is read
    off the belief space instead of fixed in advance.

    The only strategy here that touches `view` -- and the reason the library
    exists. The two hearts are forced (nothing to decide), and covering a
    played queen is certain, so the one real decision is which defender to
    play for the queen. That is a probability, not a card, and it is sitting
    in `view`.

    `entry.posterior` is the probability of that layout given everything
    observed. Summing it over the layouts in which the queen sits with a
    particular defender gives the probability that defender holds it. On this
    ending the two are 0.5 each at the root, so the choice only becomes
    informative once the defenders' own play has narrowed the space -- which
    is exactly what a belief space is for.

    **`view` is valid only for this call.** Read what is needed and return;
    storing it, or an entry from it, and touching either afterwards raises
    `ExpiredBeliefViewError`. A `layout` dict already read stays usable, but
    there is no reason to keep one here.
    """
    deal = state.known_holdings
    seat = seat_on_play(deal)
    legal = legal_cards(deal, seat)
    on_trick = cards_on_trick(deal)

    if not on_trick:
        lead = pick(legal, HEARTS, ACE) or pick(legal, HEARTS, TEN)
        if lead is not None:
            return lead
        # Leading spades. Play the low card from the hand that has one, so
        # the queen has to commit before the honour behind it does.
        return min(legal, key=lambda card: (card.rank, card.suit))

    if any(c.suit == SPADES and c.rank == QUEEN for c in on_trick):
        king = pick(legal, SPADES, KING)
        if king is not None:
            return king

    # Third hand on a spade: finesse or play for the drop, according to which
    # defender the beliefs put the queen with. `rho` is the defender who has
    # already played to this trick; if the queen is more likely to be over
    # there it is already committed and the jack is safe, so play low --
    # otherwise the king is the card that cannot be beaten by it.
    if on_trick[0].suit == SPADES:
        rho = (trick_leader(deal) + len(on_trick) - 1) % 4
        queen_with_rho = sum(
            entry.posterior for entry in view.entries
            if entry.layout["remain_cards"][rho][SPADES] & (1 << QUEEN))
        jack = pick(legal, SPADES, JACK)
        king = pick(legal, SPADES, KING)
        if queen_with_rho > 0.5 and jack is not None:
            return jack
        if queen_with_rho < 0.5 and king is not None:
            return king

    return min(legal, key=lambda card: (card.rank, card.suit))


# --- templates ------------------------------------------------------------
#
# The shape of each callback, with the body left out. Copy one of these
# rather than a working strategy above when the rule you want has nothing to
# do with playing low.


def empty_declarer(state, view):
    """pi, unimplemented. Return one Card: what declarer or dummy -- whichever
    of the two is on play -- plays at this node.

    It must be legal for *every* layout the node holds. That is automatic if
    it is chosen from `state.known_holdings`, since the seats pi plays for
    hold the same cards in every layout of the belief space; it is not
    automatic if chosen any other way.
    """
    raise NotImplementedError("declarer strategy")


def empty_defender(layout, seat, state):
    """delta, unimplemented. Return the cards `seat` might play from `layout`,
    with the probability of each, summing to 1.

    Every card returned must be legal for `seat` in `layout`, and every card
    that could be played must be listed -- a card omitted is a card this
    defender will never play, which is a claim about the strategy, not a
    shortcut.
    """
    raise NotImplementedError("defender strategy")


# --- the solver seam ------------------------------------------------------


def double_dummy_defender(ctx, policy=None):
    """delta: both defenders play double dummy -- they see all four hands.

    `bsle.DoubleDummyDefender` is already usable as delta directly; this
    exists to keep its two caveats next to the call rather than in a
    docstring you have to go and find.

    **It is not best defence against the contract.** It maximises tricks,
    and will sometimes concede the contract to hold the trick count down.
    The gap between maximising tricks and minimising `P_make` is the thing
    this library exists to measure, not a defect to work around.

    **A `SolverContext` is not thread-safe**, and the solve releases the
    GIL, so two Python threads sharing one really do run concurrently. One
    context, and one of these, per worker.

    Not paired here with `DoubleDummyBound`, which is the configuration the
    library documents as the intended one, only because this example has no
    use for pruning -- 70 layouts and a four-card ending. The pairing is
    sound: on positions where `solve_board` at `solutions=1` answers -2
    ("not evaluated" rather than a trick count), `DoubleDummyBound` returns
    its too-high sentinel instead, so the cut cannot fire on a live node.
    That costs pruning on those positions and nothing else. Which positions
    they are is not established -- see "Known gaps" in
    docs/belief_space_local_evaluation.md.
    """
    import belief_space_local_evaluation as bsle

    if policy is None:
        policy = bsle.SpreadPolicy.TouchingSequence
    return bsle.DoubleDummyDefender(ctx, policy)
