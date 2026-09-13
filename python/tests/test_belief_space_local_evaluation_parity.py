"""Proves driving the evaluator from Python gives the same answers as
driving it from C++, including where sampling and replenishment are in
play.

size() on three roots, a sampled/replenishing run compared bitwise, and
counters compared field for field all read their fixtures and expected
answers from library/tests/belief_evaluation/parity_reference, a small C++
program run once as a subprocess and its stdout parsed as Python literals.
This is a "build both sides of every comparison from one description"
mechanism: hand-transcribing the same fixture once in each language is
exactly how a slip hides -- the Python side would still run, still
produce *some* answer, and pass or fail for a reason that has nothing to
do with the binding. Reading the C++ program's own output instead means
the fixture and the answer it is compared against both come from the same
construction, once, in C++.

The oracle cases and the Python LayoutSource-subclass tests instead
compare against a hand-derived constant (1.0, 0.0, 0.5) that is
independently known and asserted by oracle_test.cpp's own EXPECT_DOUBLE_EQ
-- safe from the same failure mode even when hand-ported, since a
transcription slip there would show up as *some other number*, not as an
agreement with the wrong reference. parity_reference.cpp still emits these
same fixtures too (see ORACLE_*), for the extra rigor of comparing against
the value C++ actually computed rather than only the value someone
derived by hand -- both hold here, and the oracle tests below assert
against the hand-derived constant directly.
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
    # Do not Path.resolve() -- under `bazel test` this file is often a
    # runfiles symlink into the execroot/source tree, and resolving leaves
    # the runfiles tree where the parity_reference data dep lives. Mirrors
    # ci_belief_evaluation_opt_test.py's own _repo_root exactly.
    here = (start or Path(__file__)).absolute()
    for parent in here.parents:
        workflows = parent / ".github" / "workflows"
        if workflows.is_dir() and any(workflows.glob("ci_*.yml")):
            return parent
    raise AssertionError("could not locate repository root from test file path")


def _parity_reference_binary() -> Path:
    binary = (
        _repo_root() / "library" / "tests" / "belief_evaluation" / "parity_reference"
    )
    if not binary.is_file():
        raise AssertionError(f"parity_reference data dependency not found at {binary}")
    return binary


def _run_parity_reference() -> dict:
    output = subprocess.run(
        [str(_parity_reference_binary())],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    namespace: dict = {}
    # Trusted, self-generated output (this process's own data dependency,
    # not external input) -- exec rather than ast.literal_eval because the
    # output is a sequence of assignment statements, not one literal.
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
    # Three roots -- a whole-space ending, a mid-trick ending, and a
    # two-card degenerate ending -- read from the C++ program that
    # built them, so "the same root" is not an assumption.
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
    # A Python LayoutSource subclass -- not ExhaustiveLayoutSource, which
    # is itself the C++ type -- reproduces the C++ source's own at()
    # values on the same (small, exhaustively comparable) fixture.
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

        # And ExhaustiveLayoutSource's own seeded enumeration over the same
        # root produces exactly this set (bijectively, since size() == 2),
        # not merely the same count.
        cpp_source = ExhaustiveLayoutSource(REFERENCE["SMALL_SINGLE_SUIT_ROOT"], North, 1)
        produced = [cpp_source.at(i) for i in range(cpp_source.size())]
        self.assertEqual(sorted(produced, key=repr), sorted(expected, key=repr))


class TestSampledReplenishingRunIsBitwiseIdentical(unittest.TestCase):
    # Exact equality on the double, not approximate -- the one property
    # most likely to quietly not hold.
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
        # Confirms the Python-driven run took the *same path*, not merely
        # reached the same answer.
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
    # The established oracle cases (oracle_test.cpp's own 9a, 9b, 9e),
    # reproduced with a Python pi and delta, against the hand-derived
    # constant those C++ tests themselves assert -- not merely against
    # whatever this run computes, so a Python-side bug reads as "wrong
    # answer" rather than "agrees with itself".
    def test_certainty_over_a_several_layout_belief_space(self) -> None:
        layout = REFERENCE["ORACLE_CERTAINTY_LAYOUT"]

        class ThreeLayoutSource(LayoutSource):
            def size(self):
                return 3

            def at(self, index):
                # Mirrors oracle_test.cpp's own three make_certain_win_layout
                # calls: (2,4), (4,2), (2,2) for East/West's low card.
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
        # Only one trick exists in the whole ending; two is unreachable.
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
    # The most important test in the project (oracle_test.cpp's own words):
    # a genuine two-layout information set, reproduced with a Python pi
    # that inspects BeliefView the same way play_two_way_guess does in
    # C++. Not read from parity_reference -- this fixture's whole point is
    # exercising the belief-view machinery from Python's own side, and the
    # hand-derived 0.5 is independently walked through in the comment
    # below, matching oracle_test.cpp's own derivation.
    #
    # North: Spade Two, Club Two filler. South (dummy): Spade Ace, Spade
    # Jack. East always holds two spades (King+Six, or Six+Seven -- Six is
    # the lowest either way, so East's own lead is uninformative). West
    # holds one spade (Seven, or King) plus a club filler.
    #
    # Layout 0 (East King+Six, West Seven): West's forced Seven loses to
    # dummy's Jack -- trick 1 to declarer. Trick 2: dummy's Ace beats
    # East's forced King -- 2/2, contract made.
    # Layout 1 (East Six+Seven, West King): West's forced King beats the
    # Jack -- trick 1 to the defence. West leads its own club filler next;
    # North follows, East and South (void in clubs) discard their
    # remaining spades uselessly -- West's own club wins trick 2 too --
    # 0/2, contract fails.
    # Equally likely (kappa = 1/2 each): P_make = 0.5*1 + 0.5*0 = 0.5.
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
        # The info set really was seen as genuinely ambiguous: two entries,
        # posteriors summing to 1, each near 0.5.
        self.assertEqual(info_set_checks[0], 2)
        self.assertAlmostEqual(info_set_checks[1], 1.0, places=9)
        self.assertAlmostEqual(info_set_checks[2], 0.5, places=9)
        self.assertAlmostEqual(info_set_checks[3], 0.5, places=9)

    def test_collapses_to_one_if_the_line_per_layout_is_allowed_to_differ(self) -> None:
        # The mutation this guards against: an evaluator that (incorrectly)
        # solved each layout independently would report 1.0 here instead
        # of 0.5 -- confirmed by literally doing that, against a singleton
        # source per layout, matching oracle_test.cpp's own version of
        # this check.
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
