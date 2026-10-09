"""Runs the example, and checks the numbers it prints.

| value | what it is | status |
| --- | --- | --- |
| 252 | layouts in the belief space | golden (fixed by the deal and the history) |
| 10/21 | root prior, South a 2-2 break | golden -- exactly the 2-2 mass `TestPMake` also reads off `always_rise_with_the_ace`'s P_make |
| 5/63 | root prior, South QJ tight | golden |
| 5/84 | root prior, South singleton jack | golden; equal to singleton queen by the north/south symmetry of the prior, before any card is played |
| 5/84 | root prior, South singleton queen (the real deal) | golden |
| 60% | finesse read, queen branch, vs the 50/50 defender | golden |
| 60% | finesse read, jack branch, vs the 50/50 defender | golden -- identical to the queen branch, a fact about genuine randomisation, not a coincidence |
| 60% | both branches, again, against `DoubleDummyDefender`'s own touching-sequence policy | golden -- it agrees with the bespoke 50/50 exactly, on both branches |
| 3/7 | finesse read, queen branch, vs a defender who always shows the queen from the pair | golden -- showing the queen is no longer any tell there, so the posterior is the raw prior odds, not the restricted-choice-adjusted one |
| 100% | finesse read, jack branch, vs that same always-shows-the-queen defender | golden -- that defender can never show the jack while still holding the queen, so the posterior this example reads is provably 1, not merely measured as 1, but the test itself still computes it the same way as every other row above, not by an independent derivation |
| 1.0 / 3/7 | the mirror image, queen/jack, against a defender who always plays its lowest legal card | golden -- "lowest always" prefers the jack (the lower-ranked card) from the pair, so it is the *queen* that becomes the dead giveaway there, not the jack |
| 95/126 | cash the king, then read the beliefs, vs the 50/50 defender | golden |
| 65/126 | the same, vs `DoubleDummyDefender` specifically | golden -- *not* equal to the row above, even though the two agree exactly on the genuine small-card node's own reading (`TestTheFinesseReading`); see `TestAlwaysDucksWhenNorthIsVoid` for why the always-duck-when-void technique behind the gap does not pay off against this one defender |
| 10/21 | always rise with the ace instead, vs the 50/50 defender | golden; 10/21 is exactly the posterior mass of the 2-2 spade breaks in this belief space, and is unchanged by which defender is paired with it, since rising never reaches a node where the belief mattered |
| 65/84 | cash the king, then read the beliefs, vs the always-shows-the-queen defender | golden |
| 55/84 | the same, vs a defender who always plays its lowest legal card | golden; the mirror image of the always-shows-the-queen figure above, by the same mirror symmetry row 15 already describes -- not a second coincidence, but not numerically equal either, since the two defenders' own "void" play differs along with their second-seat one |
| 65/126 | cash the king, then read the beliefs, vs a caller-assembled `HeuristicDefender` chain, `randomize_touching_honors=True` | golden; equal to the `DoubleDummyDefender` row above bit for bit, not a coincidence -- with randomisation on, `second_seat_low` detects every genuine touching pair this ending's own second-seat node reaches and defers there to the chain's own fallback spread, which is exactly `DoubleDummyDefender`'s uniform spread over the same solved position |
| 15/28 | the same, `randomize_touching_honors=False` | golden; **higher**, not equal -- with randomisation off, the same rule always shows the lower card of a touching pair instead, and the belief-reading declarer is measurably able to exploit it |

A belief-space node sharing a South-honour key with another one is not
itself a bug -- North voiding out of spades entirely reaches the same key
with a structurally different, generally much larger `danger`.
`_norths_second_card_is_a_genuine_small_spade` excludes those nodes from
`ace_vs_finesse_readings`'s own result, and
`TestNorthsSecondCardIsAGenuineSmallSpade` pins that directly rather than
relying on the evaluator's own traversal order to keep picking the right
one first. The declarer strategy itself, unlike the reporting helper,
cannot just exclude those nodes -- it still has to play *something*
there -- so it always ducks instead of reading `danger` as a probability
to act on.

That void branch covers two layouts that do not share an outcome, and
neither is the clean "duck wins" story an earlier draft of this file
claimed. When South holds the queen *and* the jack together (with one
small card alongside them, or two), the contract cannot be made by
either choice -- ducking is merely harmless there, not a win. When
North's own singleton is the *other* honour instead, the contract is
double-dummy makeable, but not by this function: the mistake already
happened one trick earlier, at this plan's fixed first lead (always low
to the king, never the ace), which is a bet that South, not North, holds
the dangerous singleton -- a bet this specific layout loses. Ducking at
the void trick still salvages more of what that first-lead mistake left
behind than rising would, but it does not recover the rest. See
`TestAlwaysDucksWhenNorthIsVoid` and
`restricted_choice_6nt_belief_space.cash_the_king_then_read_the_beliefs`'s
own docstring for exactly which layout is which.
"""

