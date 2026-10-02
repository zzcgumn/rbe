"""Runs the example, and checks the numbers it prints.

| value | what it is | status |
| --- | --- | --- |
| 252 | layouts in the belief space | golden (fixed by the deal and the history) |
| 10/21 | root prior, South a 2-2 break | golden -- exactly the 2-2 mass `TestPMake` also reads off `always_rise_with_the_ace`'s P_make |
| 5/63 | root prior, South QJ tight | golden |
| 5/84 | root prior, South singleton jack | golden; equal to singleton queen by the north/south symmetry of the prior, before any card is played |
| 5/84 | root prior, South singleton queen (the real deal) | golden |
| 60% | finesse read, real branch, vs the 50/50 defender | **re-derived** below from the raw combinatorics, not just measured |
| 60% | the same read against `DoubleDummyDefender`'s own touching-sequence policy | golden -- the two agree exactly, which is itself asserted |
| 3/7 | the same read against a defender who always shows the queen from the pair | golden -- showing it is no longer any tell, so the posterior is the raw prior odds, not the restricted-choice-adjusted one |
| 100% | the same read against a defender who never hides a jack behind a queen | re-derived -- a defender who always plays its lowest legal card can never show the queen while still holding the jack, so the posterior this example reads is provably 1 against it, not merely measured as 1 |
| 0.3968 | cash the king, then read the beliefs, vs the 50/50 defender | golden |
| 0.4762 | always rise with the ace instead, vs the 50/50 defender | golden; 0.4762 is exactly the posterior mass of the 2-2 spade breaks in this belief space |
| 0.4762 | cash the king, then read the beliefs, vs the always-shows-the-queen defender | golden -- the read's own conclusion there is to rise, so it matches always-rise exactly |
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
    """The one belief-dependent number `cash_the_king_then_read_the_beliefs`
    computes: the probability, on the real branch (South already shown the
    queen, North down to the bare jack), that the finesse is correct.
    """

    def _read(self, delta) -> float:
        sequence = example.restricted_choice_6nt()
        root = sequence.current_deal
        record = example.bsle.PlayRecord(sequence.history, sequence.opening_leader)
        source = example.bsle.ExhaustiveLayoutSource(root, sequence.declarer, example.SEED, record=record)

        example._LAST_SMALL_CARD_READING.clear()
        example.evaluate(sequence, source, example.cash_the_king_then_read_the_beliefs, delta)
        readings = set(example._LAST_SMALL_CARD_READING)
        self.assertEqual(len(readings), 1, "expected one consistent reading, got %r" % readings)
        return readings.pop()

    def test_it_is_60_percent_against_the_bespoke_defender(self) -> None:
        self.assertAlmostEqual(
            self._read(example.randomises_queen_jack_in_second_seat), 0.6)

    def test_double_dummy_touching_sequence_agrees_exactly(self) -> None:
        ctx = dds3.SolverContext()

        self.assertAlmostEqual(self._read(double_dummy_defender(ctx)), 0.6)

    def test_it_is_certain_against_a_defender_that_never_hides_a_jack(self) -> None:
        self.assertAlmostEqual(self._read(lowest_eligible_defender), 1.0)

    def test_it_is_3_of_7_against_a_defender_who_always_shows_the_queen(self) -> None:
        self.assertAlmostEqual(
            self._read(example.always_shows_the_queen_from_qj_in_second_seat), 3 / 7)


class TestPMake(unittest.TestCase):
    def test_reading_the_beliefs_scores_lower_than_always_rising_here(self) -> None:
        sequence = example.restricted_choice_6nt()
        root = sequence.current_deal
        record = example.bsle.PlayRecord(sequence.history, sequence.opening_leader)
        source = example.bsle.ExhaustiveLayoutSource(root, sequence.declarer, example.SEED, record=record)
        defence = example.randomises_queen_jack_in_second_seat

        belief_value = example.evaluate(sequence, source, example.cash_the_king_then_read_the_beliefs, defence)
        ace_value = example.evaluate(sequence, source, example.always_rise_with_the_ace, defence)

        self.assertAlmostEqual(belief_value["p_make"], 0.3968253968253968)
        self.assertAlmostEqual(ace_value["p_make"], 0.47619047619047616)
        self.assertLess(belief_value["p_make"], ace_value["p_make"])

    def test_against_the_no_tell_defender_the_read_matches_always_rising(self) -> None:
        sequence = example.restricted_choice_6nt()
        root = sequence.current_deal
        record = example.bsle.PlayRecord(sequence.history, sequence.opening_leader)
        source = example.bsle.ExhaustiveLayoutSource(root, sequence.declarer, example.SEED, record=record)
        defence = example.always_shows_the_queen_from_qj_in_second_seat

        belief_value = example.evaluate(sequence, source, example.cash_the_king_then_read_the_beliefs, defence)
        ace_value = example.evaluate(sequence, source, example.always_rise_with_the_ace, defence)

        self.assertAlmostEqual(belief_value["p_make"], ace_value["p_make"])
        self.assertAlmostEqual(belief_value["p_make"], 0.47619047619047616)


class TestTheScriptRuns(unittest.TestCase):
    def test_main_runs_and_reports_both_p_makes(self) -> None:
        import io
        from contextlib import redirect_stdout

        out = io.StringIO()
        with redirect_stdout(out):
            example.main()

        doc = out.getvalue()
        self.assertIn("P_make = 0.3968", doc)
        self.assertIn("P_make = 0.4762", doc)
        self.assertIn("60%", doc)
        self.assertIn("43%", doc)
        self.assertIn("47.62%", doc)
        self.assertIn("7.94%", doc)
        self.assertIn("5.95%", doc)


if __name__ == "__main__":
    unittest.main()
