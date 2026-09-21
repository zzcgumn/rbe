"""Runs every Python code example in docs/belief_space_local_evaluation.md,
as close to verbatim as a runnable script requires (the doc's own snippets
use placeholder names -- root, declarer, seed, tricks_needed -- that this
file defines once and reuses, exactly the values the doc's prose implies).
An example that does not run is worse than no example; this is how "every
code example in the document runs" is verified rather than eyeballed.
"""

import unittest

import dds3
import belief_space_local_evaluation as bsle

Spades, Hearts, Diamonds, Clubs = 0, 1, 2, 3
North, East, South, West = 0, 1, 2, 3
DDS_NOTRUMP = 4


def holding(*ranks: int) -> int:
    mask = 0
    for rank in ranks:
        mask |= 1 << rank
    return mask


def make_root() -> dict:
    # East/West share a seven-card spade pool (East 2, West 5); North/South
    # hold one card each. Small enough to enumerate exhaustively, large
    # enough for sample_size/replenish_below to have real headroom.
    remain_cards = [[0, 0, 0, 0] for _ in range(4)]
    remain_cards[North][Spades] = holding(9)
    remain_cards[South][Spades] = holding(8)
    remain_cards[East][Spades] = holding(2, 3)
    remain_cards[West][Spades] = holding(4, 5, 6, 7, 10)
    return {
        "trump": DDS_NOTRUMP,
        "first": East,
        "remain_cards": remain_cards,
        "current_trick_suit": (0, 0, 0),
        "current_trick_rank": (0, 0, 0),
    }


class TestImport(unittest.TestCase):
    def test_import(self) -> None:
        import belief_space_local_evaluation as bsle  # noqa: F401


class TestALayoutSource(unittest.TestCase):
    def test_exhaustive_layout_source(self) -> None:
        root = make_root()
        declarer = North
        seed = 1

        source = bsle.ExhaustiveLayoutSource(root, declarer, seed)

        self.assertEqual(source.size(), 21)  # C(7, 2)

    def test_exhaustive_layout_source_with_history(self) -> None:
        # A consistent history needs the *whole* deck accounted for
        # (played, or still held somewhere) -- every card not explicitly
        # played goes to North by default, so the fixture states only
        # what it cares about: North leads, East shows void in diamonds by
        # discarding a spade, and root reflects the position afterwards.
        played = [(Diamonds, 14), (Spades, 2), (Diamonds, 13), (Diamonds, 12)]
        played_set = set(played)
        remain_cards = [[0, 0, 0, 0] for _ in range(4)]
        for suit in range(4):
            for rank in range(2, 15):
                if (suit, rank) in played_set:
                    continue
                remain_cards[North][suit] |= 1 << rank
        root = {
            "trump": DDS_NOTRUMP,
            "first": North,
            "remain_cards": remain_cards,
            "current_trick_suit": (0, 0, 0),
            "current_trick_rank": (0, 0, 0),
        }
        declarer = North
        seed = 1
        history = [bsle.Card(suit, rank) for suit, rank in played]

        source = bsle.ExhaustiveLayoutSource(
            root, declarer, seed, history=history, opening_leader=North)

        self.assertEqual(source.history_verdict(), bsle.HistoryVerdict.Consistent)

    def test_a_rejected_history_raises_immediately(self) -> None:
        root = make_root()
        with self.assertRaises(bsle.HistoryRejectedError):
            bsle.ExhaustiveLayoutSource(
                root, North, 1, history=[bsle.Card(Spades, 9), bsle.Card(Spades, 9)],
                opening_leader=East)

    def test_my_source_subclass(self) -> None:
        deals = [make_root(), make_root()]

        class MySource(bsle.LayoutSource):
            def __init__(self):
                super().__init__()
                self._deals = deals

            def size(self):
                return len(self._deals)

            def at(self, index):
                return self._deals[index]  # must already be in a randomised order

        source = MySource()
        self.assertEqual(source.size(), 2)
        self.assertEqual(source.at(0), deals[0])


def lowest_card_in(remain_cards_row):
    for suit in range(4):
        mask = remain_cards_row[suit]
        if mask:
            rank = 2
            while not (mask & (1 << rank)):
                rank += 1
            return bsle.Card(suit, rank)
    raise AssertionError("seat holds nothing")


def seat_on_play(deal: dict) -> int:
    played = 0
    for rank in deal["current_trick_rank"]:
        if rank == 0:
            break
        played += 1
    return (deal["first"] + played) % 4


class TestPiAndDelta(unittest.TestCase):
    def test_pi_and_delta_shapes_run(self) -> None:
        # The doc's own pi/delta bodies are illustrative fixed plays
        # ("return bsle.Card(0, 14)"), which is only legal at a node where
        # that exact card is held -- not true throughout a real search.
        # This test confirms the *shape* (arguments in, a Card out; a list
        # of (Card, probability) pairs out) is exactly what evaluate()
        # calls with and accepts, using real forced/lowest-legal bodies
        # instead of the doc's fixed example so it runs to completion.
        def pi(state, view):
            del view
            return lowest_card_in(state.known_holdings["remain_cards"][seat_on_play(state.known_holdings)])

        def delta(layout, seat, state):
            del state
            return [(lowest_card_in(layout["remain_cards"][seat]), 1.0)]

        root = make_root()
        declarer = North
        source = bsle.ExhaustiveLayoutSource(root, declarer, 1)
        result = bsle.evaluate(root, declarer, 1, source, pi, delta)
        self.assertNotIn("error", result)


