"""Runs the example, and checks the numbers it prints.

| value | what it is | status |
| --- | --- | --- |
| 252 | layouts in the belief space | golden (fixed by the deal and the history) |
| 10/21 | root prior, South a 2-2 break | golden -- exactly the 2-2 mass `TestPMake` also reads off `always_rise_with_the_ace`'s P_make |
| 5/63 | root prior, South QJ tight | golden |
| 5/84 | root prior, South singleton jack | golden; equal to singleton queen by the north/south symmetry of the prior, before any card is played |
| 5/84 | root prior, South singleton queen (the real deal) | golden |
| 60% | finesse read, queen branch, vs the 50/50 defender | **re-derived** below from the raw combinatorics, not just measured |
| 60% | finesse read, jack branch, vs the 50/50 defender | golden -- identical to the queen branch, a fact about genuine randomisation, not a coincidence |
| 60% | both branches, again, against `DoubleDummyDefender`'s own touching-sequence policy | golden -- it agrees with the bespoke 50/50 exactly, on both branches |
| 3/7 | finesse read, queen branch, vs a defender who always shows the queen from the pair | golden -- showing the queen is no longer any tell there, so the posterior is the raw prior odds, not the restricted-choice-adjusted one |
| 100% | finesse read, jack branch, vs that same always-shows-the-queen defender | re-derived -- that defender can never show the jack while still holding the queen, so the posterior this example reads is provably 1, not merely measured as 1 |
| 1.0 / 3/7 | the mirror image, queen/jack, against a defender who always plays its lowest legal card | golden -- "lowest always" prefers the jack (the lower-ranked card) from the pair, so it is the *queen* that becomes the dead giveaway there, not the jack |
| 65/126 | cash the king, then read the beliefs, vs the 50/50 defender | golden |
| 10/21 | always rise with the ace instead, vs the 50/50 defender | golden; 10/21 is exactly the posterior mass of the 2-2 spade breaks in this belief space, and is unchanged by which defender is paired with it, since rising never reaches a node where the belief mattered |
| 15/28 | cash the king, then read the beliefs, vs the always-shows-the-queen defender | golden; higher than 10/21 even though the one node this example reports reads "rise" there too -- every *other* node in the tree is still read on its own belief |
"""

import unittest

import dds3

import restricted_choice_6nt_belies_space as example
from strategies import double_dummy_defender, lowest_eligible_defender

Card = example.Card
SPADES = example.SPADES
QUEEN, JACK = example.QUEEN, example.JACK


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

    def _layout(self, south_spades: int) -> dict:
        layout = {
            "trump": 4,  # notrump
            "first": 2,  # South on lead for this probe
            "remain_cards": [[0, 0, 0, 0] for _ in range(4)],
            "current_trick_suit": (0, 0, 0),
            "current_trick_rank": (0, 0, 0),
        }
        layout["remain_cards"][2][SPADES] = south_spades
        return layout

    def test_it_splits_50_50_holding_the_queen_and_the_jack_second_seat(self) -> None:
        layout = self._layout((1 << QUEEN) | (1 << JACK))
        layout["current_trick_suit"] = (SPADES, 0, 0)
        layout["current_trick_rank"] = (2, 0, 0)  # one card already led

        distribution = example.randomises_queen_jack_in_second_seat(layout, 2, None)

        self.assertEqual(
            {(card.suit, card.rank): probability for card, probability in distribution},
            {(SPADES, QUEEN): 0.5, (SPADES, JACK): 0.5})

    def test_it_plays_low_holding_only_the_queen_second_seat(self) -> None:
        layout = self._layout((1 << QUEEN) | (1 << 5))
        layout["current_trick_suit"] = (SPADES, 0, 0)
        layout["current_trick_rank"] = (2, 0, 0)

        distribution = example.randomises_queen_jack_in_second_seat(layout, 2, None)

        self.assertEqual(distribution, [(Card(SPADES, 5), 1.0)])

    def test_it_plays_low_holding_the_queen_and_the_jack_as_the_leader(self) -> None:
        layout = self._layout((1 << QUEEN) | (1 << JACK))

        distribution = example.randomises_queen_jack_in_second_seat(layout, 2, None)

        self.assertEqual(distribution, [(Card(SPADES, JACK), 1.0)])


