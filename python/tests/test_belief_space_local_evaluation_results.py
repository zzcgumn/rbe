import unittest

from belief_space_local_evaluation import Card
from belief_space_local_evaluation import evaluate
from belief_space_local_evaluation import ExhaustiveLayoutSource

Spades, Hearts, Diamonds, Clubs = 0, 1, 2, 3
North, East, South, West = 0, 1, 2, 3
DDS_NOTRUMP = 4


def holding(*ranks: int) -> int:
    mask = 0
    for rank in ranks:
        mask |= 1 << rank
    return mask


def make_one_card_finesse_root() -> dict:
    # A defender (East) leads at the root -- East/West share a seven-card
    # pool, North/South hold one card each. Mirrors
    # exhaustive_layout_source_integration_test.cpp's own fixture.
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


def make_declarer_choice_root() -> dict:
    # North (declarer) leads at the root and holds two cards -- a genuine
    # choice, both of which beat every other hand's single card, so
    # whichever North picks wins the trick: every root_children entry has
    # value 1.0, and their sum (2.0) does not equal p_make (1.0) -- the
    # shape the "alternatives, not a partition" distinction needs.
    remain_cards = [[0, 0, 0, 0] for _ in range(4)]
    remain_cards[North][Spades] = holding(9, 14)
    remain_cards[South][Spades] = holding(8)
    remain_cards[East][Spades] = holding(2)
    remain_cards[West][Spades] = holding(3)
    return {
        "trump": DDS_NOTRUMP,
        "first": North,
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


class TestPMakeIsBitwiseExact(unittest.TestCase):
    def test_p_make_is_a_float_matching_the_hand_derived_value_exactly(self) -> None:
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)
        result = evaluate(root, North, 1, source, declarer_play, defender_play)
        p_make = result["by_strategy"][1]["p_make"]
        self.assertIsInstance(p_make, float)
        self.assertEqual(p_make, 6.0 / 7.0)


class TestRootChildren(unittest.TestCase):
    def test_at_a_defender_root_children_partition_and_sum_to_p_make(self) -> None:
        root = make_one_card_finesse_root()  # East (a defender) leads
        source = ExhaustiveLayoutSource(root, North, 5)
        result = evaluate(root, North, 1, source, declarer_play, defender_play)
        entry = result["by_strategy"][1]
        total = sum(value for _card, value in entry["root_children"])
        self.assertAlmostEqual(total, entry["p_make"], places=9)

    def test_at_a_declarer_root_children_are_alternatives_not_a_partition(self) -> None:
        root = make_declarer_choice_root()  # North (declarer) leads
        source = ExhaustiveLayoutSource(root, North, 1)
        result = evaluate(root, North, 1, source, declarer_play, defender_play)
        entry = result["by_strategy"][1]
        children = entry["root_children"]
        self.assertEqual(len(children), 2)  # North's two legal cards, both evaluated
        for card, value in children:
            self.assertIsInstance(card, Card)
            self.assertEqual(value, 1.0)  # each wins the trick regardless of which is played
        total = sum(value for _card, value in children)
        self.assertNotEqual(total, entry["p_make"])  # 2.0 != 1.0 -- not a partition here
        self.assertEqual(entry["p_make"], 1.0)

    def test_root_is_declaring_side_says_which_of_the_two_shapes_it_is(self) -> None:
        # The two tests above each know which shape they built. A consumer does
        # not, and cannot tell from the values: summing is meaningful at one
        # root and meaningless at the other. The flag is what makes that
        # readable, so it is asserted against the two roots those tests use.
        defender_root = make_one_card_finesse_root()  # East leads
        declarer_root = make_declarer_choice_root()   # North leads

        at_defender = evaluate(
            defender_root, North, 1, ExhaustiveLayoutSource(defender_root, North, 5),
            declarer_play, defender_play)["by_strategy"][1]
        at_declarer = evaluate(
            declarer_root, North, 1, ExhaustiveLayoutSource(declarer_root, North, 1),
            declarer_play, defender_play)["by_strategy"][1]

        self.assertIs(at_defender["root_is_declaring_side"], False)
        self.assertIs(at_declarer["root_is_declaring_side"], True)

    def test_root_is_declaring_side_is_present_at_a_terminal_root_too(self) -> None:
        # root_children is empty there, and the flag still describes the root:
        # a consumer branching on it must not have to special-case emptiness.
        root = make_one_card_finesse_root()
        result = evaluate(root, North, 0, ExhaustiveLayoutSource(root, North, 5),
                          declarer_play, defender_play)
        entry = result["by_strategy"][1]

        self.assertEqual(entry["root_children"], [])
        self.assertIs(entry["root_is_declaring_side"], False)

    def test_root_children_is_empty_at_a_terminal_root(self) -> None:
        # tricks_needed=0: already_made() fires before any card is chosen.
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)
        result = evaluate(root, North, 0, source, declarer_play, defender_play)
        entry = result["by_strategy"][1]
        self.assertEqual(entry["root_children"], [])
        self.assertEqual(entry["p_make"], 1.0)


