"""Runs the example, and checks the numbers it prints.

All values are golden: fixed by the deal, the history and the strategies.

| value | what it is | note |
| --- | --- | --- |
| 252 | layouts in the belief space | |
| 10/21 | root prior, 2-2 break | |
| 5/63 | root prior, South QJ tight | |
| 5/84 | root prior, South singleton jack | equals singleton queen: North and South are symmetric before any card is played |
| 5/84 | root prior, South singleton queen (the real deal) | |
| 60% | finesse read, queen branch, vs the 50/50 defender | |
| 60% | finesse read, jack branch, vs the 50/50 defender | equals the queen branch, because the defender randomises |
| 60% | both branches, vs `DoubleDummyDefender` | matches the 50/50 defender |
| 3/7 | finesse read, queen branch, vs always-shows-the-queen | the queen tells nothing, so this is the prior odds |
| 100% | finesse read, jack branch, vs always-shows-the-queen | that defender never shows the jack while holding the queen |
| 1.0 / 3/7 | queen / jack branch, vs lowest-always | the mirror image: this defender shows the jack from the pair |
| 95/126 | read the beliefs, vs the 50/50 defender | |
| 65/126 | read the beliefs, vs `DoubleDummyDefender` | lower, although the finesse reading matches; see `TestAlwaysDucksWhenNorthIsVoid` |
| 10/21 | always rise with the ace, vs any defender | the 2-2 mass: rising never reaches a node where the belief matters |
| 65/84 | read the beliefs, vs always-shows-the-queen | |
| 55/84 | read the beliefs, vs lowest-always | the mirror of 65/84, but not equal: the two defenders also play differently when void |
| 65/126 | read the beliefs, vs `HeuristicDefender`, `randomize_touching_honors=True` | equals `DoubleDummyDefender` exactly: `second_seat_low` defers to the same fallback spread |
| 15/28 | the same, `randomize_touching_honors=False` | higher: showing the lower card leaks information |
| 5/42 | `BruteForceDeclarer`, `max_layouts=3, max_depth=3`, vs `DoubleDummyDefender` | measured; far from 0.5159 and from the unlimited search (0.5952), because only 3 of 252 layouts survive pruning |

Two belief-space nodes can share a South-honour key: a small-spade node
and a node where North is void. The void node has a much larger
`danger`. `_norths_second_card_is_a_genuine_small_spade` keeps void nodes
out of `ace_vs_finesse_readings`, and
`TestNorthsSecondCardIsAGenuineSmallSpade` checks it. The declarer
strategy must still play a card at a void node, so it always ducks there.
The module docstring of the example explains the two void layouts.
"""

import unittest

import dds3

import restricted_choice_6nt_belief_space as example
from bridge_notation import CLUBS, DIAMONDS, EAST, SOUTH, WEST
from strategies import (
    brute_force_declarer,
    double_dummy_defender,
    heuristic_defender,
    lowest_eligible_defender,
)

Card = example.Card
SPADES = example.SPADES
QUEEN, JACK = example.QUEEN, example.JACK
TEN, ACE = example.TEN, example.ACE


class _FakeState:
    """A stand-in for `ObservationState`, which Python cannot construct.
    It has only the fields the tested functions read.
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
    """A stand-in for `BeliefEntry`, with `layout` and `posterior`."""

    def __init__(self, layout, posterior):
        self.layout = layout
        self.posterior = posterior


class _FakeView:
    """A stand-in for `BeliefView`, with `entries` only."""

    def __init__(self, entries):
        self.entries = entries


def _south_second_seat_layout(south_spades: int, leader: int = SOUTH) -> dict:
    """A minimal deal in which South holds only `south_spades`.

    `leader` defaults to South, for tests where South leads. For a second
    seat test, pass `leader=EAST`, so that South plays next. South cannot
    both lead and play second to the same trick.
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
    """`_lead_spade_or_the_lone_club`, without `evaluate()`: its three
    lead cases, and the fallback with no spades.
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
    """`_third_hand_after_the_ace_decision`, without `evaluate()`.

    - The ten is played when nothing beats it, or when only our partner's
      card does.
    - The ten is kept when a defender's card beats it.
    - Without the ten, the cheapest winning card is played.

    Each case matches the real call: `on_trick` holds two cards, and
    declarer or dummy led.
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
        # East (dummy) led the ace, because the king is gone. South follows
        # low.
        on_trick = [Card(SPADES, ACE), Card(SPADES, 8)]
        state = _FakeState(declarer=WEST, trick_leader=EAST)

        self.assertEqual(
            example._third_hand_after_the_ace_decision(legal, on_trick, state, EAST),
            Card(SPADES, TEN))

    def test_unloads_the_ten_even_when_a_defenders_honour_also_shows(self) -> None:
        # East (dummy) led the ace, and a defender played the jack. The jack
        # beats the ten but not the ace, so unload the ten.
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
    """`cash_the_king_then_read_the_beliefs` when North is void and
    discards.

    `danger` is 1.0 here, but the strategy never acts on it:
    `_norths_second_card_is_a_genuine_small_spade` blocks the rise first.
    So it always ducks. Both tests use the same code path with different
    South holdings. They check that the rule is the same, not that ducking
    wins. The example module docstring explains both layouts.
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
        # Two layouts reach this node:
        # - South had QJ and one small card, and played the queen in round
        #   one. The contract cannot make.
        # - North had the singleton queen. South had the jack and both small
        #   cards. Makeable double-dummy, but not after the king lead.
        # The strategy cannot tell them apart here. It ducks either way.
        card = self._decide((1 << JACK) | (1 << 8))

        self.assertEqual(card, Card(SPADES, example.NINE))

    def test_ducks_when_south_holds_all_four_too(self) -> None:
        # South holds every missing card, so the contract cannot make. The
        # strategy still ducks: `danger` is the same as in the test above.
        card = self._decide((1 << QUEEN) | (1 << JACK) | (1 << 8) | (1 << 5))

        self.assertEqual(card, Card(SPADES, example.NINE))


class TestNorthsSecondCardIsAGenuineSmallSpade(unittest.TestCase):
    """`_norths_second_card_is_a_genuine_small_spade`: a discard from a
    void must not count as a small spade.
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
        # North and South are symmetric before any card is played.
        self.assertAlmostEqual(frequencies["south singleton J"], frequencies["south singleton Q"])


