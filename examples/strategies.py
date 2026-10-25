"""Strategies to evaluate against each other, and the empty templates to
copy when writing your own.

`evaluate()` computes `P_make` for one pair: a declarer strategy pi and a
defender strategy delta. Both are plain callables:

    pi(state, view) -> Card
    delta(layout, seat, state) -> [(Card, probability), ...]

**Both must be pure functions of their arguments.** A strategy that keeps
state between calls, such as a random stream, corrupts the result silently.
See "π and δ" in docs/belief_space_local_evaluation.md.

The two signatures differ on purpose. Declarer does not know the layout, so
pi gets `state` (public knowledge) and a `BeliefView` (the belief space).
A defender sees their own cards, so delta gets the real `layout`. The
evaluator calls delta once per layout.
"""

from bridge_notation import HEARTS, SPADES
from belief_space_local_evaluation import legal_cards, seat_on_play

# The defenders below read the trick from `layout`, not from `state`.
# Declarer strategies read state.current_trick instead.
from belief_space_local_evaluation.play_sequence import cards_on_trick


def lowest_eligible_card(deal: dict, seat: int):
    """The lowest legal card. Compare ranks first. Break a tie by suit,
    spades lowest.

    The tie-break matters only when leading or discarding. It is arbitrary
    but fixed, so the same node always gives the same card.
    """
    legal = legal_cards(deal, seat)
    if not legal:
        raise ValueError(f"seat {seat} holds nothing")
    return min(legal, key=lambda card: (card.rank, card.suit))


def lowest_eligible_declarer(state, view):
    """pi: declarer and dummy always play the lowest eligible card.

    A poor declarer, but a useful reference. It ignores `view`, so any
    difference between two runs comes from delta alone. It is not a lower
    bound: a strategy that leads its own winners does worse.

    `state.known_holdings` is exact for declarer and dummy. A defender entry
    is the pool of both defenders' cards, not one hand. **Do not read it as
    a hand:** that makes pi cheat without any error.
    """
    del view  # A lowest-card rule conditions on the position, not the beliefs.
    return lowest_eligible_card(state.known_holdings, state.seat_on_play)


def lowest_eligible_defender(layout, seat, state):
    """delta: both defenders always play the lowest eligible card.

    The list is a distribution over the cards this defender might play.
    **Leave out a card the defender never plays.** Do not give it
    probability 0. The evaluator keeps a layout only if its card has
    probability > 0.
    """
    del state  # This defender uses only their own cards.
    return [(lowest_eligible_card(layout, seat), 1.0)]


TEN, JACK, QUEEN, KING, ACE = 10, 11, 12, 13, 14


def _lowest(legal):
    return min(legal, key=lambda card: (card.rank, card.suit))


def pick(legal, suit, rank):
    """The card with this suit and rank if it is legal, else None."""
    for card in legal:
        if card.suit == suit and card.rank == rank:
            return card
    return None


