"""Runs the example, and checks the numbers it prints.

An example nobody runs is an example that rots -- these scripts are the first
thing a new caller reads, and they exercise the Python surface from outside
the library the way a caller does. This is what makes "the examples still
work" a fact rather than a hope.

Every asserted value, and what each one is worth. "Re-derived" means a test
here recomputes the answer independently and would catch a regression;
"golden" means it asserts a number established once, elsewhere, and would
absorb one.

| value | what it is | status |
| --- | --- | --- |
| 44/70 | fixed line, both sides low | golden (settled by direct playout) |
| 14/70 | playing low vs double dummy | golden, but cross-checked by the per-layout average below |
| 55/70 | fixed line vs double dummy | **re-derived** -- set equality against DoubleDummyBound per layout |
| 35/70 | fixed line vs cover-when-wins | golden (settled by exhaustive minimax); the *ordering* against 55 is re-derived |
| 1.0 | belief finesse vs low | golden; exact, and the ceiling against that defence is 70/70 |
| 60/70 | belief finesse vs cover-when-wins | golden (settled by the same minimax) |
| 0.4488 | belief finesse vs double dummy | golden, and the weakest of the lot -- see below |

**0.4488 is not a bridge quantity.** It is a mixture, not n/70, because
`DoubleDummyDefender` spreads probability over tied-for-best cards -- so it
depends on which of several equally good cards dds happens to name first and
on `SpreadPolicy`. It pins a solver tie-break, not a property of the ending.
What is worth pinning there is the *ordering* it participates in
(test_neither_declarer_dominates_the_other), and that is asserted separately.

The minimax and playout that settled the golden values are deliberately not
committed; see rbe-notes. Their absence is why those rows say golden.
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
        # run reported 0.0 while this average stayed at 0.2. The bound now
        # range-checks the score, so the two agree; the cross-check is worth
        # keeping for the next bound, not only for that one.
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
        # A golden value, like 44 and 35: established once by playing this
        # declarer against this defender over each of the 70 layouts and
        # counting, not re-derived here. It is not a ceiling -- the ceiling
        # against this defence is 70/70 -- so 60 says the belief finesse
        # locates the queen in 60 of the 70 splits and not that 60 is the most
        # any declarer could manage. The ordering test below is what would
        # catch a regression; this pins the number for reference.
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

    def test_it_answers_a_repeated_node_the_same_way(self) -> None:
        # The contract the evaluator relies on, checked *within* one traversal
        # rather than across two. The evaluator revisits sibling subtrees, so
        # the hazard is a strategy answering the same node differently on a
        # second visit; two identical runs compared afterwards cannot see that
        # and would pass for any strategy whose state resets between calls.
        #
        # Keyed on everything pi is allowed to condition on: the position and
        # the trick in progress. If the same key ever yields two different
        # cards, pi is not a function of its arguments.
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
        # The check is vacuous unless nodes really are revisited. They are:
        # measured at 773 calls over 322 distinct nodes, one of them 24 times.
        self.assertGreater(max(visits.values()), 1)
        self.assertGreater(sum(visits.values()), len(visits))


class TestTheDocstring(unittest.TestCase):
    """The example's own module docstring has drifted from main() twice, in
    both cases because the paragraph and the code it describes are ~150 lines
    apart. This is the structural guard, not a third round of proofreading."""

    def test_the_docstring_matches_the_grid(self) -> None:
        doc = example.__doc__

        self.assertIn(f"{len(example.DECLARERS)} declarers", doc)
        self.assertIn(f"{len(example.DEFENCE_NAMES)} defences", doc)
        for name, _ in example.DECLARERS:
            self.assertIn(name.replace(" ", ""), doc.replace(" ", "").replace("\n", ""))

    def test_the_grid_axes_agree_with_each_other(self) -> None:
        # DEFENCE_NAMES exists so the docstring can be checked without
        # constructing a SolverContext; it has to stay in step with the real
        # list, which does construct one.
        ctx = dds3.SolverContext()

        self.assertEqual(
            tuple(n for n, _ in example.defences_with(ctx)), example.DEFENCE_NAMES)

    def test_exactly_one_declarer_reads_the_belief_view(self) -> None:
        # The scope claim the docstring makes, detected by behaviour rather
        # than by reading the source. It has to run a real evaluation: the
        # belief finesse consults `view` only in third hand on a spade, so
        # probing it at the root would wrongly report that it never does.
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
        # The values, parsed out of their row -- not the row verbatim, whose
        # spacing is a function of the longest defence name and would fail on
        # a rename for no behavioural reason.
        rows = {}
        for line in printed.splitlines():
            for name, _ in example.DECLARERS:
                # A grid row is the declarer's name followed by nothing but
                # numbers. Other lines start with a declarer's name too.
                rest = line[len(name):].split() if line.startswith(name) else None
                if rest and all(_is_float(token) for token in rest):
                    rows[name] = [float(token) for token in rest]
        self.assertEqual(rows["fixed line"], [0.7857, 0.7857, 0.5000])
        self.assertEqual(rows["belief finesse"], [1.0000, 0.4488, 0.8571])


if __name__ == "__main__":
    unittest.main()