import unittest

import dds3

import restricted_choice_6nt_belief_space as example
from bridge_notation import CLUBS, DIAMONDS, EAST, SOUTH, WEST
from strategies import double_dummy_defender, heuristic_defender, lowest_eligible_defender

Card = example.Card
SPADES = example.SPADES
QUEEN, JACK = example.QUEEN, example.JACK
TEN, ACE = example.TEN, example.ACE


class _FakeState:
    """A duck-typed stand-in for `ObservationState`. The real type is
    read-only and not constructible from Python (nothing builds one
    outside the evaluator), so a direct unit test of a function that
    reads one needs a stand-in rather than a real instance. Carries only
    what the functions tested against it actually read: `declarer`,
    `trick_leader` and `known_holdings` for `_lead_spade_or_the_lone_club`
    and `_third_hand_after_the_ace_decision`; `legal_cards`,
    `current_trick` and `position_in_trick` as well, for
    `cash_the_king_then_read_the_beliefs` itself.
    """

    def __init__(
        self, declarer, trick_leader=None, declarer_spades=0,
        legal_cards=None, current_trick=None, position_in_trick=None,
    ):
        self.declarer = declarer
        self.trick_leader = trick_leader
        self.known_holdings = {"remain_cards": [[0, 0, 0, 0] for _ in range(4)]}
        self.known_holdings["remain_cards"][declarer][SPADES] = declarer_spades
        self.legal_cards = legal_cards or []
        self.current_trick = current_trick or []
        self.position_in_trick = position_in_trick


class _FakeEntry:
    """A duck-typed stand-in for `BeliefEntry`: `layout` and `posterior`,
    the only two attributes `_danger_south_still_guards_an_honour` reads.
    """

    def __init__(self, layout, posterior):
        self.layout = layout
        self.posterior = posterior


class _FakeView:
    """A duck-typed stand-in for `BeliefView`: just `entries`, the only
    attribute `_danger_south_still_guards_an_honour` reads.
    """

    def __init__(self, entries):
        self.entries = entries


def _south_second_seat_layout(south_spades: int, leader: int = SOUTH) -> dict:
    """A minimal deal dict with South holding `south_spades` in spades and
    nothing else, for probing a defender function directly. Shared by both
    bespoke-defender test classes below, which differ only in which
    defender function they call, not in what layout they call it with.

    `leader` defaults to South, right for the "as the leader" probes
    (nothing yet on the trick, South about to lead it). A "second seat"
    probe -- one card already on the trick, South about to play next --
    needs a *different* leader: South cannot be both the leader and the
    very next seat to call on the same trick, so those callers pass
    `leader=EAST` instead (East leads, South is the next seat to act).
    `evaluate()` itself would never hand a defender the inconsistent
    pairing "South led this trick" and "South is on play a second time
    before anyone else has" -- this parameter exists so these tests do
    not construct it either.
    """
    layout = {
        "trump": 4,  # notrump
        "first": leader,
        "remain_cards": [[0, 0, 0, 0] for _ in range(4)],
        "current_trick_suit": (0, 0, 0),
        "current_trick_rank": (0, 0, 0),
    }
    layout["remain_cards"][2][SPADES] = south_spades
    return layout