class TestEvaluate(unittest.TestCase):
    def test_evaluate_with_no_options(self) -> None:
        root = make_root()
        declarer = North
        tricks_needed = 1
        source = bsle.ExhaustiveLayoutSource(root, declarer, 1)

        def pi(state, view):
            del view
            return lowest_card_in(
                state.known_holdings["remain_cards"][seat_on_play(state.known_holdings)])

        def delta(layout, seat, state):
            del state
            return [(lowest_card_in(layout["remain_cards"][seat]), 1.0)]

        result = bsle.evaluate(root, declarer, tricks_needed, source, pi, delta)
        self.assertNotIn("error", result)

    def test_evaluate_with_sampling_options_and_result_shape(self) -> None:
        root = make_root()
        declarer = North
        tricks_needed = 1
        source = bsle.ExhaustiveLayoutSource(root, declarer, 1)

        def pi(state, view):
            del view
            return lowest_card_in(
                state.known_holdings["remain_cards"][seat_on_play(state.known_holdings)])

        def delta(layout, seat, state):
            del state
            return [(lowest_card_in(layout["remain_cards"][seat]), 1.0)]

        result = bsle.evaluate(
            root, declarer, tricks_needed, source, pi, delta,
            sample_size=200, replenish_below=20)

        value = result["by_strategy"][1]  # keyed by pi's strategy id, always 1 here
        self.assertIsInstance(value["p_make"], float)
        for card, weight in value["root_children"]:
            self.assertIsInstance(card, bsle.Card)
            self.assertIsInstance(weight, float)


class TestExceptionsSection(unittest.TestCase):
    # The Exceptions section is prose, not a code block, but its two
    # concrete claims are checked directly: the shared root, and that a
    # propagated Python exception keeps its own type.
    def test_every_family_is_rooted_at_the_same_base(self) -> None:
        self.assertTrue(issubclass(bsle.HistoryRejectedError, bsle.BeliefSpaceLocalEvaluationError))
        self.assertTrue(issubclass(bsle.ConstrainedSpaceEmptyError, bsle.BeliefSpaceLocalEvaluationError))
        self.assertTrue(issubclass(bsle.RootFailureError, bsle.BeliefSpaceLocalEvaluationError))
        self.assertTrue(issubclass(bsle.CallbackContractError, bsle.BeliefSpaceLocalEvaluationError))
        self.assertFalse(issubclass(bsle.NoLayoutSurvivedError, bsle.HistoryRejectedError))

    def test_a_propagated_exception_keeps_its_own_type(self) -> None:
        class Boom(RuntimeError):
            pass

        def pi(state, view):
            del state, view
            raise Boom("deliberate")

        def delta(layout, seat, state):
            del state
            return [(lowest_card_in(layout["remain_cards"][seat]), 1.0)]

        root = make_root()
        source = bsle.ExhaustiveLayoutSource(root, North, 1)
        with self.assertRaises(Boom):
            bsle.evaluate(root, North, 1, source, pi, delta)


def make_solver_seam_root() -> dict:
    # solve_board (the real double-dummy solver, unlike the scripted
    # strategies used everywhere else in this file) requires all four
    # hands to hold the same number of cards -- an ordinary well-formed
    # ending, not merely something belief_evaluation's own machinery
    # tolerates. North: Q, J of spades; South: 2, 3; East: A, 4; West: K,
    # 5; one club filler each.
    remain_cards = [[0, 0, 0, 0] for _ in range(4)]
    remain_cards[North][Spades] = holding(12, 11)
    remain_cards[South][Spades] = holding(2, 3)
    remain_cards[East][Spades] = holding(14, 4)
    remain_cards[West][Spades] = holding(13, 5)
    remain_cards[North][Clubs] = holding(6)
    remain_cards[South][Clubs] = holding(7)
    remain_cards[East][Clubs] = holding(8)
    remain_cards[West][Clubs] = holding(9)
    return {
        "trump": DDS_NOTRUMP,
        "first": East,
        "remain_cards": remain_cards,
        "current_trick_suit": (0, 0, 0),
        "current_trick_rank": (0, 0, 0),
    }


def lowest_legal_card(deal: dict, seat: int):
    remain_cards = deal["remain_cards"][seat]
    if deal["current_trick_rank"][0] != 0:
        led = deal["current_trick_suit"][0]
        if remain_cards[led] != 0:
            return lowest_card_in([remain_cards[led] if s == led else 0 for s in range(4)])
    return lowest_card_in(remain_cards)


class TestTheSolverSeam(unittest.TestCase):
    def test_the_solver_seam_example(self) -> None:
        root = make_solver_seam_root()
        declarer = North

        ctx = dds3.SolverContext()
        delta = bsle.DoubleDummyDefender(ctx)  # usable directly as delta
        bound = bsle.DoubleDummyBound(ctx, declarer)  # usable directly as bound

        def pi(state, view):
            del view
            return lowest_legal_card(state.known_holdings, seat_on_play(state.known_holdings))

        source = bsle.ExhaustiveLayoutSource(root, declarer, 1)
        result = bsle.evaluate(
            root, declarer, 1, source, pi, delta,
            bound=bound, delta_is_double_dummy_optimal=True)
        self.assertNotIn("error", result)


if __name__ == "__main__":
    unittest.main()