class TestCounters(unittest.TestCase):
    def test_counters_absent_by_default(self) -> None:
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)
        result = evaluate(root, North, 1, source, declarer_play, defender_play)
        self.assertNotIn("counters", result["by_strategy"][1])

    def test_counters_present_with_every_field_when_collected(self) -> None:
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)
        result = evaluate(
            root, North, 1, source, declarer_play, defender_play, collect_counters=True)
        counters = result["by_strategy"][1]["counters"]

        for field in ("nodes_visited", "tier1_made_cuts", "tier1_dead_cuts", "tier2_cuts"):
            self.assertIn(field, counters)
            self.assertIsInstance(counters[field], int)

        self.assertIn("sample_size_by_depth", counters)
        self.assertIn("replenishment_by_depth", counters)
        self.assertGreater(len(counters["sample_size_by_depth"]), 0)
        for depth in counters["sample_size_by_depth"]:
            for field in ("nodes", "layout_sum", "layout_min"):
                self.assertIn(field, depth)

        # replenish_below is unset, so no scan ever ran -- the vector can
        # legitimately be empty (an index beyond it means "no node was
        # ever visited at this depth", not that a scan attempted nothing).
        for depth in counters["replenishment_by_depth"]:
            for field in ("attempted", "succeeded", "layouts_added", "at_calls"):
                self.assertIn(field, depth)

    def test_collecting_counters_changes_no_answer(self) -> None:
        # Mirrors counters_test.cpp's own paired-run check, from Python:
        # the same fixture with and without collect_counters, p_make
        # bitwise equal both directions.
        root = make_one_card_finesse_root()

        without = evaluate(
            root, North, 1, ExhaustiveLayoutSource(root, North, 5), declarer_play, defender_play,
            collect_counters=False)
        with_counters = evaluate(
            root, North, 1, ExhaustiveLayoutSource(root, North, 5), declarer_play, defender_play,
            collect_counters=True)

        self.assertEqual(
            without["by_strategy"][1]["p_make"], with_counters["by_strategy"][1]["p_make"])


class TestRetainedRoot(unittest.TestCase):
    def test_retained_root_absent_by_default(self) -> None:
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)
        result = evaluate(root, North, 1, source, declarer_play, defender_play)
        self.assertNotIn("retained_root", result["by_strategy"][1])

    def test_retained_root_is_the_root_node_only(self) -> None:
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)
        result = evaluate(
            root, North, 1, source, declarer_play, defender_play, retain_root=True)
        retained = result["by_strategy"][1]["retained_root"]

        self.assertEqual(len(retained["layouts"]), 7)  # C(7, 1); the whole root space
        self.assertEqual(len(retained["p"]), 7)
        self.assertEqual(len(retained["root_keys"]), 7)
        self.assertIn("is_sample", retained)
        self.assertIn("no_more_available", retained)
        # kappa must never cross -- the evaluator's own sample-weight
        # bookkeeping, not something a caller reading a result needs.
        self.assertNotIn("kappa", retained)
        # Not a tree: nothing here names a child or a further recursion.
        self.assertNotIn("children", retained)


if __name__ == "__main__":
    unittest.main()
