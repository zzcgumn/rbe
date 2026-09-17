"""The Python side of this capability's own Python-vs-C++ per-callback cost
comparison -- timer.cpp's own header comment explains the shape (two pi,
two sampling configurations) and why. Loops the identical evaluate() call
through the compiled binding and times it.

A py_binary (built, not run automatically -- py_binary is not a test, so
`bazel test //...` never executes it), not a plain script: unlike
benchmarks/belief_evaluation's own sweep scripts, this needs the compiled
belief_space_local_evaluation extension itself, which only bazel's own
py_binary/py_test wiring builds and places on sys.path correctly (see
BUILD.bazel's own comment on this file's target).

Usage (normally driven by report.py, not run standalone):

    bazel run //benchmarks/python_cost:timer_py -- [ITERATIONS]
"""

from __future__ import annotations

import sys
import time

from belief_space_local_evaluation import Card
from belief_space_local_evaluation import ExhaustiveLayoutSource
from belief_space_local_evaluation import evaluate

Spades = 0
North, East, South, West = 0, 1, 2, 3
DDS_NOTRUMP = 4


def make_two_card_finesse_root() -> dict:
    # Identical to parity_reference.cpp's own make_two_card_finesse_root
    # and timer.cpp's own copy -- the fourth independent copy of this tiny
    # fixture in the tree (parity_reference.cpp,
    # exhaustive_layout_source_integration_test.cpp, timer.cpp, this one),
    # matching those files' own established precedent of duplicating a
    # fixture this small rather than sharing it across a package boundary.
    remain_cards = [[0, 0, 0, 0] for _ in range(4)]
    remain_cards[North][Spades] = 1 << 9
    remain_cards[South][Spades] = 1 << 8
    remain_cards[East][Spades] = (1 << 2) | (1 << 3)
    remain_cards[West][Spades] = (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10)
    return {
        "trump": DDS_NOTRUMP,
        "first": East,
        "remain_cards": remain_cards,
        "current_trick_suit": (0, 0, 0),
        "current_trick_rank": (0, 0, 0),
    }


def seat_on_play(deal: dict) -> int:
    played = 0
    for rank in deal["current_trick_rank"]:
        if rank == 0:
            break
        played += 1
    return (deal["first"] + played) % 4


def lowest_card_in(remain_cards_row) -> Card:
    for suit in range(4):
        mask = remain_cards_row[suit]
        if mask:
            rank = 2
            while not (mask & (1 << rank)):
                rank += 1
            return Card(suit, rank)
    raise AssertionError("seat holds nothing")


def lowest_legal_card(deal: dict, seat: int) -> Card:
    remain_cards = deal["remain_cards"][seat]
    if deal["current_trick_rank"][0] != 0:
        led = deal["current_trick_suit"][0]
        if remain_cards[led] != 0:
            return lowest_card_in([remain_cards[led] if s == led else 0 for s in range(4)])
    return lowest_card_in(remain_cards)


def trivial_declarer_play(state, view):
    del view
    return lowest_legal_card(state.known_holdings, seat_on_play(state.known_holdings))


def trivial_defender_play(layout, seat, state):
    del state
    return [(lowest_legal_card(layout, seat), 1.0)]


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


def real_work_declarer_play(state, view):
    # Identical rule to test_support.hpp's safety_score_declarer_play and
    # timer.cpp's own copy -- proven correct (both languages, independently
    # hand-derived) in safety_score_declarer_play_test.cpp and
    # test_belief_space_local_evaluation_python_cost.py; not re-proven
    # here, only timed.
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


def run_config(pi_label: str, pi, config_label: str, kwargs: dict, iterations: int) -> None:
    root = make_two_card_finesse_root()
    seed = 7  # timer.cpp's own seed, parity_reference.cpp's SAMPLED_SEED

    start = time.perf_counter()
    for _ in range(iterations):
        source = ExhaustiveLayoutSource(root, North, seed)
        result = evaluate(root, North, 1, source, pi, trivial_defender_play, **kwargs)
        if "error" not in result:
            _ = result["by_strategy"][1]["p_make"]  # touch the result, nothing more
    total_ms = (time.perf_counter() - start) * 1000.0
    print(
        f"py pi={pi_label:<9s} config={config_label:<16s} iterations={iterations} "
        f"ms_per_iteration={total_ms / iterations:.6f}")


def run(iterations: int) -> None:
    # Same root/declarer/tricks_needed/seed/sample_size/scan_budget
    # throughout -- only replenish_below differs -- matching timer.cpp's
    # own pairing exactly, for the same reason (this task's own
    # background: isolate the node-local replenishment scan's own effect
    # on how often delta is called).
    non_replenishing = {"collect_counters": False, "sample_size": 10, "scan_budget": 100}
    replenishing = dict(non_replenishing, replenish_below=5)

    for config_label, kwargs in (("non_replenishing", non_replenishing), ("replenishing", replenishing)):
        run_config("trivial", trivial_declarer_play, config_label, kwargs, iterations)
        run_config("real_work", real_work_declarer_play, config_label, kwargs, iterations)


if __name__ == "__main__":
    run(int(sys.argv[1]) if len(sys.argv) > 1 else 2000)
