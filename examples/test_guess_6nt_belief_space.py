"""Runs the example, and checks the numbers it prints.

The examples are the first thing a new caller reads. These tests keep them
working.

Each asserted value has a status:

- **Re-derived:** the test recomputes the answer independently, so it
  catches a regression.
- **Golden:** the test asserts a number established once, outside this
  repository, so it would absorb a regression.

| value | what it is | status |
| --- | --- | --- |
| 44/70 | fixed line, both sides low | golden (direct playout) |
| 14/70 | playing low vs double dummy | golden, cross-checked by the per-layout average |
| 55/70 | fixed line vs double dummy | **re-derived**: set equality with `DoubleDummyBound` per layout |
| 35/70 | fixed line vs cover-when-wins | golden (exhaustive minimax); the ordering against 55 is re-derived |
| 1.0 | belief finesse vs low | golden; exact |
| 60/70 | belief finesse vs cover-when-wins | golden (the same minimax) |
| 0.4488 | belief finesse vs double dummy | golden; the weakest value, see below |

**0.4488 is not a bridge quantity.** `DoubleDummyDefender` spreads
probability over tied cards, so this value depends on the dds tie order and
on `SpreadPolicy`. The ordering it takes part in is the meaningful result.
`test_neither_declarer_dominates_the_other` checks that ordering.
"""

import io
import unittest
from contextlib import redirect_stdout

import dds3

import belief_space_local_evaluation as bsle

import guess_6nt_belief_space as example
from bridge_notation import HEARTS, NORTH, SOUTH, SPADES, holding
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


def _source(sequence):
    record = bsle.PlayRecord(sequence.history, sequence.opening_leader)
    return bsle.ExhaustiveLayoutSource(
        sequence.current_deal, sequence.declarer, example.SEED, record=record)


class TestTheBeliefSpace(unittest.TestCase):
    def test_the_history_is_consistent_with_the_root(self) -> None:
        sequence = example.guess_6nt()

        source = _source(sequence)

        # From Python, history_verdict() always returns Consistent, because a
        # bad history raises in the constructor. So check the size instead.
        # The next test checks the raise.
        self.assertEqual(source.size(), 70)  # C(8, 4): eight cards out, East has four.

    def test_a_bad_history_raises_from_the_constructor(self) -> None:
        # Python is stricter than C++ here: a bad history raises. A
        # duplicated card raises from PlayRecord, before
        # ExhaustiveLayoutSource sees it.
        sequence = example.guess_6nt()

        with self.assertRaises(bsle.BeliefSpaceLocalEvaluationError):
            bsle.PlayRecord(
                [bsle.Card(0, 14), bsle.Card(0, 14)],  # the same card twice
                sequence.opening_leader)


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
        # Golden: declarer, playing low, takes three tricks against
        # double-dummy defence in 14 of the 70 layouts.
        sequence = example.guess_6nt()
        source = _source(sequence)
        ctx = dds3.SolverContext()

        result = bsle.evaluate(
            sequence.current_deal, sequence.declarer, sequence.tricks_needed, source,
            lowest_eligible_declarer, double_dummy_defender(ctx))

        self.assertNotIn("error", result)
        self.assertAlmostEqual(result["by_strategy"][1]["p_make"], 14 / 70, places=9)

    def test_p_make_is_the_average_over_the_layouts_taken_one_at_a_time(self) -> None:
        # pi ignores the BeliefView, so P_make over the belief space must
        # equal the mean of P_make over each layout alone. This check
        # catches a bound that returns a wrong value.
        sequence = example.guess_6nt()
        ctx = dds3.SolverContext()
        delta = double_dummy_defender(ctx)
        source = _source(sequence)

        total = 0.0
        for index in range(source.size()):
            one = bsle.evaluate(
                sequence.current_deal, sequence.declarer, sequence.tricks_needed,
                bsle.SingleLayoutSource(source.at(index)), lowest_eligible_declarer, delta)
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
        # Against double-dummy defence, the line makes in exactly the
        # layouts where three tricks are double-dummy possible. No line does
        # better against that defender. This says nothing about other
        # defences: TestAnOptimalDefence holds the same line to 35/70.
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
                bsle.SingleLayoutSource(layout), example.cash_two_hearts_and_play_a_spade, delta)
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
        # Golden. An exhaustive minimax over every defensive choice, outside
        # this repository, lets this line make in exactly these 35 layouts.
        # So no defence does better, and this delta is optimal here. The
        # ordering test below guards the property.
        sequence = example.guess_6nt()

        result = bsle.evaluate(
            sequence.current_deal, sequence.declarer, sequence.tricks_needed,
            _source(sequence), example.cash_two_hearts_and_play_a_spade,
            queen_of_spades_when_it_wins)

        self.assertNotIn("error", result)
        self.assertAlmostEqual(result["by_strategy"][1]["p_make"], 35 / 70, places=9)

    def test_it_beats_double_dummy_defence_against_this_declarer(self) -> None:
        # Maximising tricks is not the same as defeating the contract.
        # Against this fixed line, a tailored defender does better: 35/70
        # against 55/70.
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
        # Exactly 1.0. The defender never plays the queen early, so the
        # beliefs locate it in every layout.
        self.assertAlmostEqual(
            self._p_make(example.finesse_the_queen_from_the_beliefs,
                         lowest_eligible_defender),
            1.0, places=9)

    def test_it_makes_in_60_of_the_70_against_the_tailored_defender(self) -> None:
        # Golden, counted once over the 70 layouts. It is not a ceiling:
        # against this defence, 70/70 is possible. The ordering test below
        # catches a regression.
        self.assertAlmostEqual(
            self._p_make(example.finesse_the_queen_from_the_beliefs,
                         queen_of_spades_when_it_wins),
            60 / 70, places=9)

    def test_neither_declarer_dominates_the_other(self) -> None:
        # The main result of the example, as an ordering. Reading the
        # beliefs is better against the tailored defender and worse against
        # double-dummy defence. So the best line depends on the defence.
        #
        # It loses to double dummy for two reasons. That defender spreads
        # over tied cards, which shapes the posterior the finesse reads. And
        # it assumes a double-dummy declarer.
        ctx = dds3.SolverContext()
        fixed, belief = (example.cash_two_hearts_and_play_a_spade,
                         example.finesse_the_queen_from_the_beliefs)

        tailored_fixed = self._p_make(fixed, queen_of_spades_when_it_wins)
        tailored_belief = self._p_make(belief, queen_of_spades_when_it_wins)
        dd_fixed = self._p_make(fixed, double_dummy_defender(ctx))
        dd_belief = self._p_make(belief, double_dummy_defender(ctx))

        self.assertGreater(tailored_belief, tailored_fixed)
        self.assertLess(dd_belief, dd_fixed)

    def test_it_answers_a_repeated_node_the_same_way(self) -> None:
        # pi must be pure. Check it within one traversal: the evaluator
        # revisits nodes, and two separate runs would not catch a strategy
        # whose state resets between runs.
        #
        # The key is everything pi may depend on: the position and the trick
        # in progress. Two different cards for one key mean pi is not pure.
        sequence = example.guess_6nt()
        seen = {}
        collisions = []

        visits = {}

        def watched(state, view):
            deal = state.known_holdings
            key = (tuple(tuple(row) for row in deal["remain_cards"]),
                   deal["first"], deal["current_trick_suit"], deal["current_trick_rank"])
            visits[key] = visits.get(key, 0) + 1
            card = example.finesse_the_queen_from_the_beliefs(state, view)
            previous = seen.setdefault(key, card)
            if previous != card:
                collisions.append((key, previous, card))
            return card

        result = bsle.evaluate(
            sequence.current_deal, sequence.declarer, sequence.tricks_needed,
            _source(sequence), watched, queen_of_spades_when_it_wins)

        self.assertNotIn("error", result)
        self.assertEqual(collisions, [])
        # The check means something only if nodes are revisited. Measured:
        # 773 calls over 322 nodes, one of them visited 24 times.
        self.assertGreater(max(visits.values()), 1)
        self.assertGreater(sum(visits.values()), len(visits))


