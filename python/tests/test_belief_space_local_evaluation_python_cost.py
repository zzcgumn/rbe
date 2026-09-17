"""Correctness for `real_work_declarer_play` -- the "real Python-side
work" pi the Python-vs-C++ cost comparison
(benchmarks/python_cost/) needs, ported line-for-line from
benchmarks/python_cost/strategies.hpp's own C++ version (see its own
doxygen for what the rule computes and why).

Hand-derived case, worked independently rather than read from a shared
generator -- the same choice safety_score_declarer_play_test.cpp's own
header comment explains (oracle_test.cpp's own precedent): a
transcription slip here shows up as *some other score*, not as agreement
with the wrong one, so a small enough case is safe to hand-derive on both
sides separately. The two files' own worked comments should read
identically; if they ever diverge, that divergence is the bug to chase.

Only the scoring rule is proven here. That the Python binding reproduces
a C++ evaluate() run bitwise, on this same shape of fixture, is already
proven by test_belief_space_local_evaluation_parity.py -- not re-proven
here (this plan's own "read, do not re-derive").
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
    # Duck-typed stand-in for the real (pybind11-bound) BeliefEntry, which
    # has no Python-visible constructor -- it only ever comes from a live
    # BeliefView the evaluator built, referencing evaluator-owned memory
    # (belief_space_local_evaluation.cpp's own PyBeliefEntry). safety_score
    # _declarer_play only ever reads `.layout`/`.posterior` off an entry, so
    # a plain object with those two attributes exercises the identical code
    # path without needing a real evaluator run.
    def __init__(self, layout: dict, posterior: float) -> None:
        self.layout = layout
        self.posterior = posterior


class FakeView:
    def __init__(self, entries: list[FakeEntry]) -> None:
        self.entries = entries


class TestSafetyScoreDeclarerPlay(unittest.TestCase):
    # North holds the Five and the Ace of Spades, on lead -- both legal.
    # Two equally likely layouts: East holds the King in one (beats the
    # Five, not the Ace), neither defender holds anything in Spades in the
    # other.
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

    # Same two layouts, but North holds only the Five -- no choice to make.
    def test_returns_the_only_legal_card_when_there_is_no_choice(self) -> None:
        known_holdings = empty_deal()
        known_holdings["remain_cards"][North][Spades] = 1 << 5
        state = FakeState(North, known_holdings)

        layout_a = empty_deal()
        layout_a["remain_cards"][East][Spades] = 1 << 13
        view = FakeView([FakeEntry(layout_a, 1.0)])

        played = safety_score_declarer_play(state, view)
        self.assertEqual((played.suit, played.rank), (Spades, 5))

    # An empty belief view scores every candidate 0 -- the tie breaks by
    # lowest rank, the same deterministic rule lowest_legal_card uses.
    def test_ties_break_by_lowest_rank(self) -> None:
        known_holdings = empty_deal()
        known_holdings["remain_cards"][North][Spades] = (1 << 5) | (1 << 14)
        state = FakeState(North, known_holdings)

        view = FakeView([])

        played = safety_score_declarer_play(state, view)
        self.assertEqual((played.suit, played.rank), (Spades, 5))


if __name__ == "__main__":
    unittest.main()