class TestLeadSpadeOrTheLoneClub(unittest.TestCase):
    """`_lead_spade_or_the_lone_club` in isolation, independent of a real
    `evaluate()` run -- the three lead cases its own docstring names, plus
    the no-spades fallback.
    """

    def test_leads_low_while_the_king_is_still_unplayed(self) -> None:
        legal = [Card(SPADES, ACE), Card(SPADES, 9), Card(SPADES, 6), Card(SPADES, 4), Card(SPADES, 2)]
        state = _FakeState(declarer=WEST, declarer_spades=(1 << example.KING))

        self.assertEqual(example._lead_spade_or_the_lone_club(legal, state), Card(SPADES, 2))

    def test_leads_the_ace_once_the_king_is_gone(self) -> None:
        legal = [Card(SPADES, ACE), Card(SPADES, 9), Card(SPADES, 6), Card(SPADES, 4)]
        state = _FakeState(declarer=WEST, declarer_spades=0)

        self.assertEqual(example._lead_spade_or_the_lone_club(legal, state), Card(SPADES, ACE))

    def test_leads_the_higher_small_card_keeping_the_lower_as_the_spare(self) -> None:
        legal = [Card(SPADES, TEN), Card(SPADES, 7), Card(SPADES, 3)]
        state = _FakeState(declarer=WEST, declarer_spades=0)

        self.assertEqual(example._lead_spade_or_the_lone_club(legal, state), Card(SPADES, 7))

    def test_leads_the_lone_club_once_there_is_no_spade_left(self) -> None:
        legal = [Card(CLUBS, 8)]
        state = _FakeState(declarer=WEST, declarer_spades=0)

        self.assertEqual(example._lead_spade_or_the_lone_club(legal, state), Card(CLUBS, 8))


class TestThirdHandAfterTheAceDecision(unittest.TestCase):
    """`_third_hand_after_the_ace_decision` in isolation: the ten is
    unloaded whenever nothing beats it, or whenever only our own
    partner's card does, and kept back only once a defender's own card
    has genuinely beaten it; past the ten, the cheapest winning card is
    played rather than the lowest.

    The function's one real call site
    (`cash_the_king_then_read_the_beliefs`) only ever reaches it with
    `state.position_in_trick == 2`, so `on_trick` always has exactly two
    entries (the leader's card and the second seat's), and the leader is
    always `declarer` or `dummy` -- never a defender, since this helper is
    declarer's own third-hand decision. Every case below keeps that
    shape, rather than the shorter, leaderless `on_trick`s an earlier
    version of this test used, which this function is never actually
    called with.
    """

    def test_plays_the_ten_when_nothing_on_the_trick_beats_it(self) -> None:
        legal = [Card(SPADES, TEN), Card(SPADES, 3)]
        on_trick = [Card(SPADES, 4), Card(SPADES, 6)]  # West's lead, North's low follow
        state = _FakeState(declarer=WEST, trick_leader=WEST)

        self.assertEqual(
            example._third_hand_after_the_ace_decision(legal, on_trick, state, EAST),
            Card(SPADES, TEN))

    def test_unloads_the_ten_once_its_own_partners_lead_already_won(self) -> None:
        legal = [Card(SPADES, TEN), Card(SPADES, 3)]
        # East (dummy) led the ace -- cash_the_king_then_read_the_beliefs's own
        # lead fix, once the king is gone -- South, a defender, follows low.
        on_trick = [Card(SPADES, ACE), Card(SPADES, 8)]
        state = _FakeState(declarer=WEST, trick_leader=EAST)

        self.assertEqual(
            example._third_hand_after_the_ace_decision(legal, on_trick, state, EAST),
            Card(SPADES, TEN))

    def test_unloads_the_ten_even_when_a_defenders_honour_also_shows(self) -> None:
        # The exact scenario a code-review bot caught: East (dummy) led the
        # ace, and a defender's honour (here the jack) also appears on the
        # trick before this decision. The ace already beats the jack
        # outright, so the ten is still safe to unload -- checking only
        # "did a defender beat the ten" would wrongly keep it back here,
        # since the jack *does* beat the ten even though it never had a
        # chance against the ace.
        legal = [Card(SPADES, TEN), Card(SPADES, 3)]
        on_trick = [Card(SPADES, ACE), Card(SPADES, JACK)]
        state = _FakeState(declarer=WEST, trick_leader=EAST)

        self.assertEqual(
            example._third_hand_after_the_ace_decision(legal, on_trick, state, EAST),
            Card(SPADES, TEN))

    def test_keeps_the_ten_back_once_a_defenders_card_has_beaten_it(self) -> None:
        legal = [Card(SPADES, TEN), Card(SPADES, 3)]
        on_trick = [Card(SPADES, 4), Card(SPADES, JACK)]  # West's lead, North's jack
        state = _FakeState(declarer=WEST, trick_leader=WEST)

        self.assertEqual(
            example._third_hand_after_the_ace_decision(legal, on_trick, state, EAST),
            Card(SPADES, 3))

    def test_plays_the_cheapest_card_that_still_wins_once_the_ten_is_gone(self) -> None:
        legal = [Card(SPADES, 9), Card(SPADES, 6)]
        on_trick = [Card(SPADES, 4), Card(SPADES, 5)]  # West's lead, North's low follow
        state = _FakeState(declarer=WEST, trick_leader=WEST)

        self.assertEqual(
            example._third_hand_after_the_ace_decision(legal, on_trick, state, EAST),
            Card(SPADES, 6))

    def test_plays_its_lowest_once_nothing_left_can_win(self) -> None:
        legal = [Card(SPADES, 4)]
        on_trick = [Card(SPADES, 6), Card(SPADES, 9)]  # West's lead, North's high follow
        state = _FakeState(declarer=WEST, trick_leader=WEST)

        self.assertEqual(
            example._third_hand_after_the_ace_decision(legal, on_trick, state, EAST),
            Card(SPADES, 4))


