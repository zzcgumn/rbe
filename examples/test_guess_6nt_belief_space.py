"""Runs the example, and checks the two numbers it prints.

An example nobody runs is an example that rots -- these scripts are the first
thing a new caller reads, and they exercise the Python surface from outside
the library the way a caller does. This is what makes "the examples still
work" a fact rather than a hope.

No expected `P_make` here is a golden value recorded from a previous run.
The ending is small enough to settle independently. Of the 70 possible
splits of the defenders' eight cards, declarer takes the three tricks needed
in:

  44  playing low, against defenders who also play low
  14  playing low, against double-dummy defenders
  55  playing the cash-two-hearts line, against double-dummy defenders

and that last 55 is checked against the double-dummy ceiling rather than
just asserted, so it pins "this line is optimal here", not only its score.
"""

import io
import unittest
from contextlib import redirect_stdout

import dds3

import belief_space_local_evaluation as bsle

import guess_6nt_belief_space as example
from bridge_notation import NORTH, SOUTH, SPADES, HEARTS, holding
from strategies import (
    double_dummy_defender,
    lowest_eligible_declarer,
    lowest_eligible_defender,
)


class TestTheEnding(unittest.TestCase):
    def test_nine_tricks_are_played_and_declarer_wins_all_nine(self) -> None:
        sequence = example.guess_6nt()

        self.assertEqual(len(sequence.completed_tricks), 9)
        self.assertEqual(len(sequence.history), 36)
        self.assertEqual(sequence.tricks_won_by_declarer, 9)
        self.assertEqual(sequence.tricks_needed, 3)  # 6NT is 12; nine are in.
        self.assertEqual(sequence.current_deal["first"], SOUTH)

    def test_the_four_card_ending_is_the_one_the_docstring_draws(self) -> None:
        remain_cards = example.guess_6nt().current_deal["remain_cards"]

        self.assertEqual(remain_cards[NORTH][SPADES], holding(13, 11, 7))  # KJ7
        self.assertEqual(remain_cards[NORTH][HEARTS], holding(12))  # Q
        self.assertEqual(remain_cards[SOUTH][SPADES], holding(10, 9))  # T9
        self.assertEqual(remain_cards[SOUTH][HEARTS], holding(14, 10))  # AT
        for seat in range(4):
            self.assertEqual(sum(bin(mask).count("1") for mask in remain_cards[seat]), 4)


def _source(sequence):
    return bsle.ExhaustiveLayoutSource(
        sequence.current_deal, sequence.declarer, example.SEED,
        history=sequence.history, opening_leader=sequence.opening_leader)


class TestTheBeliefSpace(unittest.TestCase):
    def test_the_history_is_consistent_with_the_root(self) -> None:
        sequence = example.guess_6nt()

        source = _source(sequence)

        self.assertEqual(source.history_verdict(), bsle.HistoryVerdict.Consistent)
        self.assertEqual(source.size(), 70)  # C(8, 4): eight cards out, East has four.


class TestPMake(unittest.TestCase):
    def test_both_sides_playing_low_makes_in_44_of_the_70_layouts(self) -> None:
        sequence = example.guess_6nt()
        source = _source(sequence)

        result = bsle.evaluate(
            sequence.current_deal, sequence.declarer, sequence.tricks_needed, source,
            lowest_eligible_declarer, lowest_eligible_defender)

        self.assertNotIn("error", result)
        self.assertAlmostEqual(result["by_strategy"][1]["p_make"], 44 / 70, places=9)