def _spade_queen_cannot_be_beaten(layout: dict) -> bool:
    """Whether the spade queen, played now by the seat on play, wins the
    trick. The seat comes from `layout`, so it cannot disagree with the
    position.

    The queen wins if all three hold:

    1. Spades were led. The contract is 6NT, so nothing can ruff.
    2. No higher spade is already on the trick.
    3. No seat still to play holds the ace or the king.

    Test 3 reads the other hands from `layout`. A model defender may see
    all the cards, as `DoubleDummyDefender` does. A realistic defender would
    use `state` instead.

    The test asks whether the queen *can* be beaten, not whether it will
    be. Otherwise the answer would depend on declarer's strategy.
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
    """delta: play the spade queen when it wins the trick, otherwise low.

    Written for this example's ending only. It is not a general rule.
    """
    del state  # Uses the layout only.
    legal = legal_cards(layout, seat)
    queen = pick(legal, SPADES, QUEEN)
    if queen is not None and _spade_queen_cannot_be_beaten(layout):
        return [(queen, 1.0)]
    return [(_lowest(legal), 1.0)]


def finesse_the_queen_from_the_beliefs(state, view):
    """pi: as `cash_two_hearts_and_play_a_spade`, but read the spade guess
    from the belief space.

    This is the only strategy here that reads `view`. The hearts are forced,
    and covering the queen is automatic. The one real decision is which
    defender to play for the queen. `view` holds that probability.

    `entry.posterior` is the probability of one layout, given the play so
    far. Sum it over the layouts where a defender holds the queen. The
    result is the probability that this defender holds it. At the root both
    defenders have 0.5. The probabilities change only after the defenders'
    play narrows the belief space.

    **`view` is valid only during this call.** After the call, any access to
    it, or to an entry from it, raises `ExpiredBeliefViewError`.
    """
    legal = state.legal_cards
    on_trick = state.current_trick

    if not on_trick:
        lead = pick(legal, HEARTS, ACE) or pick(legal, HEARTS, TEN)
        if lead is not None:
            return lead
        # Leading spades. Lead low, so that the defender with the queen must
        # play before the honour behind them.
        return min(legal, key=lambda card: (card.rank, card.suit))

    if any(c.suit == SPADES and c.rank == QUEEN for c in on_trick):
        king = pick(legal, SPADES, KING)
        if king is not None:
            return king

    # Third hand on a spade: finesse, or play for the drop. `rho` is the
    # defender who has already played to this trick. If rho more likely holds
    # the queen, play the jack. Otherwise play the king.
    if on_trick[0].suit == SPADES:
        # Use `trick_leader`, not `first`. `first` is the root leader and
        # never changes, so it names the wrong defender after trick one.
        rho = (state.trick_leader + state.position_in_trick - 1) % 4
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
# The shape of each callback, with no body. Copy one to start a new
# strategy.


def empty_declarer(state, view):
    """pi, not implemented. Return one Card for the seat on play, declarer
    or dummy.

    The card must be legal in *every* layout at this node. Choose it from
    `state.known_holdings` and it will be: declarer and dummy hold the same
    cards in every layout.
    """
    raise NotImplementedError("declarer strategy")


def empty_defender(layout, seat, state):
    """delta, not implemented. Return the cards `seat` might play from
    `layout`, each with its probability. The probabilities sum to 1.

    Every card must be legal for `seat` in `layout`. List every card the
    defender might play. Leaving a card out means the defender never plays
    it.
    """
    raise NotImplementedError("defender strategy")


# --- the solver seam ------------------------------------------------------


def double_dummy_defender(ctx, policy=None):
    """delta: both defenders play double dummy. They see all four hands.

    `bsle.DoubleDummyDefender` works as delta directly. This wrapper keeps
    its caveats next to the call:

    - **It is not best defence against the contract.** It maximises tricks,
      and sometimes gives up the contract to do so.
    - **A `SolverContext` is not thread-safe.** The solve releases the GIL.
      Use one context, and one of these, per thread.

    The examples do not pair it with `DoubleDummyBound`, because a
    four-card ending with 70 layouts does not need pruning. The pair is
    sound. See "Known gaps" in docs/belief_space_local_evaluation.md.
    """
    import belief_space_local_evaluation as bsle

    if policy is None:
        policy = bsle.SpreadPolicy.TouchingSequence
    return bsle.DoubleDummyDefender(ctx, policy)


def heuristic_defender(ctx, chain, fallback_policy=None):
    """delta: double-dummy candidates, filtered by a chain of defensive
    rules. If every rule in `chain` defers, it falls back to the even spread
    of `DoubleDummyDefender`.

    `bsle.HeuristicDefender` works as delta directly. This wrapper keeps its
    caveats next to the call. The caveats of `double_dummy_defender` apply.

    **You build `chain`.** Pass `bsle.make_default_defender_heuristics(trump)`
    for the shipped order, or a `bsle.DefenderHeuristicChain` built by hand.
    """
    import belief_space_local_evaluation as bsle

    if fallback_policy is None:
        fallback_policy = bsle.SpreadPolicy.TouchingSequence
    return bsle.HeuristicDefender(ctx, chain, fallback_policy)


def brute_force_declarer(ctx, objective=None, opponent_model=None, max_depth=None, max_layouts=None):
    """pi: a lookahead search over the belief space. It keeps the best legal
    card for `objective`. `max_depth` and `max_layouts` limit the search.

    `bsle.BruteForceDeclarer` works as pi directly. This wrapper keeps its
    caveats next to the call:

    - **Match `opponent_model` to the paired delta.** Otherwise `P_make`
      is valid only for that pairing, not for optimal play.
    - **An `opponent_model` you supply must answer from the position
      only.** It must not read `state.history` or `state.play_record`.
      Nothing checks this.
    - With `opponent_model=None`, it owns a `DoubleDummyDefender` over
      `ctx`. The caveats of `double_dummy_defender` apply.

    For the internal cache and the search limits, see `BruteForceDeclarer`
    in docs/belief_space_local_evaluation.md.
    """
    import belief_space_local_evaluation as bsle

    if objective is None:
        objective = bsle.DeclarerObjective.MaximizeExpectedTricks
    return bsle.BruteForceDeclarer(ctx, objective, opponent_model, max_depth, max_layouts)


def remembers_whether_a_heart_was_already_pitched(state, view):
    """pi: play high if a heart was played before the root, otherwise low.

    `state.history` starts at the root, so it cannot show this.
    `state.play_record` holds the full record, so it can. This is not a
    real bridge rule. It shows that `play_record` reaches pi.
    examples/test_strategies.py uses it.
    """
    del view
    deal = state.known_holdings
    seat = seat_on_play(deal)
    legal = legal_cards(deal, seat)

    heart_already_gone = state.play_record is not None and any(
        card.suit == HEARTS for card in state.play_record.cards)

    if heart_already_gone:
        return max(legal, key=lambda c: (c.rank, -c.suit))
    return min(legal, key=lambda c: (c.rank, -c.suit))
