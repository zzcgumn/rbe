"""Runs every Python code example in docs/belief_space_local_evaluation.md,
as close to verbatim as a script allows. The doc's placeholders, such as root,
declarer, seed and tricks_needed, are defined here with the values its prose
implies.
"""

import unittest

import dds3
import belief_space_local_evaluation as bsle
from belief_space_local_evaluation import PlaySequence

Spades, Hearts, Diamonds, Clubs = 0, 1, 2, 3
North, East, South, West = 0, 1, 2, 3
DDS_NOTRUMP = 4


def holding(*ranks: int) -> int:
    mask = 0
    for rank in ranks:
        mask |= 1 << rank
    return mask


def make_root() -> dict:
    # East and West share seven spades, two for East and five for West.
    # North and South hold one card each. The space is small enough to
    # enumerate and large enough for sampling and replenishment.
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
        # A consistent history needs all 52 cards, played or held. Every
        # card not played goes to North. North leads, and East shows out of
        # diamonds by discarding a spade.
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
        record = bsle.PlayRecord(history, opening_leader=North)

        source = bsle.ExhaustiveLayoutSource(root, declarer, seed, record=record)

        self.assertEqual(source.history_verdict(), bsle.HistoryVerdict.Consistent)

    def test_a_rejected_history_raises_immediately(self) -> None:
        with self.assertRaises(bsle.HistoryRejectedError):
            bsle.PlayRecord([bsle.Card(Spades, 9), bsle.Card(Spades, 9)], opening_leader=East)

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


class TestPlayingAHandOut(unittest.TestCase):
    def test_the_four_free_functions(self) -> None:
        # One card per hand, so every step can be derived by hand. East
        # leads.
        deal = {
            "trump": DDS_NOTRUMP,
            "first": East,
            "remain_cards": [[0, 0, 0, 0] for _ in range(4)],
            "current_trick_suit": (0, 0, 0),
            "current_trick_rank": (0, 0, 0),
        }
        deal["remain_cards"][North][Spades] = holding(9)
        deal["remain_cards"][South][Spades] = holding(8)
        deal["remain_cards"][East][Spades] = holding(2)
        deal["remain_cards"][West][Spades] = holding(3)

        seat = bsle.seat_on_play(deal)
        self.assertEqual(seat, East)

        legal = bsle.legal_cards(deal, seat)
        self.assertEqual(legal, [bsle.Card(Spades, 2)])

        after_east = bsle.play(deal, legal[0])
        after_south = bsle.play(after_east, bsle.Card(Spades, 8))
        after_west = bsle.play(after_south, bsle.Card(Spades, 3))

        winner = bsle.trick_complete_winner(after_west, bsle.Card(Spades, 9))
        self.assertEqual(winner, North)  # the nine is the highest card played

        after = bsle.play(after_west, bsle.Card(Spades, 9))
        self.assertEqual(after["first"], North)  # first reassigned to the winner
        self.assertEqual(deal["first"], East)    # the original deal is unchanged

    def test_play_out(self) -> None:
        deal = {
            "trump": DDS_NOTRUMP,
            "first": East,
            "remain_cards": [[0, 0, 0, 0] for _ in range(4)],
            "current_trick_suit": (0, 0, 0),
            "current_trick_rank": (0, 0, 0),
        }
        deal["remain_cards"][North][Spades] = holding(9)
        deal["remain_cards"][South][Spades] = holding(8)
        deal["remain_cards"][East][Spades] = holding(2)
        deal["remain_cards"][West][Spades] = holding(3)
        history = [bsle.Card(Spades, 2), bsle.Card(Spades, 8),
                   bsle.Card(Spades, 3), bsle.Card(Spades, 9)]

        root, tricks_won_by_declarer = bsle.play_out(deal, history, East, North)

        self.assertEqual(tricks_won_by_declarer, 1)
        self.assertEqual(root["first"], North)

    def test_play_sequence(self) -> None:
        # West holds the ♣6, North the ♣4, East the ♣9 and South the ♣Q.
        # South (declarer) wins with the queen.
        deal = {
            "trump": DDS_NOTRUMP,
            "first": East,  # PlaySequence's opening_leader overrides this
            "remain_cards": [[0, 0, 0, 0] for _ in range(4)],
            "current_trick_suit": (0, 0, 0),
            "current_trick_rank": (0, 0, 0),
        }
        deal["remain_cards"][West][Clubs] = holding(6)
        deal["remain_cards"][North][Clubs] = holding(4)
        deal["remain_cards"][East][Clubs] = holding(9)
        deal["remain_cards"][South][Clubs] = holding(12)  # queen

        sequence = PlaySequence(deal, declarer=South, trump=DDS_NOTRUMP, level=6)
        sequence.play_trick("C6 C4 C9 CQ")
        root, tricks_needed = sequence.current_deal, sequence.tricks_needed

        self.assertEqual(sequence.tricks_won_by_declarer, 1)
        self.assertEqual(tricks_needed, 11)  # 6 + 6 - 1
        self.assertEqual(root["first"], South)  # the queen's seat, on lead next


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
        # The doc's pi and delta return a fixed card ("return
        # bsle.Card(0, 14)"), which is legal only where that card is held.
        # This test keeps the doc's signatures and return types, and plays
        # the lowest card instead.
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
    # The Exceptions section is prose, but its two claims are checked: one
    # base class, and a propagated Python exception keeps its type.
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
    # solve_board, the double-dummy solver, needs equal hand sizes.
    # North holds ♠Q J, South ♠3 2, East ♠A 4 and West ♠K 5, and each has
    # one club.
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
