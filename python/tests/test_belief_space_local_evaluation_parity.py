"""Checks that the evaluator gives the same answers driven from Python as
from C++, including with sampling and replenishment.

Most tests read their fixtures and expected answers from
tests/belief_evaluation/parity_reference. That C++ program runs once as a
subprocess and prints Python literals. Both sides of each comparison come
from one construction, so a transcription slip cannot hide.

The oracle cases and the two-way guess compare against hand-derived
constants (1.0, 0.0, 0.5), which oracle_test.cpp also asserts. A slip there
gives a different number, not a false agreement. TestOracleCases also
compares against the ORACLE_* values that parity_reference prints.
"""

import subprocess
import unittest
from pathlib import Path

from belief_space_local_evaluation import BeliefEntry
from belief_space_local_evaluation import Card
from belief_space_local_evaluation import evaluate
from belief_space_local_evaluation import ExhaustiveLayoutSource
from belief_space_local_evaluation import LayoutSource

Spades, Hearts, Diamonds, Clubs = 0, 1, 2, 3
North, East, South, West = 0, 1, 2, 3
DDS_NOTRUMP = 4


def _repo_root(start: Path | None = None) -> Path:
    # Do not call Path.resolve(). Under `bazel test` this file is often a
    # runfiles symlink, and resolving it leaves the runfiles tree that holds
    # parity_reference. Same as _repo_root in ci_belief_evaluation_opt_test.py.
    here = (start or Path(__file__)).absolute()
    for parent in here.parents:
        workflows = parent / ".github" / "workflows"
        if workflows.is_dir() and any(workflows.glob("ci_*.yml")):
            return parent
    raise AssertionError("could not locate repository root from test file path")


def _parity_reference_binary() -> Path:
    # A cc_binary runfile has no suffix on Linux and macOS, and .exe on
    # Windows. The build has Windows rules, so try both, bare name first.
    root = _repo_root() / "tests" / "belief_evaluation" / "parity_reference"
    for candidate in (root, root.with_suffix(".exe")):
        if candidate.is_file():
            return candidate
    raise AssertionError(f"parity_reference data dependency not found at {root} or {root}.exe")