class TestTheDocstring(unittest.TestCase):
    """The module docstring of the example must match `DECLARERS` and
    `DEFENCE_NAMES`."""

    def test_the_docstring_matches_the_grid(self) -> None:
        doc = example.__doc__

        self.assertIn(f"{len(example.DECLARERS)} declarers", doc)
        self.assertIn(f"{len(example.DEFENCE_NAMES)} defences", doc)
        for name, _ in example.DECLARERS:
            self.assertIn(name.replace(" ", ""), doc.replace(" ", "").replace("\n", ""))

    def test_the_grid_axes_agree_with_each_other(self) -> None:
        # DEFENCE_NAMES lets the docstring test run without a SolverContext.
        # It must match the real list, which needs one.
        ctx = dds3.SolverContext()

        self.assertEqual(
            tuple(n for n, _ in example.defences_with(ctx)), example.DEFENCE_NAMES)

    def test_exactly_one_declarer_reads_the_belief_view(self) -> None:
        # Detected by behaviour, in a real evaluation. The belief finesse
        # reads `view` only in third hand on a spade, so a probe at the root
        # would miss it.
        sequence = example.guess_6nt()

        readers = []
        for name, pi in example.DECLARERS:
            seen = []

            class Watching:
                """Delegates to the real view and records that it was read."""

                def __init__(self, view):
                    self._view = view

                @property
                def entries(self):
                    seen.append("entries")
                    return self._view.entries

                @property
                def is_sample(self):
                    seen.append("is_sample")
                    return self._view.is_sample

                @property
                def space_size(self):
                    seen.append("space_size")
                    return self._view.space_size

            def watched(state, view, pi=pi):
                return pi(state, Watching(view))

            result = bsle.evaluate(
                sequence.current_deal, sequence.declarer, sequence.tricks_needed,
                _source(sequence), watched, lowest_eligible_defender)
            self.assertNotIn("error", result)
            if seen:
                readers.append(name)

        self.assertEqual(readers, ["belief finesse"])


def _is_float(token: str) -> bool:
    try:
        float(token)
    except ValueError:
        return False
    return True


class TestTheScriptRuns(unittest.TestCase):
    def test_main_runs_and_reports_the_grid(self) -> None:
        output = io.StringIO()
        with redirect_stdout(output):
            example.main()

        printed = output.getvalue()
        self.assertIn("Contract: 6NT by South", printed)
        # The defenders' cards print as one pool. Their real hands must not
        # appear.
        self.assertIn("\u2660 AQ6542", printed)
        self.assertNotIn("\u2660 Q42", printed)
        # Low and double-dummy defence give the same value against the fixed
        # line. That is a coincidence of this ending: the third column shows
        # the defenders can change the outcome.
        # Parse the values from each row. Do not compare whole rows: their
        # spacing depends on the longest defence name.
        rows = {}
        for line in printed.splitlines():
            for name, _ in example.DECLARERS:
                # A grid row is a declarer name followed only by numbers.
                # Other lines can also start with a declarer name.
                rest = line[len(name):].split() if line.startswith(name) else None
                if rest and all(_is_float(token) for token in rest):
                    rows[name] = [float(token) for token in rest]
        self.assertEqual(rows["fixed line"], [0.7857, 0.7857, 0.5000])
        self.assertEqual(rows["belief finesse"], [1.0000, 0.4488, 0.8571])


if __name__ == "__main__":
    unittest.main()
