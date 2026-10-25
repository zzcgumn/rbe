"""Tests for Python pi and delta callables passed to evaluate(), and for the
ObservationState properties a Python strategy reads."""

import unittest

from belief_space_local_evaluation import Card
from belief_space_local_evaluation import CardNotHeldError
from belief_space_local_evaluation import evaluate
from belief_space_local_evaluation import legal_cards
from belief_space_local_evaluation import ExhaustiveLayoutSource
from belief_space_local_evaluation import ProbabilitiesDoNotSumToOneError
from belief_space_local_evaluation import RankMap
from belief_space_local_evaluation import seat_on_play

Spades, Hearts, Diamonds, Clubs = 0, 1, 2, 3
North, East, South, West = 0, 1, 2, 3
DDS_NOTRUMP = 4


def holding(*ranks: int) -> int:
    mask = 0
    for rank in ranks:
        mask |= 1 << rank
    return mask


def make_one_card_finesse_root() -> dict:
    # North (declarer) holds the ♠9 and South (dummy) the ♠8. East and West
    # share a seven-card pool: ♠2 to ♠7 and ♠10. Mirrors the fixture of the
    # same name in exhaustive_layout_source_integration_test.cpp.
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
    # A pure function of (state, view), as DeclarerStrategy::play requires.
    # Declarer and dummy hold one card each, so only one trick is played.
    # The seat on play is first plus the cards played so far, mod 4. Only
    # spades exist, so the lowest card held is the lowest legal card.
    del view
    seat = (state.first + len(state.history)) % 4
    holdings = state.known_holdings["remain_cards"]
    return lowest_card_in(holdings[seat])


def defender_play(layout, seat, state):
    del state
    return [(lowest_card_in(layout["remain_cards"][seat]), 1.0)]


class TestDeclarerAndDefenderCallables(unittest.TestCase):
    def test_a_python_pi_and_delta_reach_the_hand_derived_answer(self) -> None:
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)

        result = evaluate(root, North, 1, source, declarer_play, defender_play)

        self.assertNotIn("error", result)
        # East holds one of the seven pool cards. North's nine wins unless
        # East holds the ten: 6/7.
        self.assertAlmostEqual(result["by_strategy"][1]["p_make"], 6.0 / 7.0, places=9)

    def test_root_children_survive_the_call_with_correct_values(self) -> None:
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)

        result = evaluate(root, North, 1, source, declarer_play, defender_play)
        children = result["by_strategy"][1]["root_children"]
        self.assertGreaterEqual(len(children), 1)
        for card, value in children:
            self.assertIsInstance(card, Card)
            self.assertIsInstance(value, float)


class TestObservationStateExposesRanks(unittest.TestCase):
    # ObservationState binds every field a C++ strategy can read, including
    # ranks: a RankMap between absolute and relative ranks of the
    # outstanding cards. Python cannot construct a RankMap, so each test
    # captures one from a pi call.
    def test_ranks_is_the_publicly_importable_rankmap_type(self) -> None:
        # The package __init__ must re-export RankMap, or
        # `from belief_space_local_evaluation import RankMap` fails.
        captured = {}

        def pi(state, view):
            del view
            captured["ranks"] = state.ranks
            seat = (state.first + len(state.history)) % 4
            return lowest_card_in(state.known_holdings["remain_cards"][seat])

        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)
        evaluate(root, North, 1, source, pi, defender_play)

        self.assertIsInstance(captured["ranks"], RankMap)

    def test_aggr_matches_known_holdings_own_pool_at_the_same_node(self) -> None:
        # Checked against known_holdings from the same call, since both
        # come from the same node. A defender's entry there is already the
        # pooled cards. East leads, so a card has gone before pi first runs.
        # Deriving the pool by hand would test the recursion, not RankMap.
        captured = {}

        def pi(state, view):
            del view
            captured["ranks"] = state.ranks
            captured["known_holdings"] = state.known_holdings
            seat = (state.first + len(state.history)) % 4
            return lowest_card_in(state.known_holdings["remain_cards"][seat])

        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)
        evaluate(root, North, 1, source, pi, defender_play)

        ranks = captured["ranks"]
        remain_cards = captured["known_holdings"]["remain_cards"]
        for suit in (Spades, Hearts, Diamonds, Clubs):
            pool_bits = 0
            for hand in range(4):
                pool_bits |= remain_cards[hand][suit]
            expected_aggr = (pool_bits >> 2) & 0x1FFF
            self.assertEqual(ranks.aggr[suit], expected_aggr, f"suit {suit}")
        # Not vacuous: the spade pool is not empty.
        self.assertNotEqual(ranks.aggr[Spades], 0)

    def test_to_relative_and_to_absolute_round_trip_the_top_card(self) -> None:
        captured = {}

        def pi(state, view):
            del view
            captured["ranks"] = state.ranks
            captured["known_holdings"] = state.known_holdings
            seat = (state.first + len(state.history)) % 4
            return lowest_card_in(state.known_holdings["remain_cards"][seat])

        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)
        evaluate(root, North, 1, source, pi, defender_play)

        ranks = captured["ranks"]
        remain_cards = captured["known_holdings"]["remain_cards"]
        pool_bits = 0
        for hand in range(4):
            pool_bits |= remain_cards[hand][Spades]
        highest_outstanding = max(r for r in range(2, 15) if pool_bits & (1 << r))

        self.assertEqual(ranks.to_relative(Spades, highest_outstanding), 1)
        self.assertEqual(ranks.to_absolute(Spades, 1), highest_outstanding)
        # No seat holds the ace, so it is never outstanding.
        self.assertEqual(ranks.to_relative(Spades, 14), 0)

    def test_out_of_range_suit_or_rank_returns_zero_not_an_error(self) -> None:
        # As in C++, out-of-range input means "not outstanding", not an
        # error.
        captured = {}

        def pi(state, view):
            del view
            captured["ranks"] = state.ranks
            seat = (state.first + len(state.history)) % 4
            return lowest_card_in(state.known_holdings["remain_cards"][seat])

        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)
        evaluate(root, North, 1, source, pi, defender_play)

        ranks = captured["ranks"]
        self.assertEqual(ranks.to_relative(-1, 5), 0)
        self.assertEqual(ranks.to_relative(Spades, 1), 0)
        self.assertEqual(ranks.to_absolute(Spades, 0), 0)