class TestAlwaysDucksWhenNorthIsVoid(unittest.TestCase):
    """`cash_the_king_then_read_the_beliefs` itself, at the one node a
    previous review round caught: North already void and discarding, not
    following with a genuine small card. `_danger_south_still_guards_an_
    honour` always reads 1.0 there, but that reading never actually gets
    consulted here -- `_norths_second_card_is_a_genuine_small_spade` gates
    the rise before `danger` is even looked at, so this branch always
    ducks regardless of South's exact remaining holding. Both tests below
    exercise that same single code path with different bit patterns, not
    two different branches: the point is that the rule does not change
    just because the holding looks different.

    Neither test is "duck wins here" -- that is not the same claim for
    every layout this branch covers. South holding the queen and the jack
    together makes the contract unmakeable regardless of this choice
    (ducking is merely harmless there, not a win); North's own singleton
    being the other honour makes the layout makeable double-dummy, but
    not by this plan, whose mistake already happened one trick earlier at
    the fixed king-first lead -- see `cash_the_king_then_read_the_
    beliefs`'s own docstring for why. Ducking still salvages more than
    rising would in that second layout, but the test below checking it
    only confirms the rule is applied uniformly, not that it wins.
    """

    def _decide(self, souths_remaining_spades: int) -> Card:
        legal = [Card(SPADES, ACE), Card(SPADES, 9), Card(SPADES, 6), Card(SPADES, 4)]
        on_trick = [Card(SPADES, 3), Card(DIAMONDS, 3)]  # West's lead, North's void discard
        state = _FakeState(
            declarer=WEST, trick_leader=WEST, position_in_trick=2,
            legal_cards=legal, current_trick=on_trick)
        layout = {"remain_cards": [[0, 0, 0, 0] for _ in range(4)]}
        layout["remain_cards"][SOUTH][SPADES] = souths_remaining_spades
        view = _FakeView([_FakeEntry(layout, 1.0)])

        return example.cash_the_king_then_read_the_beliefs(state, view)

    def test_ducks_when_south_holds_the_jack_and_a_small_card(self) -> None:
        # Reachable two different ways -- South started with the queen,
        # the jack, and one small card and already shed the queen on
        # round one (unmakeable regardless of this trick's choice), or
        # North's own singleton was the queen and South started with the
        # jack and both small cards (makeable double-dummy, but not by
        # this plan's fixed king-first lead). The function cannot tell
        # these apart from this node alone, and does not need to: it
        # ducks either way.
        card = self._decide((1 << JACK) | (1 << 8))

        self.assertEqual(card, Card(SPADES, example.NINE))

    def test_ducks_when_south_holds_all_four_too(self) -> None:
        # South holds every missing card; the contract cannot be made
        # regardless of this decision (see this function's own
        # docstring), but the rule is still "always duck" here, not
        # "duck only when it helps" -- there is nothing in `danger` that
        # distinguishes this layout from the one above.
        card = self._decide((1 << QUEEN) | (1 << JACK) | (1 << 8) | (1 << 5))

        self.assertEqual(card, Card(SPADES, example.NINE))


class TestNorthsSecondCardIsAGenuineSmallSpade(unittest.TestCase):
    """`_norths_second_card_is_a_genuine_small_spade` in isolation: the
    regression guard behind the fix for a belief-space node mix-up a
    previous review round caught (a void discard from North, reached
    whenever North is already out of spades, was being folded into the
    same reading as North genuinely following with a small spade).
    """

    def test_a_small_spade_is_genuine(self) -> None:
        on_trick = [Card(SPADES, 4), Card(SPADES, 8)]

        self.assertTrue(example._norths_second_card_is_a_genuine_small_spade(on_trick))

    def test_a_discard_from_a_void_is_not(self) -> None:
        on_trick = [Card(SPADES, 4), Card(DIAMONDS, 3)]

        self.assertFalse(example._norths_second_card_is_a_genuine_small_spade(on_trick))