class TestTheAlwaysShowsTheQueenDefender(unittest.TestCase):
    """`always_shows_the_queen_from_qj_in_second_seat`, the deliberate
    contrast with the 50/50 defender above: same holding, same seat, but
    deterministically the queen every time.
    """

    def _layout(self, south_spades: int) -> dict:
        layout = {
            "trump": 4,  # notrump
            "first": 2,  # South on lead for this probe
            "remain_cards": [[0, 0, 0, 0] for _ in range(4)],
            "current_trick_suit": (0, 0, 0),
            "current_trick_rank": (0, 0, 0),
        }
        layout["remain_cards"][2][SPADES] = south_spades
        return layout

    def test_it_always_shows_the_queen_holding_the_pair_second_seat(self) -> None:
        layout = self._layout((1 << QUEEN) | (1 << JACK))
        layout["current_trick_suit"] = (SPADES, 0, 0)
        layout["current_trick_rank"] = (2, 0, 0)  # one card already led

        distribution = example.always_shows_the_queen_from_qj_in_second_seat(layout, 2, None)

        self.assertEqual(distribution, [(Card(SPADES, QUEEN), 1.0)])

    def test_it_plays_low_holding_only_the_queen_second_seat(self) -> None:
        layout = self._layout((1 << QUEEN) | (1 << 5))
        layout["current_trick_suit"] = (SPADES, 0, 0)
        layout["current_trick_rank"] = (2, 0, 0)

        distribution = example.always_shows_the_queen_from_qj_in_second_seat(layout, 2, None)

        self.assertEqual(distribution, [(Card(SPADES, 5), 1.0)])

    def test_it_plays_low_holding_the_queen_and_the_jack_as_the_leader(self) -> None:
        layout = self._layout((1 << QUEEN) | (1 << JACK))

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

        example._FINESSE_READING_BY_SOUTHS_HONOUR.clear()
        example.evaluate(sequence, source, example.cash_the_king_then_read_the_beliefs, delta)
        return dict(example._FINESSE_READING_BY_SOUTHS_HONOUR)

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

        self.assertAlmostEqual(belief_value["p_make"], 65 / 126)
        self.assertAlmostEqual(ace_value["p_make"], 10 / 21)
        self.assertGreater(belief_value["p_make"], ace_value["p_make"])

    def test_always_rising_is_exactly_the_2_2_mass_regardless_of_defender(self) -> None:
        sequence = example.restricted_choice_6nt()
        root = sequence.current_deal
        record = example.bsle.PlayRecord(sequence.history, sequence.opening_leader)
        source = example.bsle.ExhaustiveLayoutSource(root, sequence.declarer, example.SEED, record=record)

        for defence in (
            example.randomises_queen_jack_in_second_seat,
            example.always_shows_the_queen_from_qj_in_second_seat,
            lowest_eligible_defender,
        ):
            ace_value = example.evaluate(sequence, source, example.always_rise_with_the_ace, defence)
            self.assertAlmostEqual(ace_value["p_make"], 10 / 21)

    def test_against_the_no_tell_defender_the_read_still_beats_always_rising(self) -> None:
        sequence = example.restricted_choice_6nt()
        root = sequence.current_deal
        record = example.bsle.PlayRecord(sequence.history, sequence.opening_leader)
        source = example.bsle.ExhaustiveLayoutSource(root, sequence.declarer, example.SEED, record=record)
        defence = example.always_shows_the_queen_from_qj_in_second_seat

        belief_value = example.evaluate(sequence, source, example.cash_the_king_then_read_the_beliefs, defence)
        ace_value = example.evaluate(sequence, source, example.always_rise_with_the_ace, defence)

        self.assertAlmostEqual(belief_value["p_make"], 15 / 28)
        self.assertAlmostEqual(ace_value["p_make"], 10 / 21)
        self.assertGreater(belief_value["p_make"], ace_value["p_make"])


class TestTheScriptRuns(unittest.TestCase):
    def test_main_runs_and_reports_both_p_makes(self) -> None:
        import io
        from contextlib import redirect_stdout

        out = io.StringIO()
        with redirect_stdout(out):
            example.main()

        doc = out.getvalue()
        self.assertIn("P_make = 0.5159", doc)
        self.assertIn("P_make = 0.4762", doc)
        self.assertIn("P_make = 0.5357", doc)
        self.assertIn("47.62%", doc)
        self.assertIn("7.94%", doc)
        self.assertIn("5.95%", doc)
        self.assertIn("ace 40%  --  finesse 60%", doc)  # both branches, bespoke 50/50
        self.assertIn("ace 57%  --  finesse 43%", doc)  # queen branch, always-shows-the-queen
        self.assertIn("ace 0%  --  finesse 100%", doc)  # jack branch, always-shows-the-queen


if __name__ == "__main__":
    unittest.main()