def make_two_cards_each_root() -> dict:
    # Two cards per hand, so the winner of trick one leads trick two. Only
    # then can `trick_leader` differ from `first`, which stays the root's
    # leader. North (declarer) holds ♠K J and South (dummy) ♠10 9. The
    # defenders share ♠Q 6 5 4. East leads.
    remain_cards = [[0, 0, 0, 0] for _ in range(4)]
    remain_cards[North][Spades] = holding(13, 11)
    remain_cards[South][Spades] = holding(10, 9)
    remain_cards[East][Spades] = holding(12, 4)
    remain_cards[West][Spades] = holding(6, 5)
    return {
        "trump": DDS_NOTRUMP,
        "first": East,
        "remain_cards": remain_cards,
        "current_trick_suit": (0, 0, 0),
        "current_trick_rank": (0, 0, 0),
    }


class TestObservationStateDerivedProperties(unittest.TestCase):
    """The seven properties a strategy would otherwise derive by hand.

    Python cannot construct an ObservationState, so the tests capture states
    from pi calls. At a root, `trick_leader` equals `first` and
    `position_in_trick` is 0. A root-only test would pass an implementation
    that returned `first` and 0.
    """

    def _observe(self, root: dict) -> list:
        seen = []

        def pi(state, view):
            del view
            seen.append({
                "seat_on_play": state.seat_on_play,
                "trick_leader": state.trick_leader,
                "first": state.first,
                "current_trick": [(c.suit, c.rank) for c in state.current_trick],
                "position_in_trick": state.position_in_trick,
                "legal_cards": [(c.suit, c.rank) for c in state.legal_cards],
                "can_follow_led_suit": state.can_follow_led_suit,
                "is_declaring_side": state.is_declaring_side,
                "known_holdings": state.known_holdings,
            })
            return min(state.legal_cards, key=lambda card: (card.rank, card.suit))

        # tricks_needed is 2. North's king always wins a trick, so with 1 the
        # search stops after trick one. trick_leader would then equal first
        # at every call, and pi would never lead. The guards below check
        # this.
        source = ExhaustiveLayoutSource(root, North, 5)
        result = evaluate(root, North, 2, source, pi, defender_play)
        self.assertNotIn("error", result)
        return seen

    def test_seat_on_play_and_legal_cards_agree_with_the_free_functions(self) -> None:
        # The state properties must agree with the module-level functions
        # applied to the state's known_holdings.
        for call in self._observe(make_two_cards_each_root()):
            deal = call["known_holdings"]
            self.assertEqual(call["seat_on_play"], seat_on_play(deal))
            self.assertEqual(
                call["legal_cards"],
                [(c.suit, c.rank) for c in legal_cards(deal, call["seat_on_play"])])

    def test_trick_leader_is_not_first_once_a_trick_has_been_won(self) -> None:
        # `first` is the root's leader and never changes. The winner of the
        # last trick leads the current one. A strategy that reads `first` is
        # wrong from the second trick on.
        calls = self._observe(make_two_cards_each_root())

        diverged = [c for c in calls if c["trick_leader"] != c["first"]]
        self.assertTrue(
            diverged,
            "no call saw trick_leader differ from first, so this test cannot "
            "catch an implementation that just returns first -- the fixture "
            "needs to play more than one trick")
        for call in calls:
            self.assertEqual(call["first"], East)  # the root's leader, always

    def test_current_trick_is_empty_exactly_when_leading(self) -> None:
        calls = self._observe(make_two_cards_each_root())

        self.assertTrue(any(c["position_in_trick"] == 0 for c in calls))
        self.assertTrue(any(c["position_in_trick"] != 0 for c in calls))
        for call in calls:
            self.assertEqual(len(call["current_trick"]), call["position_in_trick"])
            self.assertEqual(call["current_trick"] == [], call["position_in_trick"] == 0)

    def test_current_trick_is_empty_when_leading_even_though_spades_is_suit_zero(self) -> None:
        # An empty trick has current_trick_suit == (0, 0, 0), and spades are
        # suit 0. Only the 0 rank in current_trick_rank marks an empty trick.
        # Every card here is a spade, so a property that read the suit would
        # report a led spade at every lead.
        for call in self._observe(make_two_cards_each_root()):
            if call["position_in_trick"] == 0:
                self.assertEqual(call["current_trick"], [])
            else:
                self.assertEqual(call["current_trick"][0][0], Spades)

    def test_can_follow_led_suit_matches_the_holding(self) -> None:
        for call in self._observe(make_two_cards_each_root()):
            deal = call["known_holdings"]
            if call["position_in_trick"] == 0:
                # Nothing led, so the property is False, not vacuously true.
                # A strategy's `if not can_follow_led_suit` branch then picks
                # freely on a lead, as on a discard.
                self.assertFalse(call["can_follow_led_suit"])
            else:
                led = call["current_trick"][0][0]
                held = deal["remain_cards"][call["seat_on_play"]][led] != 0
                self.assertEqual(call["can_follow_led_suit"], held)

    def test_is_declaring_side_is_true_for_dummy_as_well_as_declarer(self) -> None:
        # pi plays for declarer and dummy, so this is true at every pi call.
        # It must not be computed as `seat == declarer`.
        calls = self._observe(make_two_cards_each_root())

        for call in calls:
            self.assertTrue(call["is_declaring_side"])
        seats = {c["seat_on_play"] for c in calls}
        self.assertIn(South, seats, "dummy never played, so the dummy half is untested")
        self.assertIn(North, seats)


