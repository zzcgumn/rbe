"""Guess 6NT: a slam that comes down to locating one queen.

    North   S KJ7    H QJ     D QT65   C AJ42
    South   S T9     H AKT    D AK98   C KQ73

South plays 6NT on a club lead. After nine tricks (four clubs, four
diamonds, one heart), this four-card ending is left:

    North   S KJ7    H Q
    South   S T9     H AT

Declarer needs three more tricks. Two come from hearts: the queen falls
under the ace, and the ten is good because the defenders hold only H 92.
The third must come from spades. East and West hold S AQ6542 and H 92,
split over 70 layouts. Nobody knows which defender holds the spade queen.

**The question:** which line gives the best chance to make, and against
which defence? The answer is a probability, not a card.

**The example:**

1. Play the nine tricks to reach the ending.
2. Build the belief space from the play history.
3. Evaluate 2 declarers against 3 defences: six values, printed as a grid.

The declarers:

- **fixed line:** cash the heart ace, then the ten, then play a spade.
  Cover the queen if it appears.
- **belief finesse:** the same, but read the spade guess from the belief
  space. This is the only strategy here that reads the `BeliefView`.

The defences are **low**, **double dummy** and **cover when it wins**.

**The result:** neither declarer dominates. The belief finesse beats the
fixed line against two defences and loses against the third. So the best
line depends on the defence you assume.

`DECLARERS` and `defences_with()` define the grid. A test checks that this
docstring matches them.

Run it with:

    bazelisk run //examples:guess_6nt_belief_space
"""

import dds3

import belief_space_local_evaluation as bsle
from belief_space_local_evaluation import Card

from bridge_notation import (
    HEARTS,
    NOTRUMP,
    SEAT_NAMES,
    SOUTH,
    SPADES,
    format_card,
    format_denomination,
    format_hand,
    parse_deal,
)
from belief_space_local_evaluation import legal_cards, seat_on_play, PlaySequence

from belief_space_local_evaluation.play_sequence import cards_on_trick
from strategies import (
    ACE,
    KING,
    QUEEN,
    TEN,
    double_dummy_defender,
    finesse_the_queen_from_the_beliefs,
    lowest_eligible_defender,
    queen_of_spades_when_it_wins,
    pick as _pick,
)

SEED = 1


def guess_6nt() -> PlaySequence:
    """The deal, and the nine tricks that lead to the ending."""
    deal = parse_deal(
        "N: KJ7.QJ.QT65.AJ42 A653.86432.J2.T9 T9.AKT.AK98.KQ73 Q842.975.743.865")
    sequence = PlaySequence(deal, declarer=SOUTH, trump=NOTRUMP, level=6)

    sequence.play_trick("C6 C4 C9 CQ")  # West leads a club, won by South.
    sequence.play_trick("CK C8 C2 CT")  # South cashes the club king.
    sequence.play_trick("C7 C5 CA H8")  # Club to dummy's ace; East pitches a heart.
    sequence.play_trick("CJ S3 C3 S8")  # Dummy's club jack; both defenders pitch spades.
    sequence.play_trick("D6 D2 DK D3")  # Diamond to the king.
    sequence.play_trick("DA D4 D5 DJ")  # Cash the diamond ace.
    sequence.play_trick("D9 D7 DQ H4")  # Diamond to the queen; East pitches a heart.
    sequence.play_trick("DT H3 D8 H5")  # The last diamond; both defenders pitch hearts.
    sequence.play_trick("HJ H6 HK H7")  # A heart to the king, and South is on play.

    return sequence


def cash_two_hearts_and_play_a_spade(state, view):
    """pi: cash the heart ace, then the heart ten, then play a spade.
    Cover the queen if it appears.

    Every choice comes from `legal_cards`, so it is always legal. The
    fallback, the lowest card, is always safe.

    pi plays for both declarer and dummy. So this function looks at the
    cards on the trick, not at the seat.
    """
    del view  # This line is fixed in advance; it does not read the beliefs.
    deal = state.known_holdings
    seat = seat_on_play(deal)
    legal = legal_cards(deal, seat)
    on_trick = cards_on_trick(deal)

    # An empty trick means we are leading. Do not test
    # current_trick_suit[0] == SPADES: spades is suit 0, so an empty trick
    # also passes that test.
    if not on_trick:
        return (_pick(legal, HEARTS, ACE)
                or _pick(legal, HEARTS, TEN)
                or min(legal, key=lambda card: (card.rank, card.suit)))

    # Following. If the spade queen is on the trick, cover with the king.
    if Card(SPADES, QUEEN) in on_trick:
        king = _pick(legal, SPADES, KING)
        if king is not None:
            return king

    return min(legal, key=lambda card: (card.rank, card.suit))


# The two axes of the grid. They are at module scope so that
# test_the_docstring_matches_the_grid can check the docstring against them.
DECLARERS = (
    ("fixed line", cash_two_hearts_and_play_a_spade),
    # The only strategy here that reads the BeliefView.
    ("belief finesse", finesse_the_queen_from_the_beliefs),
)

DEFENCE_NAMES = ("low", "double dummy", "cover when it wins")


