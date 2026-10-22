"""Tests safety_score_declarer_play, a line-for-line port of
real_work_declarer_play in benchmarks/python_cost/strategies.hpp. The
Python-vs-C++ cost comparison uses it as a pi that does real work.

The cases are derived by hand, as in safety_score_declarer_play_test.cpp. A
slip gives a different score, not a false agreement. The worked comments in
the two files should match. test_belief_space_local_evaluation_parity.py
covers the binding's bitwise agreement with C++.
"""

import unittest

from belief_space_local_evaluation import Card

Spades = 0
North, East = 0, 1


def enumerate_legal_cards(deal: dict, seat: int) -> list[Card]:
    def collect(suit: int) -> list[Card]:
        holding = deal["remain_cards"][seat][suit]
        return [Card(suit, rank) for rank in range(2, 15) if holding & (1 << rank)]

    led = -1
    if deal["current_trick_rank"][0] != 0:
        led = deal["current_trick_suit"][0]
    if led != -1 and deal["remain_cards"][seat][led] != 0:
        return collect(led)
    cards: list[Card] = []
    for suit in range(4):
        cards.extend(collect(suit))
    return cards


def higher_defender_count(layout: dict, declarer: int, suit: int, rank: int) -> int:
    east = (declarer + 1) % 4
    west = (declarer + 3) % 4
    holding = layout["remain_cards"][east][suit] | layout["remain_cards"][west][suit]
    above_rank = holding & ~((1 << (rank + 1)) - 1)
    return bin(above_rank).count("1")


def seat_on_play(deal: dict) -> int:
    played = 0
    for rank in deal["current_trick_rank"]:
        if rank == 0:
            break
        played += 1
    return (deal["first"] + played) % 4


def safety_score_declarer_play(state, view) -> Card:
    known_holdings = state.known_holdings
    seat = seat_on_play(known_holdings)
    candidates = enumerate_legal_cards(known_holdings, seat)

    best = None
    best_score = 0.0
    for candidate in candidates:
        score = sum(
            entry.posterior * higher_defender_count(entry.layout, state.declarer, candidate.suit, candidate.rank)
            for entry in view.entries
        )
        if best is None or score < best_score or (score == best_score and candidate.rank < best.rank):
            best = candidate
            best_score = score
    assert best is not None
    return best


def empty_deal() -> dict:
    return {
        "trump": 4,  # DDS_NOTRUMP; irrelevant to this rule
        "first": North,
        "remain_cards": [[0, 0, 0, 0] for _ in range(4)],
        "current_trick_suit": (0, 0, 0),
        "current_trick_rank": (0, 0, 0),
    }


class FakeState:
    def __init__(self, declarer: int, known_holdings: dict) -> None:
        self.declarer = declarer
        self.known_holdings = known_holdings


class FakeEntry:
    # Stands in for BeliefEntry, which Python cannot construct: it only
    # comes from a live BeliefView. safety_score_declarer_play reads only
    # `.layout` and `.posterior`, so this runs the same code without an
    # evaluator.
    def __init__(self, layout: dict, posterior: float) -> None:
        self.layout = layout
        self.posterior = posterior


class FakeView:
    def __init__(self, entries: list[FakeEntry]) -> None:
        self.entries = entries


class TestSafetyScoreDeclarerPlay(unittest.TestCase):
    # North, on lead, holds the ♠A 5. In one of two equally likely
    # layouts East holds the ♠K. In the other neither defender holds a
    # spade.
    #
    #   score(Five) = 0.5 * 1 (East's King beats it) + 0.5 * 0 = 0.5
    #   score(Ace)  = 0.5 * 0 (nothing beats an Ace)  + 0.5 * 0 = 0.0
    #
    # The Ace's score is strictly lower.
    def test_plays_the_card_no_defender_layout_can_beat(self) -> None:
        known_holdings = empty_deal()
        known_holdings["remain_cards"][North][Spades] = (1 << 5) | (1 << 14)
        state = FakeState(North, known_holdings)

        layout_a = empty_deal()
        layout_a["remain_cards"][East][Spades] = 1 << 13
        layout_b = empty_deal()  # both defenders void in Spades

        view = FakeView([FakeEntry(layout_a, 0.5), FakeEntry(layout_b, 0.5)])

        played = safety_score_declarer_play(state, view)
        self.assertEqual((played.suit, played.rank), (Spades, 14))

    # North holds only the five, so there is no choice.
    def test_returns_the_only_legal_card_when_there_is_no_choice(self) -> None:
        known_holdings = empty_deal()
        known_holdings["remain_cards"][North][Spades] = 1 << 5
        state = FakeState(North, known_holdings)

        layout_a = empty_deal()
        layout_a["remain_cards"][East][Spades] = 1 << 13
        view = FakeView([FakeEntry(layout_a, 1.0)])

        played = safety_score_declarer_play(state, view)
        self.assertEqual((played.suit, played.rank), (Spades, 5))

    # An empty belief view scores every card 0. The tie goes to the lowest
    # rank, as in lowest_legal_card.
    def test_ties_break_by_lowest_rank(self) -> None:
        known_holdings = empty_deal()
        known_holdings["remain_cards"][North][Spades] = (1 << 5) | (1 << 14)
        state = FakeState(North, known_holdings)

        view = FakeView([])

        played = safety_score_declarer_play(state, view)
        self.assertEqual((played.suit, played.rank), (Spades, 5))


if __name__ == "__main__":
    unittest.main()