class TestTheBespokeDefender(unittest.TestCase):
    """`randomises_queen_jack_in_second_seat`: with Q and J in second seat
    it splits 50/50. Everywhere else it plays low.
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
        # Diamonds were led and South is void, so both spade honours are
        # legal discards. This is not a spade decision. Do not randomise.
        layout = _south_second_seat_layout((1 << QUEEN) | (1 << JACK), leader=EAST)
        layout["current_trick_suit"] = (DIAMONDS, 0, 0)
        layout["current_trick_rank"] = (5, 0, 0)

        distribution = example.randomises_queen_jack_in_second_seat(layout, 2, None)

        self.assertEqual(distribution, [(Card(SPADES, JACK), 1.0)])


class TestTheAlwaysShowsTheQueenDefender(unittest.TestCase):
    """`always_shows_the_queen_from_qj_in_second_seat`: same holding and
    seat as the 50/50 defender, but always the queen.
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
        # As for the 50/50 defender: void in the suit led, so not a spade
        # decision.
        layout = _south_second_seat_layout((1 << QUEEN) | (1 << JACK), leader=EAST)
        layout["current_trick_suit"] = (DIAMONDS, 0, 0)
        layout["current_trick_rank"] = (5, 0, 0)

        distribution = example.always_shows_the_queen_from_qj_in_second_seat(layout, 2, None)

        self.assertEqual(distribution, [(Card(SPADES, JACK), 1.0)])


class TestTheFinesseReading(unittest.TestCase):
    """The probability that the finesse is right, once North has followed
    with their last small spade. Keyed by the honour South showed in round
    one.
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
        # The mirror of the always-shows-the-queen defender. Lowest-always
        # plays the jack from the pair, so a shown queen must be bare.
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
        # The example claims the read beats always rising against every
        # defender. This checks it for DoubleDummyDefender. 65/126 is lower
        # than 95/126: ducking when North is void gains only against a
        # defender who sheds small cards lowest first.
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
        # The same claim, for the fourth defender.
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
        # On: second_seat_low defers to the fallback spread of
        # DoubleDummyDefender, so P_make equals that defender's 65/126.
        # Off: the rule shows the lower card of the pair, and the declarer
        # exploits the leak, so P_make is higher.
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

    def test_the_brute_force_declarer_capped_this_aggressively_lands_far_from_either_anchor(
        self,
    ) -> None:
        # A measured value, not a derived one. The limits keep this test to
        # a few seconds; an unlimited search takes about 104s. Only 3 of 252
        # layouts survive pruning, and equal posteriors make the choice
        # close to arbitrary. So the result is far from both 0.5159 and the
        # unlimited 0.5952.
        sequence = example.restricted_choice_6nt()
        root = sequence.current_deal
        record = example.bsle.PlayRecord(sequence.history, sequence.opening_leader)
        source = example.bsle.ExhaustiveLayoutSource(root, sequence.declarer, example.SEED, record=record)
        ctx = dds3.SolverContext()
        defense = double_dummy_defender(ctx)

        brute_force_value = example.evaluate(
            sequence, source,
            brute_force_declarer(
                ctx, objective=example.bsle.DeclarerObjective.MaximizeProbabilityToMake,
                opponent_model=defense, max_layouts=3, max_depth=3),
            defense)

        self.assertAlmostEqual(brute_force_value["p_make"], 5 / 42)


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

        # The fourth defender is run and printed.
        self.assertIn("never hides a jack behind a queen", doc)
        self.assertIn("exact mirror of the always-shows-the-queen defender", doc)

        # The heuristic chain and the minimal position are run and printed.
        self.assertIn("caller-assembled heuristic chain", doc)
        self.assertIn("matching the `DoubleDummyDefender` row above bit for bit", doc)
        self.assertIn("♠J 50%, ♠Q 50%", doc)
        self.assertIn("♠J 100%", doc)


if __name__ == "__main__":
    unittest.main()
