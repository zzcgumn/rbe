import unittest

from belief_space_local_evaluation import Card
from belief_space_local_evaluation import evaluate
from belief_space_local_evaluation import ExhaustiveLayoutSource
from belief_space_local_evaluation import RootFailure
from belief_space_local_evaluation import SampleSizeZeroError

Spades, Hearts, Diamonds, Clubs = 0, 1, 2, 3
North, East, South, West = 0, 1, 2, 3
DDS_NOTRUMP = 4


def holding(*ranks: int) -> int:
    mask = 0
    for rank in ranks:
        mask |= 1 << rank
    return mask


def make_one_card_finesse_root() -> dict:
    # North (declarer) holds a single middling card (Nine); South (dummy)
    # holds a single safely-lower card (Eight); East and West share a
    # seven-card pool {Two..Seven, Ten}. Mirrors
    # exhaustive_layout_source_integration_test.cpp's own fixture of the
    # same name -- small enough to state the hand-derived answer by hand,
    # per this task's own steps.
    remain_cards = [[0, 0, 0, 0] for _ in range(4)]
    remain_cards[North][Spades] = holding(9)
    remain_cards[South][Spades] = holding(8)
    remain_cards[East][Spades] = holding(2)
    remain_cards[West][Spades] = holding(3, 4, 5, 6, 7, 10)
    return {
        "trump": DDS_NOTRUMP,
        "first": East,
        "remain_cards": remain_cards,
        "current_trick_suit": (0, 0, 0),
        "current_trick_rank": (0, 0, 0),
    }


def lowest_card_in(remain_cards_row) -> Card:
    for suit in range(4):
        mask = remain_cards_row[suit]
        if mask:
            rank = 2
            while not (mask & (1 << rank)):
                rank += 1
            return Card(suit, rank)
    raise AssertionError("seat holds nothing")


def declarer_play(state, view):
    del view
    seat = (state.first + len(state.history)) % 4
    holdings = state.known_holdings["remain_cards"]
    return lowest_card_in(holdings[seat])


def defender_play(layout, seat, state):
    del state
    return [(lowest_card_in(layout["remain_cards"][seat]), 1.0)]


class TestTheEntryPointWorks(unittest.TestCase):
    def test_evaluate_with_no_options_returns_a_result(self) -> None:
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 1)

        result = evaluate(root, North, 1, source, declarer_play, defender_play)

        self.assertNotIn("error", result)
        self.assertIn(1, result["by_strategy"])


class TestBitwiseMatchAgainstTheExhaustiveAnswer(unittest.TestCase):
    def test_reproduces_the_hand_derived_value_exactly(self) -> None:
        # Not approx: sample_size=0.9999... would be a different bug than
        # this criterion is pinning. East holds one of {2,3,4,5,6,7,10};
        # North's Nine wins the trick unless East's own card already beats
        # it (only Ten does). 6 of 7 -> 6/7 exactly, the same double a C++
        # run of the equivalent fixture produces.
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)

        result = evaluate(root, North, 1, source, declarer_play, defender_play)

        self.assertEqual(result["by_strategy"][1]["p_make"], 6.0 / 7.0)


class TestKeywordOnlyOptions(unittest.TestCase):
    def test_options_are_keyword_only(self) -> None:
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 1)
        with self.assertRaises(TypeError):
            # retain_root positionally, where bound's own precedent from
            # C++ argument order might tempt a caller -- must reject, not
            # silently accept into the wrong slot.
            evaluate(root, North, 1, source, declarer_play, defender_play, False)

    def test_retain_root_and_collect_counters_accepted(self) -> None:
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 1)
        result = evaluate(
            root, North, 1, source, declarer_play, defender_play,
            retain_root=True, collect_counters=True)
        self.assertNotIn("error", result)

    def test_delta_is_double_dummy_optimal_accepted(self) -> None:
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 1)
        result = evaluate(
            root, North, 1, source, declarer_play, defender_play,
            delta_is_double_dummy_optimal=True)
        self.assertNotIn("error", result)

    def test_bound_is_called_with_a_deal_dict_and_returns_an_int(self) -> None:
        seen = []

        def bound(deal):
            seen.append(deal)
            return 13  # never dead -- disables tier2_dead() from firing

        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 1)
        result = evaluate(
            root, North, 1, source, declarer_play, defender_play,
            delta_is_double_dummy_optimal=True, bound=bound)

        self.assertNotIn("error", result)
        self.assertGreater(len(seen), 0)
        self.assertIsInstance(seen[0], dict)
        self.assertIn("remain_cards", seen[0])

    def test_sample_size_reproduces_the_exhaustive_answer_when_at_least_size(self) -> None:
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 9)
        result = evaluate(
            root, North, 1, source, declarer_play, defender_play, sample_size=1000)
        self.assertEqual(result["by_strategy"][1]["p_make"], 6.0 / 7.0)

    def test_scan_budget_alone_does_not_raise(self) -> None:
        # scan_budget without sample_size legitimately caps a scan on its
        # own and must not be treated as a mistake.
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 1)
        result = evaluate(
            root, North, 1, source, declarer_play, defender_play, scan_budget=1000)
        self.assertNotIn("error", result)


class TestReplenishBelowRequiresSampleSize(unittest.TestCase):
    def test_replenish_below_without_sample_size_raises(self) -> None:
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 1)
        with self.assertRaises(ValueError):
            evaluate(
                root, North, 1, source, declarer_play, defender_play, replenish_below=2)

    def test_replenish_below_with_sample_size_does_not_raise(self) -> None:
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 1)
        result = evaluate(
            root, North, 1, source, declarer_play, defender_play,
            sample_size=100, replenish_below=2)
        self.assertNotIn("error", result)


class TestSampleSizeZeroRaises(unittest.TestCase):
    def test_sample_size_zero_raises_rather_than_returning_a_result(self) -> None:
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 1)
        with self.assertRaises(SampleSizeZeroError):
            evaluate(root, North, 1, source, declarer_play, defender_play, sample_size=0)

    def test_sample_size_zero_is_distinguishable_from_an_ordinary_root_failure(self) -> None:
        # An ordinary empty source (nothing consistent) is NoLayoutSurvived,
        # not SampleSizeZeroError -- the two must never be confused.
        # Constructed by hand here (not through ExhaustiveLayoutSource,
        # which cannot itself produce an inconsistent candidate) via a tiny
        # Python LayoutSource whose one candidate does not match root.
        from belief_space_local_evaluation import LayoutSource

        class EmptySource(LayoutSource):
            def size(self):
                return 1

            def at(self, index):
                deal = make_one_card_finesse_root()
                # Give declarer a card it does not hold at the real root --
                # inconsistent, so make_root's own filter drops it.
                deal["remain_cards"][North][Spades] = holding(9, 8)
                return deal

        root = make_one_card_finesse_root()
        result = evaluate(root, North, 1, EmptySource(), declarer_play, defender_play)

        self.assertIn("error", result)
        self.assertEqual(result["error"]["root_failure"], RootFailure.NoLayoutSurvived)


if __name__ == "__main__":
    unittest.main()
