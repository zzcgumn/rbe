"""Two strategies to evaluate against each other, and the empty templates to
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

from play_sequence import legal_cards, seat_on_play


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
    `view` at all, so `P_make` under it is the floor a real strategy has to
    beat, and any difference between two runs is down to delta alone.

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
    library documents as the intended sound one. On this example's root that
    pairing reports `P_make = 0` for a position that makes 20% of the time:
    `DoubleDummyBound` calls `solve_board` with `solutions=1`, and dds
    answers a forced or all-equals play with `score = -2` -- "not evaluated"
    rather than a trick count -- which the bound returns as if it were one.
    A negative bound is below any `tricks_needed`, so the tier-2 cut fires
    on live nodes. Left out until that is fixed; it costs pruning only.
    """
    import belief_space_local_evaluation as bsle

    if policy is None:
        policy = bsle.SpreadPolicy.TouchingSequence
    return bsle.DoubleDummyDefender(ctx, policy)
