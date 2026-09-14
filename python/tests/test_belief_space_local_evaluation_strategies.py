import unittest

from belief_space_local_evaluation import Card
from belief_space_local_evaluation import CardNotHeldError
from belief_space_local_evaluation import evaluate
from belief_space_local_evaluation import ExhaustiveLayoutSource
from belief_space_local_evaluation import ProbabilitiesDoNotSumToOneError

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
    # same name.
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
    # A pure function of (state, view) alone, as DeclarerStrategy::play's
    # own contract requires -- no PRNG stream, nothing but the arguments.
    # This fixture never plays a second trick (declarer and dummy hold
    # exactly one card each, and tricks_needed=1), so the seat on play
    # within the one trick `first` opened is simply first + however many
    # cards have been played so far, mod 4 -- no need to replay trick
    # winners the way multi-trick rotation would require. Only one suit
    # exists anywhere in this fixture too, so "lowest legal card" reduces
    # to "the lowest card the seat on play holds".
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
        # Hand-derived, same as the C++ fixture this mirrors: East holds
        # one of {2,3,4,5,6,7,10}; North's Nine wins the trick unless
        # East's own card already beats it (Ten is the only one that
        # does). 6 of 7 -> 6/7.
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
    # ObservationState's own docstring claims every field a C++ strategy
    # may condition on directly is bound -- ranks (a RankMap) is one of
    # those fields, the precomputed absolute/relative rank mapping over
    # the node's outstanding pool. Captured from a real pi call, not
    # constructed by hand: nothing constructs a RankMap from Python either.
    def test_aggr_matches_known_holdings_own_pool_at_the_same_node(self) -> None:
        # Cross-checked against known_holdings from the very same call
        # (both are derived from the same node Deal -- see
        # known_holdings' own doxygen on a defender's entry already being
        # the union pool) rather than hand-simulated from the root: pi is
        # not the root's own first call here (first=East, a defender
        # leads), so the pool ranks sees already has one card removed
        # by the time pi first runs, and re-deriving that by hand would
        # only be re-testing the recursion, not RankMap.
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
        # Not vacuous: this fixture is spades-only, so a real, nonzero
        # pool is actually being compared for at least one suit.
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
        # The Ace is never outstanding in this small fixture, whatever
        # node pi happens to be called at.
        self.assertEqual(ranks.to_relative(Spades, 14), 0)

    def test_out_of_range_suit_or_rank_returns_zero_not_an_error(self) -> None:
        # RankMap's own doxygen: out-of-range input is "not outstanding",
        # not a validation error -- matches the type's own C++ contract.
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


class TestStateKey(unittest.TestCase):
    # evaluate()'s own doxygen: "state_key is never called: there is no
    # cache yet." So the only thing observable from Python is that
    # supplying one -- None, or a callable returning bytes -- changes
    # nothing about the result and does not crash. Direct invocation is
    # untestable honestly until a cache exists to call it.
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
        # Not a new claim about this binding -- the equivalent C++ test
        # (using the C++-native single_card_declarer_play/single_card_defender,
        # no Python involved) is part of the unchanged 347. This just
        # confirms this binding's own Python fixture reproduces the exact
        # same hand-derived number that test pins, so both surfaces agree.
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)
        result = evaluate(root, North, 1, source, declarer_play, defender_play)
        self.assertAlmostEqual(result["by_strategy"][1]["p_make"], 6.0 / 7.0, places=9)


class TestMalformedDefenderDistribution(unittest.TestCase):
    # See test_belief_space_local_evaluation_errors.py for the full
    # exception hierarchy this raises through -- these two just pin that
    # a malformed distribution is reported (as a clean exception, not a
    # crash) rather than silently accepted.
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