class TestTheEnding(unittest.TestCase):
    def test_eight_tricks_are_played_and_declarer_needs_the_rest(self) -> None:
        sequence = example.restricted_choice_6nt()

        self.assertEqual(len(sequence.completed_tricks), 8)
        self.assertEqual(sequence.tricks_won_by_declarer, 7)
        self.assertEqual(sequence.tricks_needed, 5)


class TestTheBeliefSpace(unittest.TestCase):
    def test_the_history_rules_out_nothing_further_here(self) -> None:
        sequence = example.restricted_choice_6nt()
        root = sequence.current_deal
        record = example.bsle.PlayRecord(sequence.history, sequence.opening_leader)

        with_history = example.bsle.ExhaustiveLayoutSource(
            root, sequence.declarer, example.SEED, record=record)
        without_history = example.bsle.ExhaustiveLayoutSource(root, sequence.declarer, example.SEED)

        self.assertEqual(with_history.size(), 252)
        self.assertEqual(with_history.size(), without_history.size())


class TestTheSpadeSplitFrequencies(unittest.TestCase):
    def test_the_four_named_splits_match_the_raw_combinatorics(self) -> None:
        sequence = example.restricted_choice_6nt()
        root = sequence.current_deal
        record = example.bsle.PlayRecord(sequence.history, sequence.opening_leader)
        source = example.bsle.ExhaustiveLayoutSource(root, sequence.declarer, example.SEED, record=record)

        frequencies = example.spade_split_frequencies(sequence, source)

        self.assertAlmostEqual(frequencies["2-2"], 10 / 21)
        self.assertAlmostEqual(frequencies["south QJ tight"], 5 / 63)
        self.assertAlmostEqual(frequencies["south singleton J"], 5 / 84)
        self.assertAlmostEqual(frequencies["south singleton Q"], 5 / 84)
        # North/South are symmetric in the prior -- nothing has been played
        # yet to tell them apart.
        self.assertAlmostEqual(frequencies["south singleton J"], frequencies["south singleton Q"])


class TestTheBespokeDefender(unittest.TestCase):
    """`randomises_queen_jack_in_second_seat` in isolation, independent of
    the belief space -- a defender holding Q and J together, second seat,
    really does split 50/50, and plays low everywhere else.
    """

    def test_it_splits_50_50_holding_the_queen_and_the_jack_second_seat(self) -> None:
        layout = _south_second_seat_layout((1 << QUEEN) | (1 << JACK), leader=EAST)
        layout["current_trick_suit"] = (SPADES, 0, 0)
        layout["current_trick_rank"] = (2, 0, 0)  # one card already led

        distribution = example.randomises_queen_jack_in_second_seat(layout, 2, None)

        self.assertEqual(
            {(card.suit, card.rank): probability for card, probability in distribution},
            {(SPADES, QUEEN): 0.5, (SPADES, JACK): 0.5})

    def test_it_plays_low_holding_only_the_queen_second_seat(self) -> None:
        layout = _south_second_seat_layout((1 << QUEEN) | (1 << 5), leader=EAST)
        layout["current_trick_suit"] = (SPADES, 0, 0)
        layout["current_trick_rank"] = (2, 0, 0)

        distribution = example.randomises_queen_jack_in_second_seat(layout, 2, None)

        self.assertEqual(distribution, [(Card(SPADES, 5), 1.0)])

    def test_it_plays_low_holding_the_queen_and_the_jack_as_the_leader(self) -> None:
        layout = _south_second_seat_layout((1 << QUEEN) | (1 << JACK))

        distribution = example.randomises_queen_jack_in_second_seat(layout, 2, None)

        self.assertEqual(distribution, [(Card(SPADES, JACK), 1.0)])

    def test_it_plays_low_void_in_second_seat_on_a_non_spade_trick(self) -> None:
        # Second seat, one card already played, and both honours legal --
        # but only because this defender is void in the suit actually led
        # (diamonds here) and every card, including both spade honours,
        # is consequently a legal discard. This is not a second-seat
        # spade decision at all, and must not randomise as if it were.
        layout = _south_second_seat_layout((1 << QUEEN) | (1 << JACK), leader=EAST)
        layout["current_trick_suit"] = (DIAMONDS, 0, 0)
        layout["current_trick_rank"] = (5, 0, 0)

        distribution = example.randomises_queen_jack_in_second_seat(layout, 2, None)

        self.assertEqual(distribution, [(Card(SPADES, JACK), 1.0)])