class TestDoubleDummyDefence(unittest.TestCase):
    def test_double_dummy_defenders_hold_declarer_to_14_of_the_70_layouts(self) -> None:
        # Also settled outside the library: declarer, still playing low,
        # takes three tricks against best defence in 14 of the 70 splits.
        sequence = example.guess_6nt()
        source = _source(sequence)
        ctx = dds3.SolverContext()

        result = bsle.evaluate(
            sequence.current_deal, sequence.declarer, sequence.tricks_needed, source,
            lowest_eligible_declarer, double_dummy_defender(ctx))

        self.assertNotIn("error", result)
        self.assertAlmostEqual(result["by_strategy"][1]["p_make"], 14 / 70, places=9)

    def test_p_make_is_the_average_over_the_layouts_taken_one_at_a_time(self) -> None:
        # pi ignores the BeliefView entirely, so P_make over the belief space
        # must equal the mean of P_make over each layout evaluated alone.
        # This is what caught DoubleDummyBound returning dds's "not
        # evaluated" sentinel as though it were a trick count: the bounded
        # run reported 0.0 while this average stayed at 0.2.
        sequence = example.guess_6nt()
        ctx = dds3.SolverContext()
        delta = double_dummy_defender(ctx)
        source = _source(sequence)

        class OneLayout(bsle.LayoutSource):
            def __init__(self, layout):
                super().__init__()
                self._layout = layout

            def size(self):
                return 1

            def at(self, index):
                del index
                return self._layout

        total = 0.0
        for index in range(source.size()):
            one = bsle.evaluate(
                sequence.current_deal, sequence.declarer, sequence.tricks_needed,
                OneLayout(source.at(index)), lowest_eligible_declarer, delta)
            self.assertNotIn("error", one)
            total += one["by_strategy"][1]["p_make"]

        self.assertAlmostEqual(total / source.size(), 14 / 70, places=9)


class TestTheDeclarerLine(unittest.TestCase):
    """cash_two_hearts_and_play_a_spade -- the line main() actually runs."""

    def test_the_line_makes_in_55_of_the_70_layouts(self) -> None:
        sequence = example.guess_6nt()
        ctx = dds3.SolverContext()

        result = bsle.evaluate(
            sequence.current_deal, sequence.declarer, sequence.tricks_needed,
            _source(sequence), example.cash_two_hearts_and_play_a_spade,
            double_dummy_defender(ctx))

        self.assertNotIn("error", result)
        self.assertAlmostEqual(result["by_strategy"][1]["p_make"], 55 / 70, places=9)

    def test_the_line_is_double_dummy_optimal_in_this_ending(self) -> None:
        # The stronger claim, and the reason 55/70 is the right number rather
        # than merely the observed one: the layouts this line brings home are
        # exactly the layouts in which three tricks can be taken at all. It
        # cannot be improved on here -- which is worth pinning, because it is
        # what would break first if the line were edited.
        sequence = example.guess_6nt()
        ctx = dds3.SolverContext()
        bound = bsle.DoubleDummyBound(ctx, sequence.declarer)
        delta = double_dummy_defender(ctx)
        source = _source(sequence)

        class OneLayout(bsle.LayoutSource):
            def __init__(self, layout):
                super().__init__()
                self._layout = layout

            def size(self):
                return 1

            def at(self, index):
                del index
                return self._layout

        brings_home, achievable = set(), set()
        for index in range(source.size()):
            layout = source.at(index)
            one = bsle.evaluate(
                sequence.current_deal, sequence.declarer, sequence.tricks_needed,
                OneLayout(layout), example.cash_two_hearts_and_play_a_spade, delta)
            self.assertNotIn("error", one)
            if one["by_strategy"][1]["p_make"] > 0.999:
                brings_home.add(index)
            if bound(layout) >= sequence.tricks_needed:
                achievable.add(index)

        self.assertEqual(brings_home, achievable)
        self.assertEqual(len(brings_home), 55)


class TestTheScriptRuns(unittest.TestCase):
    def test_main_runs_and_reports_p_make(self) -> None:
        output = io.StringIO()
        with redirect_stdout(output):
            example.main()

        printed = output.getvalue()
        self.assertIn("Contract: 6NT by South", printed)
        # main() runs cash_two_hearts_and_play_a_spade against both defences.
        # The two agree: once declarer commits to that line the defenders
        # have no choice left that changes the outcome.
        self.assertIn("Defenders play low: P_make = 0.7857", printed)
        self.assertIn("Defenders play double dummy: P_make = 0.7857", printed)


if __name__ == "__main__":
    unittest.main()
