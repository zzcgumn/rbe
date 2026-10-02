"""Runs the example, and checks the numbers it prints.

| value | what it is | status |
| --- | --- | --- |
| 252 | layouts in the belief space | golden (fixed by the deal and the history) |
| 60% | finesse read, real branch, vs the touching-sequence defender | golden -- see note below |
| 100% | the same read against a defender who never hides a jack behind a queen | re-derived -- a defender who always plays its lowest legal card can never show the queen while still holding the jack, so the posterior this example reads is provably 1 against it, not merely measured as 1 |
| 0.3968 | cash the king, then read the beliefs, vs double dummy | golden |
| 0.4762 | always rise with the ace instead, vs double dummy | golden; 0.4762 is exactly the posterior mass of the 2-2 spade breaks in this belief space |
"""

import unittest

import dds3

import restricted_choice_6nt_belies_space as example
from strategies import double_dummy_defender, lowest_eligible_defender


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

    def test_it_is_60_percent_against_a_defender_that_hides_a_jack_behind_a_queen(self) -> None:
        ctx = dds3.SolverContext()

        self.assertAlmostEqual(self._read(double_dummy_defender(ctx)), 0.6)

    def test_it_is_certain_against_a_defender_that_never_does(self) -> None:
        self.assertAlmostEqual(self._read(lowest_eligible_defender), 1.0)


class TestPMake(unittest.TestCase):
    def test_reading_the_beliefs_scores_lower_than_always_rising_here(self) -> None:
        sequence = example.restricted_choice_6nt()
        root = sequence.current_deal
        record = example.bsle.PlayRecord(sequence.history, sequence.opening_leader)
        source = example.bsle.ExhaustiveLayoutSource(root, sequence.declarer, example.SEED, record=record)
        ctx = dds3.SolverContext()
        defence = double_dummy_defender(ctx)

        belief_value = example.evaluate(sequence, source, example.cash_the_king_then_read_the_beliefs, defence)
        ace_value = example.evaluate(sequence, source, example.always_rise_with_the_ace, defence)

        self.assertAlmostEqual(belief_value["p_make"], 0.3968253968253968)
        self.assertAlmostEqual(ace_value["p_make"], 0.47619047619047616)
        self.assertLess(belief_value["p_make"], ace_value["p_make"])


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


if __name__ == "__main__":
    unittest.main()