class TestTheAlwaysShowsTheQueenDefender(unittest.TestCase):
    """`always_shows_the_queen_from_qj_in_second_seat`, the deliberate
    contrast with the 50/50 defender above: same holding, same seat, but
    deterministically the queen every time.
    """

    def test_it_always_shows_the_queen_holding_the_pair_second_seat(self) -> None:
        layout = _south_second_seat_layout((1 << QUEEN) | (1 << JACK), leader=EAST)
        layout["current_trick_suit"] = (SPADES, 0, 0)
        layout["current_trick_rank"] = (2, 0, 0)  # one card already led

        distribution = example.always_shows_the_queen_from_qj_in_second_seat(layout, 2, None)

        self.assertEqual(distribution, [(Card(SPADES, QUEEN), 1.0)])

    def test_it_plays_low_holding_only_the_queen_second_seat(self) -> None:
        layout = _south_second_seat_layout((1 << QUEEN) | (1 << 5), leader=EAST)
        layout["current_trick_suit"] = (SPADES, 0, 0)
        layout["current_trick_rank"] = (2, 0, 0)

        distribution = example.always_shows_the_queen_from_qj_in_second_seat(layout, 2, None)

        self.assertEqual(distribution, [(Card(SPADES, 5), 1.0)])

    def test_it_plays_low_holding_the_queen_and_the_jack_as_the_leader(self) -> None:
        layout = _south_second_seat_layout((1 << QUEEN) | (1 << JACK))

        distribution = example.always_shows_the_queen_from_qj_in_second_seat(layout, 2, None)

        self.assertEqual(distribution, [(Card(SPADES, JACK), 1.0)])

    def test_it_plays_low_void_in_second_seat_on_a_non_spade_trick(self) -> None:
        # Mirrors the same regression test on the 50/50 defender above:
        # second seat, both honours legal, but only because this
        # defender is void in the suit actually led.
        layout = _south_second_seat_layout((1 << QUEEN) | (1 << JACK), leader=EAST)
        layout["current_trick_suit"] = (DIAMONDS, 0, 0)
        layout["current_trick_rank"] = (5, 0, 0)

        distribution = example.always_shows_the_queen_from_qj_in_second_seat(layout, 2, None)

        self.assertEqual(distribution, [(Card(SPADES, JACK), 1.0)])


class TestTheFinesseReading(unittest.TestCase):
    """`cash_the_king_then_read_the_beliefs`'s one belief-dependent number,
    for each of the two branches it tracks: the probability that the
    finesse is correct once North has followed with their last possible
    small card, keyed by which honour South showed on the first round.
    """

    def _readings(self, delta) -> dict:
        sequence = example.restricted_choice_6nt()
        root = sequence.current_deal
        record = example.bsle.PlayRecord(sequence.history, sequence.opening_leader)
        source = example.bsle.ExhaustiveLayoutSource(root, sequence.declarer, example.SEED, record=record)

        return example.ace_vs_finesse_readings(sequence, source, delta)

    def test_it_is_60_percent_either_way_against_the_bespoke_defender(self) -> None:
        readings = self._readings(example.randomises_queen_jack_in_second_seat)

        self.assertAlmostEqual(readings[example.QUEEN], 0.6)
        self.assertAlmostEqual(readings[example.JACK], 0.6)

    def test_double_dummy_touching_sequence_agrees_exactly(self) -> None:
        ctx = dds3.SolverContext()
        readings = self._readings(double_dummy_defender(ctx))

        self.assertAlmostEqual(readings[example.QUEEN], 0.6)
        self.assertAlmostEqual(readings[example.JACK], 0.6)

    def test_queen_is_certain_and_jack_is_3_of_7_against_lowest_always(self) -> None:
        # The mirror image of the always-shows-the-queen defender below:
        # "lowest always" prefers the *lower*-ranked card of a touching
        # pair, so it is the jack that is never shown while the queen is
        # held back, making a shown queen the certain (forced) signal here.
        readings = self._readings(lowest_eligible_defender)

        self.assertAlmostEqual(readings[example.QUEEN], 1.0)
        self.assertAlmostEqual(readings[example.JACK], 3 / 7)

    def test_jack_is_certain_and_queen_is_3_of_7_against_always_shows_the_queen(self) -> None:
        readings = self._readings(example.always_shows_the_queen_from_qj_in_second_seat)

        self.assertAlmostEqual(readings[example.JACK], 1.0)
        self.assertAlmostEqual(readings[example.QUEEN], 3 / 7)