class TestStateKey(unittest.TestCase):
    # Nothing calls state_key yet. These tests only check that passing None
    # or a callable that returns bytes leaves the result unchanged.
    def test_none_disables_reuse_and_still_works(self) -> None:
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)
        result = evaluate(root, North, 1, source, declarer_play, defender_play, state_key=None)
        self.assertNotIn("error", result)
        self.assertAlmostEqual(result["by_strategy"][1]["p_make"], 6.0 / 7.0, places=9)

    def test_a_callable_returning_bytes_also_works_and_does_not_change_the_result(self) -> None:
        def state_key(state, view):
            del view
            return bytes([state.declarer])

        root = make_one_card_finesse_root()
        source_without = ExhaustiveLayoutSource(root, North, 5)
        source_with = ExhaustiveLayoutSource(root, North, 5)

        without = evaluate(root, North, 1, source_without, declarer_play, defender_play)
        with_key = evaluate(
            root, North, 1, source_with, declarer_play, defender_play, state_key=state_key)

        self.assertEqual(without["by_strategy"][1]["p_make"], with_key["by_strategy"][1]["p_make"])


class TestCppStrategiesUnaffected(unittest.TestCase):
    def test_the_same_fixture_still_matches_the_c_plus_plus_hand_derived_answer(self) -> None:
        # The C++ test of this fixture uses single_card_declarer_play and
        # single_card_defender. The Python strategies must give the same
        # hand-derived answer.
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)
        result = evaluate(root, North, 1, source, declarer_play, defender_play)
        self.assertAlmostEqual(result["by_strategy"][1]["p_make"], 6.0 / 7.0, places=9)


class TestMalformedDefenderDistribution(unittest.TestCase):
    # A malformed distribution raises an exception. It neither crashes nor
    # passes. test_belief_space_local_evaluation_errors.py covers the full
    # exception hierarchy.
    def test_probabilities_not_summing_to_one_is_reported_not_crashed(self) -> None:
        def bad_defender(layout, seat, state):
            del state
            return [(lowest_card_in(layout["remain_cards"][seat]), 0.5)]

        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)

        with self.assertRaises(ProbabilitiesDoNotSumToOneError):
            evaluate(root, North, 1, source, declarer_play, bad_defender)

    def test_a_card_not_held_is_reported_not_crashed(self) -> None:
        def bad_defender(layout, seat, state):
            del layout, state
            # Hearts ace: nobody holds anything but spades in this fixture.
            return [(Card(Hearts, 14), 1.0)]

        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)

        with self.assertRaises(CardNotHeldError):
            evaluate(root, North, 1, source, declarer_play, bad_defender)


if __name__ == "__main__":
    unittest.main()
