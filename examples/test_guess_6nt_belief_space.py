"""Runs the example, and checks the numbers it prints.

An example nobody runs is an example that rots -- these scripts are the first
thing a new caller reads, and they exercise the Python surface from outside
the library the way a caller does. This is what makes "the examples still
work" a fact rather than a hope.

The ending is small enough to settle independently. Of the 70 possible
splits of the defenders' eight cards, declarer takes the three tricks needed
in:

  44  playing low, against defenders who also play low
  14  playing low, against double-dummy defenders
  55  playing the cash-two-hearts line, against double-dummy defenders
  35  the same line, against a defender that covers when covering wins

Only 55 is checked against a ceiling *by a test here*:
test_the_line_reaches_the_ceiling_against_double_dummy_defence recomputes
`DoubleDummyBound` per layout and asserts set equality, so it pins "optimal
against that defender" and not merely the score.

The other three are plain assertions on a number. Each was derived
independently of the library before being written down -- 44 and 35 by
direct playout and by exhaustive minimax over every defensive choice
respectively -- but that derivation is *not* re-run here, so as tests they
are golden values and would absorb a regression rather than catch one. What
guards 35 instead is
test_it_beats_double_dummy_defence_against_this_declarer, which asserts the
ordering rather than the value -- the property that would actually break
first if either strategy were edited.
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
    queen_of_spades_when_it_wins,
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


class OneLayout(bsle.LayoutSource):
    """A source holding a single layout -- exempt from obligation 4 (a
    randomised order) because one element has only one order. Used to check
    P_make against a per-layout average and against the double-dummy bound."""

    def __init__(self, layout):
        super().__init__()
        self._layout = layout

    def size(self):
        return 1

    def at(self, index):
        del index
        return self._layout


def _source(sequence):
    return bsle.ExhaustiveLayoutSource(
        sequence.current_deal, sequence.declarer, example.SEED,
        history=sequence.history, opening_leader=sequence.opening_leader)


class TestTheBeliefSpace(unittest.TestCase):
    def test_the_history_is_consistent_with_the_root(self) -> None:
        sequence = example.guess_6nt()

        source = _source(sequence)

        # history_verdict() can only ever return Consistent from Python -- a
        # bad history raises from the constructor instead -- so asserting it
        # would pin nothing. The size is the real assertion, and the raising
        # behaviour is pinned separately below.
        self.assertEqual(source.size(), 70)  # C(8, 4): eight cards out, East has four.

    def test_a_bad_history_raises_from_the_constructor(self) -> None:
        # The documented "Python is deliberately stricter than C++" contract:
        # in C++ a non-Consistent verdict is an accessor a caller may ignore;
        # here it raises, so a caller who never thinks to check still finds
        # out. This is what makes the verdict assertion above redundant.
        sequence = example.guess_6nt()

        with self.assertRaises(bsle.BeliefSpaceLocalEvaluationError):
            bsle.ExhaustiveLayoutSource(
                sequence.current_deal, sequence.declarer, example.SEED,
                history=[bsle.Card(0, 14), bsle.Card(0, 14)],  # the same card twice
                opening_leader=sequence.opening_leader)


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

    def test_the_line_reaches_the_ceiling_against_double_dummy_defence(self) -> None:
        # Against *double-dummy* defence the line brings home exactly the
        # layouts in which three tricks are double-dummy achievable -- it
        # cannot be improved on against that defender. Scoped deliberately:
        # this says nothing about other defences, and
        # TestAnOptimalDefence below shows a defender that holds the same
        # line to 35/70, because double-dummy defence assumes declarer is
        # also playing double dummy and this declarer is not.
        sequence = example.guess_6nt()
        ctx = dds3.SolverContext()
        bound = bsle.DoubleDummyBound(ctx, sequence.declarer)
        delta = double_dummy_defender(ctx)
        source = _source(sequence)


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


class TestAnOptimalDefence(unittest.TestCase):
    """queen_of_spades_when_it_wins -- optimal against this declarer line."""

    def test_it_holds_the_line_to_35_of_the_70_layouts(self) -> None:
        # A golden value: this asserts the number, it does not re-derive it.
        # 35 was established once, outside the library, by exhaustive minimax
        # over every defensive choice against this (deterministic) declarer
        # line -- which lets it through in exactly 35 layouts, and in exactly
        # those 35, so no defence can do better and this delta is optimal
        # here. That minimax is deliberately not committed; the property it
        # established is guarded by the ordering test below instead.
        sequence = example.guess_6nt()

        result = bsle.evaluate(
            sequence.current_deal, sequence.declarer, sequence.tricks_needed,
            _source(sequence), example.cash_two_hearts_and_play_a_spade,
            queen_of_spades_when_it_wins)

        self.assertNotIn("error", result)
        self.assertAlmostEqual(result["by_strategy"][1]["p_make"], 35 / 70, places=9)

    def test_it_beats_double_dummy_defence_against_this_declarer(self) -> None:
        # The finding worth keeping: trick-maximising defence is not
        # contract-minimising defence, and it also presumes a double-dummy
        # declarer. Against a fixed, blind line a tailored defender does
        # strictly better -- 35/70 against 55/70.
        sequence = example.guess_6nt()
        ctx = dds3.SolverContext()
        pi = example.cash_two_hearts_and_play_a_spade

        def run(delta):
            result = bsle.evaluate(
                sequence.current_deal, sequence.declarer, sequence.tricks_needed,
                _source(sequence), pi, delta)
            self.assertNotIn("error", result)
            return result["by_strategy"][1]["p_make"]

        self.assertLess(run(queen_of_spades_when_it_wins),
                        run(double_dummy_defender(ctx)))


class TestTheBeliefReadingDeclarer(unittest.TestCase):
    """finesse_the_queen_from_the_beliefs -- the only strategy that reads the
    BeliefView, and the only one whose result depends on the posteriors."""

    def _p_make(self, pi, delta) -> float:
        sequence = example.guess_6nt()
        result = bsle.evaluate(
            sequence.current_deal, sequence.declarer, sequence.tricks_needed,
            _source(sequence), pi, delta)
        self.assertNotIn("error", result)
        return result["by_strategy"][1]["p_make"]

    def test_it_always_makes_against_defenders_who_play_low(self) -> None:
        # Exactly 1.0, not approximately: against a defender that never puts
        # the queen up, reading the beliefs locates it in every layout.
        self.assertAlmostEqual(
            self._p_make(example.finesse_the_queen_from_the_beliefs,
                         lowest_eligible_defender),
            1.0, places=9)

    def test_it_makes_in_60_of_the_70_against_the_tailored_defender(self) -> None:
        self.assertAlmostEqual(
            self._p_make(example.finesse_the_queen_from_the_beliefs,
                         queen_of_spades_when_it_wins),
            60 / 70, places=9)

    def test_neither_declarer_dominates_the_other(self) -> None:
        # The finding this example exists to show, pinned as an ordering
        # rather than as two numbers: reading the beliefs is better against
        # the tailored defender and worse against double-dummy defence. So
        # "best line" is not defined independently of the defence assumed.
        #
        # Why it loses to double dummy: that defender spreads probability over
        # tied-for-best cards, which shapes the posterior the finesse reads,
        # and it optimises trick count against a declarer playing double dummy
        # -- which neither of these declarers is.
        ctx = dds3.SolverContext()
        fixed, belief = (example.cash_two_hearts_and_play_a_spade,
                         example.finesse_the_queen_from_the_beliefs)

        tailored_fixed = self._p_make(fixed, queen_of_spades_when_it_wins)
        tailored_belief = self._p_make(belief, queen_of_spades_when_it_wins)
        dd_fixed = self._p_make(fixed, double_dummy_defender(ctx))
        dd_belief = self._p_make(belief, double_dummy_defender(ctx))

        self.assertGreater(tailored_belief, tailored_fixed)
        self.assertLess(dd_belief, dd_fixed)

    def test_it_is_a_pure_function_of_its_arguments(self) -> None:
        # The contract the evaluator relies on: it revisits sibling subtrees,
        # so a strategy that answered the same node differently on a second
        # visit would corrupt the result. Two identical runs must agree
        # exactly, bit for bit.
        first = self._p_make(example.finesse_the_queen_from_the_beliefs,
                             queen_of_spades_when_it_wins)
        second = self._p_make(example.finesse_the_queen_from_the_beliefs,
                              queen_of_spades_when_it_wins)

        self.assertEqual(first, second)


class TestTheScriptRuns(unittest.TestCase):
    def test_main_runs_and_reports_the_grid(self) -> None:
        output = io.StringIO()
        with redirect_stdout(output):
            example.main()

        printed = output.getvalue()
        self.assertIn("Contract: 6NT by South", printed)
        # Printed as declarer sees it: the defenders' cards as one pool, never
        # split. Their actual hands must not appear.
        self.assertIn("\u2660 AQ6542", printed)
        self.assertNotIn("\u2660 Q42", printed)
        # Low and double-dummy defence happen to coincide against the fixed
        # line. That is a coincidence of this ending, not a property of it --
        # the third column shows the same line held to 0.5000, so the
        # defenders did have a choice that changes the outcome. Double-dummy
        # defence optimises trick count against a double-dummy declarer, and
        # this declarer is neither.
        self.assertIn("0.7857              0.7857              0.5000", printed)
        self.assertIn("1.0000              0.4488              0.8571", printed)


if __name__ == "__main__":
    unittest.main()