class TestPMake(unittest.TestCase):
    def test_reading_the_beliefs_scores_higher_than_always_rising_here(self) -> None:
        sequence = example.restricted_choice_6nt()
        root = sequence.current_deal
        record = example.bsle.PlayRecord(sequence.history, sequence.opening_leader)
        source = example.bsle.ExhaustiveLayoutSource(root, sequence.declarer, example.SEED, record=record)
        defence = example.randomises_queen_jack_in_second_seat

        belief_value = example.evaluate(sequence, source, example.cash_the_king_then_read_the_beliefs, defence)
        ace_value = example.evaluate(sequence, source, example.always_rise_with_the_ace, defence)

        self.assertAlmostEqual(belief_value["p_make"], 95 / 126)
        self.assertAlmostEqual(ace_value["p_make"], 10 / 21)
        self.assertGreater(belief_value["p_make"], ace_value["p_make"])

    def test_always_rising_is_exactly_the_2_2_mass_regardless_of_defender(self) -> None:
        sequence = example.restricted_choice_6nt()
        root = sequence.current_deal
        record = example.bsle.PlayRecord(sequence.history, sequence.opening_leader)
        source = example.bsle.ExhaustiveLayoutSource(root, sequence.declarer, example.SEED, record=record)

        ctx = dds3.SolverContext()
        for defence in (
            example.randomises_queen_jack_in_second_seat,
            example.always_shows_the_queen_from_qj_in_second_seat,
            lowest_eligible_defender,
            double_dummy_defender(ctx),
        ):
            ace_value = example.evaluate(sequence, source, example.always_rise_with_the_ace, defence)
            self.assertAlmostEqual(ace_value["p_make"], 10 / 21)

    def test_reading_the_beliefs_still_beats_always_rising_under_double_dummy(self) -> None:
        # The module docstring's "every defender measured here" claim,
        # pinned directly for DoubleDummyDefender rather than left to be
        # inferred from two separately-asserted numbers. This no longer
        # matches test_reading_the_beliefs_scores_higher_than_always_rising_
        # here's own 95/126: DoubleDummyDefender reads the genuine
        # small-card node exactly the way the bespoke 50/50 defender does,
        # but the always-duck-when-void technique does not pay off against
        # it the way it does against the other three defenders, since that
        # technique relies on the defender's own remaining cards coming out
        # lowest-first, which a real double-dummy defender is not obliged
        # to do. 65/126 is this defender's own number, not a derived one.
        sequence = example.restricted_choice_6nt()
        root = sequence.current_deal
        record = example.bsle.PlayRecord(sequence.history, sequence.opening_leader)
        source = example.bsle.ExhaustiveLayoutSource(root, sequence.declarer, example.SEED, record=record)
        ctx = dds3.SolverContext()
        defence = double_dummy_defender(ctx)

        belief_value = example.evaluate(sequence, source, example.cash_the_king_then_read_the_beliefs, defence)
        ace_value = example.evaluate(sequence, source, example.always_rise_with_the_ace, defence)

        self.assertAlmostEqual(belief_value["p_make"], 65 / 126)
        self.assertAlmostEqual(ace_value["p_make"], 10 / 21)
        self.assertGreater(belief_value["p_make"], ace_value["p_make"])

    def test_against_the_no_tell_defender_the_read_still_beats_always_rising(self) -> None:
        sequence = example.restricted_choice_6nt()
        root = sequence.current_deal
        record = example.bsle.PlayRecord(sequence.history, sequence.opening_leader)
        source = example.bsle.ExhaustiveLayoutSource(root, sequence.declarer, example.SEED, record=record)
        defence = example.always_shows_the_queen_from_qj_in_second_seat

        belief_value = example.evaluate(sequence, source, example.cash_the_king_then_read_the_beliefs, defence)
        ace_value = example.evaluate(sequence, source, example.always_rise_with_the_ace, defence)

        self.assertAlmostEqual(belief_value["p_make"], 65 / 84)
        self.assertAlmostEqual(ace_value["p_make"], 10 / 21)
        self.assertGreater(belief_value["p_make"], ace_value["p_make"])

    def test_the_read_still_beats_always_rising_against_lowest_always(self) -> None:
        # The fourth defender the module docstring's "every defender
        # measured here" claim covers -- pinned directly, the same way
        # the other three already are, rather than leaving it as the one
        # case nothing in this file actually asserts.
        sequence = example.restricted_choice_6nt()
        root = sequence.current_deal
        record = example.bsle.PlayRecord(sequence.history, sequence.opening_leader)
        source = example.bsle.ExhaustiveLayoutSource(root, sequence.declarer, example.SEED, record=record)
        defence = lowest_eligible_defender

        belief_value = example.evaluate(sequence, source, example.cash_the_king_then_read_the_beliefs, defence)
        ace_value = example.evaluate(sequence, source, example.always_rise_with_the_ace, defence)

        self.assertAlmostEqual(belief_value["p_make"], 55 / 84)
        self.assertAlmostEqual(ace_value["p_make"], 10 / 21)
        self.assertGreater(belief_value["p_make"], ace_value["p_make"])

    def test_the_randomize_touching_honors_toggle_moves_this_ending(self) -> None:
        # With randomisation on, second_seat_low detects every genuine
        # touching pair this ending's own second-seat node ever reaches
        # and defers there to the chain's fallback spread, which is
        # exactly DoubleDummyDefender's own uniform spread over the same
        # solved position -- so the two agree exactly, not just to four
        # decimal places (65/126, the same fraction
        # test_reading_the_beliefs_still_beats_always_rising_under_double_dummy
        # already pins for DoubleDummyDefender itself). With it off, the
        # same rule always shows the lower card of a touching pair
        # instead, and the belief-reading declarer is measurably able to
        # exploit that -- a higher P_make, not a lower one.
        sequence = example.restricted_choice_6nt()
        root = sequence.current_deal
        record = example.bsle.PlayRecord(sequence.history, sequence.opening_leader)
        source = example.bsle.ExhaustiveLayoutSource(root, sequence.declarer, example.SEED, record=record)
        ctx = dds3.SolverContext()

        with_value = example.evaluate(
            sequence, source, example.cash_the_king_then_read_the_beliefs,
            heuristic_defender(ctx, example.bsle.make_default_defender_heuristics(
                root["trump"], randomize_touching_honors=True)))
        without_value = example.evaluate(
            sequence, source, example.cash_the_king_then_read_the_beliefs,
            heuristic_defender(ctx, example.bsle.make_default_defender_heuristics(
                root["trump"], randomize_touching_honors=False)))

        self.assertAlmostEqual(with_value["p_make"], 65 / 126)
        self.assertAlmostEqual(without_value["p_make"], 15 / 28)
        self.assertGreater(without_value["p_make"], with_value["p_make"])