def _run_parity_reference() -> dict:
    output = subprocess.run(
        [str(_parity_reference_binary())],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    namespace: dict = {}
    # The output comes from this test's data dependency, so it is trusted.
    # It is a series of assignments, not one literal, so ast.literal_eval
    # cannot read it.
    exec(compile(output, "<parity_reference>", "exec"), namespace)  # noqa: S102
    return namespace


REFERENCE = _run_parity_reference()


def lowest_card_in(remain_cards_row) -> Card:
    for suit in range(4):
        mask = remain_cards_row[suit]
        if mask:
            rank = 2
            while not (mask & (1 << rank)):
                rank += 1
            return Card(suit, rank)
    raise AssertionError("seat holds nothing")


def seat_on_play(deal: dict) -> int:
    played = 0
    for rank in deal["current_trick_rank"]:
        if rank == 0:
            break
        played += 1
    return (deal["first"] + played) % 4


def lowest_legal_card(deal: dict, seat: int) -> Card:
    remain_cards = deal["remain_cards"][seat]
    if deal["current_trick_rank"][0] != 0:
        led = deal["current_trick_suit"][0]
        if remain_cards[led] != 0:
            return lowest_card_in([remain_cards[led] if s == led else 0 for s in range(4)])
    return lowest_card_in(remain_cards)


def declarer_play(state, view):
    del view
    return lowest_legal_card(state.known_holdings, seat_on_play(state.known_holdings))


def defender_play(layout, seat, state):
    del state
    return [(lowest_legal_card(layout, seat), 1.0)]


class TestSizeAgreesOnThreeRootsOfDifferentShapes(unittest.TestCase):
    # A whole-space ending, a mid-trick ending and a two-card ending, read
    # from the C++ program that built them.
    def test_ten_card_pool_root(self) -> None:
        source = ExhaustiveLayoutSource(REFERENCE["TEN_CARD_POOL_ROOT"], North, 1)
        self.assertEqual(source.size(), REFERENCE["TEN_CARD_POOL_SIZE"])

    def test_mid_trick_root(self) -> None:
        source = ExhaustiveLayoutSource(REFERENCE["MID_TRICK_ROOT"], North, 1)
        self.assertEqual(source.size(), REFERENCE["MID_TRICK_SIZE"])

    def test_small_single_suit_root(self) -> None:
        source = ExhaustiveLayoutSource(REFERENCE["SMALL_SINGLE_SUIT_ROOT"], North, 1)
        self.assertEqual(source.size(), REFERENCE["SMALL_SINGLE_SUIT_SIZE"])


class TestLayoutSourceReproducesCppAnswers(unittest.TestCase):
    # A Python LayoutSource subclass returns the layouts that the C++
    # source produced for the small fixture.
    def test_a_python_layout_source_matches_every_cpp_layout(self) -> None:
        expected = REFERENCE["SMALL_SINGLE_SUIT_LAYOUTS"]

        class PythonVectorSource(LayoutSource):
            def __init__(self, deals):
                super().__init__()
                self._deals = deals

            def size(self):
                return len(self._deals)

            def at(self, index):
                return self._deals[index]

        source = PythonVectorSource(expected)
        self.assertEqual(source.size(), len(expected))
        for index, deal in enumerate(expected):
            self.assertEqual(source.at(index), deal)

        # ExhaustiveLayoutSource over the same root produces the same set,
        # not just the same count.
        cpp_source = ExhaustiveLayoutSource(REFERENCE["SMALL_SINGLE_SUIT_ROOT"], North, 1)
        produced = [cpp_source.at(i) for i in range(cpp_source.size())]
        self.assertEqual(sorted(produced, key=repr), sorted(expected, key=repr))


class TestSampledReplenishingRunIsBitwiseIdentical(unittest.TestCase):
    # Exact equality on the double, not approximate. This is the property
    # most likely to break unnoticed.
    def test_p_make_matches_bitwise(self) -> None:
        root = REFERENCE["SAMPLED_ROOT"]
        source = ExhaustiveLayoutSource(root, North, REFERENCE["SAMPLED_SEED"])
        result = evaluate(
            root, North, 1, source, declarer_play, defender_play,
            collect_counters=True,
            sample_size=REFERENCE["SAMPLED_SAMPLE_SIZE"],
            scan_budget=REFERENCE["SAMPLED_SCAN_BUDGET"],
            replenish_below=REFERENCE["SAMPLED_REPLENISH_BELOW"])

        self.assertNotIn("error", result)
        self.assertFalse(REFERENCE["SAMPLED_ERROR"])
        self.assertEqual(result["by_strategy"][1]["p_make"], REFERENCE["SAMPLED_P_MAKE"])

    def test_counters_match_field_for_field(self) -> None:
        # The Python-driven run took the same path, not just reached the
        # same answer.
        root = REFERENCE["SAMPLED_ROOT"]
        source = ExhaustiveLayoutSource(root, North, REFERENCE["SAMPLED_SEED"])
        result = evaluate(
            root, North, 1, source, declarer_play, defender_play,
            collect_counters=True,
            sample_size=REFERENCE["SAMPLED_SAMPLE_SIZE"],
            scan_budget=REFERENCE["SAMPLED_SCAN_BUDGET"],
            replenish_below=REFERENCE["SAMPLED_REPLENISH_BELOW"])

        counters = result["by_strategy"][1]["counters"]
        expected = REFERENCE["SAMPLED_COUNTERS"]

        for field in ("nodes_visited", "tier1_made_cuts", "tier1_dead_cuts", "tier2_cuts"):
            self.assertEqual(counters[field], expected[field], field)

        self.assertEqual(len(counters["sample_size_by_depth"]), len(expected["sample_size_by_depth"]))
        for depth, (got, want) in enumerate(
                zip(counters["sample_size_by_depth"], expected["sample_size_by_depth"])):
            self.assertEqual(got, want, f"sample_size_by_depth[{depth}]")

        self.assertEqual(
            len(counters["replenishment_by_depth"]), len(expected["replenishment_by_depth"]))
        for depth, (got, want) in enumerate(
                zip(counters["replenishment_by_depth"], expected["replenishment_by_depth"])):
            self.assertEqual(got, want, f"replenishment_by_depth[{depth}]")


class TestOracleCases(unittest.TestCase):
    # Three oracle cases from oracle_test.cpp, with a Python pi and delta.
    # Each checks the hand-derived constant that the C++ test asserts, so a
    # Python-side bug gives a wrong answer.
    def test_certainty_over_a_several_layout_belief_space(self) -> None:
        layout = REFERENCE["ORACLE_CERTAINTY_LAYOUT"]

        class ThreeLayoutSource(LayoutSource):
            def size(self):
                return 3

            def at(self, index):
                # Mirrors the three make_certain_win_layout calls in
                # oracle_test.cpp: East and West's low cards.
                variants = [(2, 4), (4, 2), (2, 2)]
                east_low, west_low = variants[index]
                deal = {k: (list(v) if isinstance(v, list) else v) for k, v in layout.items()}
                deal["remain_cards"] = [row[:] for row in layout["remain_cards"]]
                deal["remain_cards"][East][Spades] = 1 << east_low
                deal["remain_cards"][West][Spades] = 1 << west_low
                return deal

        source = ThreeLayoutSource()
        result = evaluate(source.at(0), North, 1, source, declarer_play, defender_play)
        self.assertNotIn("error", result)
        self.assertEqual(result["by_strategy"][1]["p_make"], 1.0)
        self.assertEqual(result["by_strategy"][1]["p_make"], REFERENCE["ORACLE_CERTAINTY_P_MAKE"])

    def test_impossibility(self) -> None:
        layout = REFERENCE["ORACLE_IMPOSSIBILITY_LAYOUT"]

        class OneLayoutSource(LayoutSource):
            def size(self):
                return 1

            def at(self, index):
                return layout

        source = OneLayoutSource()
        # The ending has one trick, so two tricks are impossible.
        result = evaluate(layout, North, 2, source, declarer_play, defender_play)
        self.assertNotIn("error", result)
        self.assertEqual(result["by_strategy"][1]["p_make"], 0.0)
        self.assertEqual(result["by_strategy"][1]["p_make"], REFERENCE["ORACLE_IMPOSSIBILITY_P_MAKE"])

    def test_delta_actually_matters(self) -> None:
        layout = REFERENCE["ORACLE_DELTA_MATTERS_LAYOUT"]

        class OneLayoutSource(LayoutSource):
            def size(self):
                return 1

            def at(self, index):
                return layout

        def east_still_has_the_choice(current_layout: dict) -> bool:
            return current_layout["remain_cards"][East][Spades] == (1 << 13) | (1 << 2)

        def delta_plays_king(current_layout, seat, state):
            del state
            if seat == East and east_still_has_the_choice(current_layout):
                return [(Card(Spades, 13), 1.0)]
            return defender_play(current_layout, seat, None)

        def delta_plays_two(current_layout, seat, state):
            del state
            if seat == East and east_still_has_the_choice(current_layout):
                return [(Card(Spades, 2), 1.0)]
            return defender_play(current_layout, seat, None)

        source = OneLayoutSource()
        with_king = evaluate(layout, North, 1, source, declarer_play, delta_plays_king)
        with_two = evaluate(layout, North, 1, source, declarer_play, delta_plays_two)

        self.assertNotIn("error", with_king)
        self.assertNotIn("error", with_two)
        self.assertEqual(with_king["by_strategy"][1]["p_make"], 0.0)  # King beats North's Queen
        self.assertEqual(with_two["by_strategy"][1]["p_make"], 1.0)  # Queen beats the Two
        self.assertEqual(with_king["by_strategy"][1]["p_make"], REFERENCE["ORACLE_DELTA_MATTERS_WITH_KING_P_MAKE"])
        self.assertEqual(with_two["by_strategy"][1]["p_make"], REFERENCE["ORACLE_DELTA_MATTERS_WITH_TWO_P_MAKE"])


class TestTheTwoWayGuess(unittest.TestCase):
    # The two-way guess from oracle_test.cpp: a two-layout information set.
    # The Python pi reads BeliefView as play_two_way_guess does in C++. The
    # fixture is built here, not read from parity_reference, to drive the
    # belief view from Python.
    #
    # North holds ♠2 ♣2, South (dummy) ♠A J. East holds ♠K 6 or ♠7 6, and
    # follows with the six either way, which reveals nothing. West holds
    # the other spade, ♠7 or ♠K, and the ♣5. North leads and dummy plays
    # the jack.
    #
    # Layout 0 (West ♠7): the jack wins trick 1. Dummy's ace beats East's
    # king on trick 2. 2/2, made.
    # Layout 1 (West ♠K): the king wins trick 1. West leads the ♣5 and North
    # follows with the ♣2. East and South have no clubs. West wins trick 2.
    # 0/2, down.
    # Each layout has kappa = 1/2: P_make = 0.5 * 1 + 0.5 * 0 = 0.5.
    def make_layout0(self) -> dict:
        remain_cards = [[0, 0, 0, 0] for _ in range(4)]
        remain_cards[North][Spades] = 1 << 2
        remain_cards[North][Clubs] = 1 << 2
        remain_cards[South][Spades] = (1 << 14) | (1 << 11)  # Ace, Jack
        remain_cards[East][Spades] = (1 << 13) | (1 << 6)  # King, Six
        remain_cards[West][Spades] = 1 << 7  # Seven
        remain_cards[West][Clubs] = 1 << 5
        return {
            "trump": DDS_NOTRUMP,
            "first": North,
            "remain_cards": remain_cards,
            "current_trick_suit": (0, 0, 0),
            "current_trick_rank": (0, 0, 0),
        }

    def make_layout1(self) -> dict:
        layout = self.make_layout0()
        layout["remain_cards"] = [row[:] for row in layout["remain_cards"]]
        layout["remain_cards"][East][Spades] = (1 << 6) | (1 << 7)  # Six, Seven
        layout["remain_cards"][West][Spades] = 1 << 13  # King
        return layout

    def test_p_make_is_one_half(self) -> None:
        layout0 = self.make_layout0()
        layout1 = self.make_layout1()

        info_set_checks = []

        def play_two_way_guess(state, view):
            seat = seat_on_play(state.known_holdings)
            is_dummys_decision = (
                seat == South
                and state.known_holdings["remain_cards"][South][Spades] == (1 << 14) | (1 << 11))
            if not is_dummys_decision:
                return lowest_legal_card(state.known_holdings, seat)

            entries = view.entries
            info_set_checks.append(len(entries))
            total = sum(e.posterior for e in entries)
            info_set_checks.append(total)
            for e in entries:
                assert isinstance(e, BeliefEntry)
                info_set_checks.append(e.posterior)
            return Card(Spades, 11)  # Jack

        class TwoLayoutSource(LayoutSource):
            def size(self):
                return 2

            def at(self, index):
                return [layout0, layout1][index]

        source = TwoLayoutSource()
        result = evaluate(layout0, North, 2, source, play_two_way_guess, defender_play)

        self.assertNotIn("error", result)
        self.assertEqual(result["by_strategy"][1]["p_make"], 0.5)
        # The information set is ambiguous: two entries, each with posterior
        # 0.5.
        self.assertEqual(info_set_checks[0], 2)
        self.assertAlmostEqual(info_set_checks[1], 1.0, places=9)
        self.assertAlmostEqual(info_set_checks[2], 0.5, places=9)
        self.assertAlmostEqual(info_set_checks[3], 0.5, places=9)

    def test_collapses_to_one_if_the_line_per_layout_is_allowed_to_differ(self) -> None:
        # An evaluator that solved each layout separately could pick the
        # winning line in each and report 1.0, not 0.5. As in
        # oracle_test.cpp, this evaluates each layout with a separate source:
        # the jack wins layout 0 and loses layout 1.
        layout0 = self.make_layout0()
        layout1 = self.make_layout1()

        def play_jack_forced(state, view):
            del view
            seat = seat_on_play(state.known_holdings)
            is_dummys_decision = (
                seat == South
                and state.known_holdings["remain_cards"][South][Spades] == (1 << 14) | (1 << 11))
            if is_dummys_decision:
                return Card(Spades, 11)
            return lowest_legal_card(state.known_holdings, seat)

        class OneLayoutSource(LayoutSource):
            def __init__(self, layout):
                super().__init__()
                self._layout = layout

            def size(self):
                return 1

            def at(self, index):
                return self._layout

        result0 = evaluate(
            layout0, North, 2, OneLayoutSource(layout0), play_jack_forced, defender_play)
        result1 = evaluate(
            layout1, North, 2, OneLayoutSource(layout1), play_jack_forced, defender_play)

        self.assertNotIn("error", result0)
        self.assertNotIn("error", result1)
        self.assertEqual(result0["by_strategy"][1]["p_make"], 1.0)
        self.assertEqual(result1["by_strategy"][1]["p_make"], 0.0)


if __name__ == "__main__":
    unittest.main()