def defences_with(ctx):
    """The three defences. Takes a SolverContext, because one of them solves.

    `DoubleDummyDefender` is not best defence here, for two reasons:

    - It maximises tricks, not the contract.
    - It assumes declarer plays double dummy. Neither declarer here does.

    It also does not spread over every card tied for best. With the default
    `SpreadPolicy.TouchingSequence`, it takes the single best card and that
    card's touching sequence. dds breaks ties between equal scores. At one
    node here, `solutions = 2` returns three cards tied for best, and the
    defender plays one of them with probability 1.0. So the "double dummy"
    column partly reflects dds's tie order. `SpreadPolicy.AllOptimal`
    spreads over all tied cards.

    `queen_of_spades_when_it_wins` is optimal against the fixed line. A full
    minimax over every defensive choice lets that line make in exactly the
    same layouts. It is not optimal against the belief finesse.
    """
    return (
        ("low", lowest_eligible_defender),
        ("double dummy", double_dummy_defender(ctx)),
        ("cover when it wins", queen_of_spades_when_it_wins),
    )


def main() -> None:
    sequence = guess_6nt()
    root = sequence.current_deal
    declarer, dummy = sequence.declarer, (sequence.declarer + 2) % 4

    print(f"Contract: {sequence.level}{format_denomination(root['trump'])} "
          f"by {SEAT_NAMES[declarer]}\n")
    print(sequence.format_tricks())
    print(f"\nDeclarer has {sequence.tricks_won_by_declarer} tricks and needs "
          f"{sequence.tricks_needed} more from:\n")

    # Print what declarer sees: declarer's and dummy's cards, and the
    # defenders' cards as one pool. This matches what `ObservationState`
    # gives pi. Printing the real split would reveal the queen.
    # `root` is a real layout, so each defender entry is one real hand.
    # Compute the union. Reading one seat would print half the pool.
    lho, rho = (declarer + 1) % 4, (declarer + 3) % 4
    pool = [root["remain_cards"][lho][s] | root["remain_cards"][rho][s] for s in range(4)]
    for seat, label in ((dummy, "dummy"), (declarer, "declarer")):
        print(f"{SEAT_NAMES[seat]:>5} ({label:<8}) {format_hand(root['remain_cards'][seat])}")
    print(f"{'E/W':>5} ({'pool':<8}) {format_hand(pool)}")
    print(f"\n{SEAT_NAMES[root['first']]} to play. Which defender holds the "
          f"{format_card(Card(SPADES, QUEEN))} is what the belief space is over.\n")

    # The play history rules out layouts in which a defender holds a suit
    # they have shown out of. Without it, nothing raises, but the answer is
    # for a different question. See "Supply the play history" in
    # docs/belief_space_local_evaluation.md.
    record = bsle.PlayRecord(sequence.history, sequence.opening_leader)
    source = bsle.ExhaustiveLayoutSource(root, declarer, SEED, record=record)
    unconstrained = bsle.ExhaustiveLayoutSource(root, declarer, SEED)

    print(f"Belief space, with history: {source.size()} layouts")
    print(f"       ... without history: {unconstrained.size()} layouts")
    if source.size() == unconstrained.size():
        # True here. Both defenders showed out of clubs and diamonds, but no
        # clubs or diamonds are left. So the history rules out nothing new.
        # This is a property of this ending only. One trick earlier, the two
        # sizes differ.
        print("       (equal here: every suit shown out of is already exhausted)")
    print()

    # Evaluate every pair over the same belief space.
    ctx = dds3.SolverContext()
    declarers, defences = DECLARERS, defences_with(ctx)

    grid = {}
    for pi_name, pi in declarers:
        for delta_name, delta in defences:
            grid[(pi_name, delta_name)] = evaluate(sequence, source, pi, delta)["p_make"]

    width = max(len(n) for n, _ in defences) + 2
    print("P_make, declarer down the side, defence across:\n")
    print(" " * 16 + "".join(f"{n:>{width}}" for n, _ in defences))
    for pi_name, _ in declarers:
        row = "".join(f"{grid[(pi_name, d)]:>{width}.4f}" for d, _ in defences)
        print(f"{pi_name:<16}{row}")

    print("\nNeither declarer dominates, and that is the result worth having:")
    print("  - reading the beliefs beats the fixed line against two defences...")
    print("  - ...and loses to it against double dummy, which is defending")
    print("    against a declarer that plays double dummy -- neither of these does.")
    print("\nSo there is no single best line here independent of the defence")
    print("assumed, which is the question belief-space evaluation exists to ask.")

    report_root_children(
        "belief finesse vs cover when it wins",
        evaluate(sequence, source, finesse_the_queen_from_the_beliefs,
                 queen_of_spades_when_it_wins))


def evaluate(sequence, source, pi, delta, **options) -> dict:
    """Run one evaluation and hand back the value, failing loudly."""
    result = bsle.evaluate(
        sequence.current_deal, sequence.declarer, sequence.tricks_needed,
        source, pi, delta, **options)
    if "error" in result:
        raise SystemExit(f"evaluation failed: {result['error']}")
    return result["by_strategy"][1]  # Keyed by pi's strategy id, always 1 here.


def report_root_children(heading: str, value: dict) -> None:
    print(f"{heading}: P_make = {value['p_make']:.4f}")

    # root_is_declaring_side is True here, so the entries are alternatives,
    # not a partition. Each is P_make if declarer plays that card now and
    # then follows the same pi. They do not sum to P_make. P_make is the
    # entry that pi chose.
    assert value["root_is_declaring_side"], "a defender root would partition"
    for card, p_make in sorted(value["root_children"], key=lambda entry: -entry[1]):
        print(f"    lead {format_card(card)}   {p_make:.4f}")
    print()


if __name__ == "__main__":
    main()