class TestTheScriptRuns(unittest.TestCase):
    def test_main_runs_and_reports_both_p_makes(self) -> None:
        import io
        from contextlib import redirect_stdout

        out = io.StringIO()
        with redirect_stdout(out):
            example.main()

        doc = out.getvalue()
        self.assertIn("P_make = 0.7540", doc)
        self.assertIn("P_make = 0.4762", doc)
        self.assertIn("P_make = 0.5159", doc)
        self.assertIn("P_make = 0.7738", doc)
        self.assertIn("P_make = 0.6548", doc)
        self.assertIn("P_make = 0.5357", doc)
        self.assertIn("47.62%", doc)
        self.assertIn("7.94%", doc)
        self.assertIn("5.95%", doc)
        self.assertIn("ace 40%  --  finesse 60%", doc)  # both branches, bespoke 50/50
        self.assertIn("ace 57%  --  finesse 43%", doc)  # queen branch, always-shows-the-queen
        self.assertIn("ace 0%  --  finesse 100%", doc)  # jack branch, always-shows-the-queen

        # The fourth defender the module docstring promises is actually
        # run and printed, not only exercised in this test file -- the
        # specific regression a previous review round caught.
        self.assertIn("never hides a jack behind a queen", doc)
        self.assertIn("exact mirror of the always-shows-the-queen defender", doc)

        # The heuristic defender chain is actually run and printed too,
        # including the minimal standalone illustration of the mechanism
        # -- not only exercised directly in TestPMake.
        self.assertIn("caller-assembled heuristic chain", doc)
        self.assertIn("matching the `DoubleDummyDefender` row above bit for bit", doc)
        self.assertIn("♠J 50%, ♠Q 50%", doc)
        self.assertIn("♠J 100%", doc)


if __name__ == "__main__":
    unittest.main()
