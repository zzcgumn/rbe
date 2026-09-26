"""Guess 6NT: a slam that comes down to locating one queen.

    North   S KJ7    H QJ     D QT65   C AJ42
    South   S T9     H AKT    D AK98   C KQ73

South plays 6NT on a club lead. Nine tricks cash themselves -- four clubs,
four diamonds, one heart -- and then declarer is in the four-card ending

    North   S KJ7    H Q
    South   S T9     H AT

needing three more. Two are hearts: North's queen crashes under the ace, but
the defenders hold only H 92, so the ten is good as well. The third has to
come from spades, against East and West holding S AQ6542 and H 92 between
them -- six spades and two hearts, the eight cards the 70 layouts split --
and which defender holds the spade queen is not known. That is the whole
problem, and it is exactly the kind of problem belief-space evaluation is
for: the answer is not a card, it is a probability over the layouts still
consistent with the play so far.

This example gets to that ending, builds the belief space, and evaluates
2 declarers against 3 defences over it -- six evaluations, printed as a grid.

The declarers are the **fixed line** (cash the heart ace, then the ten, then a
spade, covering the queen if it appears) and the **belief finesse** (the same,
except that the spade guess is read off the belief space instead of decided in
advance). The defences are **low**, **double dummy**, and **cover when it
wins**. `DECLARERS` and `defences_with()` below are the single source of both
the grid and the counts in this paragraph, and a test asserts they agree,
because this paragraph has twice drifted from the code underneath it.

The finding is that **neither declarer dominates**. Reading the belief space
beats the fixed line against two of the three defences and loses to it
against the third, so "best line" is not defined independently of the defence
assumed -- which is the question belief-space evaluation exists to ask, and
the reason a number is the answer rather than a card.

**Scope.** One of the two declarers reads the BeliefView; that is deliberate
and is the smallest thing that makes the library's point visible. The more
elaborate view-reading examples -- conditioning on a sampled space, a
strategy that updates across tricks -- are deliberately not here. They wait
on the API changes this example was written to find, which are listed under
"Known gaps" in docs/belief_space_local_evaluation.md.

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
from belief_space_local_evaluation import legal_cards, seat_on_play

from play_sequence import PlaySequence, cards_on_trick
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
    """pi: cash the heart ace, then the heart ten, then play a spade,
    covering the queen if it appears.

    Chosen from `legal_cards`, which has already applied the follow-suit
    rule, so every branch below is a preference among cards that are legal
    rather than a claim that the preferred card is available. Falling
    through to "lowest" is therefore always safe.

    The same function plays for declarer and for dummy -- pi is called for
    whichever of the two is on play -- so it is written in terms of what is
    on the trick, not in terms of the seat.
    """
    del view  # This line is fixed in advance; it does not read the beliefs.
    deal = state.known_holdings
    seat = seat_on_play(deal)
    legal = legal_cards(deal, seat)
    on_trick = cards_on_trick(deal)

    # An empty trick means *we* are leading. Not the same test as
    # current_trick_suit[0] == SPADES, which is also true of an empty trick,
    # spades being suit 0 -- see cards_on_trick's own docstring.
    if not on_trick:
        return (_pick(legal, HEARTS, ACE)
                or _pick(legal, HEARTS, TEN)
                or min(legal, key=lambda card: (card.rank, card.suit)))

    # Following. Cover the queen: if the spade queen is already on this
    # trick and the king is still ours to play, put it up.
    if Card(SPADES, QUEEN) in on_trick:
        king = _pick(legal, SPADES, KING)
        if king is not None:
            return king

    return min(legal, key=lambda card: (card.rank, card.suit))


# The grid's two axes, at module scope so that this module's own docstring can
# be checked against them (see test_the_docstring_matches_the_grid) rather than
# drifting from them, which it has done twice.
DECLARERS = (
    ("fixed line", cash_two_hearts_and_play_a_spade),
    # The only strategy here that reads the BeliefView -- see the docstring's
    # own Scope note for why exactly one does.
    ("belief finesse", finesse_the_queen_from_the_beliefs),
)

DEFENCE_NAMES = ("low", "double dummy", "cover when it wins")


def defences_with(ctx):
    """The three defences. Takes a SolverContext because one of them solves.

    `DoubleDummyDefender` maximises *tricks*, not the contract, and assumes
    declarer plays double dummy from here -- which neither declarer above
    does. Both are why it is not best defence here.

    It also does **not** spread over every tied-for-best card. Under the
    default `SpreadPolicy.TouchingSequence` it takes the canonical best card
    -- highest score, ties broken by dds's own ordering -- and that one card's
    touching group, discarding the other tied entries. Measured at one node
    here, `solutions = 2` returns three cards tied at the maximum and the
    defender plays one of them with probability 1.0. So the "double dummy"
    column is partly determined by dds's ordering among non-touching equals,
    not by anything about double-dummy defence. `SpreadPolicy.AllOptimal` is
    the policy that spreads over all of them.

    `queen_of_spades_when_it_wins` is optimal against the fixed line
    (exhaustive minimax over every defensive choice lets that line through in
    exactly the same layouts) and not against the belief finesse.
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

    # Printed the way declarer sees it, not the way the dealer dealt it:
    # declarer's and dummy's cards exactly, and the defenders' outstanding
    # cards as one pool. That is precisely what `ObservationState` gives pi --
    # both defender entries there hold this same union, never one hand -- and
    # printing the real split here would undercut the whole point one line
    # after claiming the queen's location is unknown.
    # Computed as a union, not read off a seat. `root` is a real layout, so
    # each defender entry there is that defender's own hand -- the pooled
    # entry only exists in `ObservationState.known_holdings`. Reading one
    # seat here would silently print half the pool, and look plausible.
    lho, rho = (declarer + 1) % 4, (declarer + 3) % 4
    pool = [root["remain_cards"][lho][s] | root["remain_cards"][rho][s] for s in range(4)]
    for seat, label in ((dummy, "dummy"), (declarer, "declarer")):
        print(f"{SEAT_NAMES[seat]:>5} ({label:<8}) {format_hand(root['remain_cards'][seat])}")
    print(f"{'E/W':>5} ({'pool':<8}) {format_hand(pool)}")
    print(f"\n{SEAT_NAMES[root['first']]} to play. Which defender holds the "
          f"{format_card(Card(SPADES, QUEEN))} is what the belief space is over.\n")

    # The play history is what makes this the *right* belief space rather than
    # merely a plausible one: it rules out the layouts in which a defender
    # holds a suit they have already shown out of. Omitting it is not an error
    # and raises nothing -- it silently answers a different question. See "The
    # play history: needed, not merely optional" in
    # docs/belief_space_local_evaluation.md.
    source = bsle.ExhaustiveLayoutSource(
        root, declarer, SEED,
        history=sequence.history, opening_leader=sequence.opening_leader)
    unconstrained = bsle.ExhaustiveLayoutSource(root, declarer, SEED)

    print(f"Belief space, with history: {source.size()} layouts")
    print(f"       ... without history: {unconstrained.size()} layouts")
    if source.size() == unconstrained.size():
        # True here, and worth printing rather than quietly agreeing: both
        # defenders showed out of clubs and diamonds, but no club or diamond
        # is left to hold, so on this root the history rules out nothing the
        # root did not already. That is a property of this ending, not a
        # reason to stop passing the history -- move the root one trick
        # earlier and the two numbers part company.
        print("       (equal here: every suit shown out of is already exhausted)")
    print()

    # Every pair over the same belief space. One axis at a time is what makes
    # any of it readable.
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

    # value["root_is_declaring_side"] says which shape these have; it is True
    # here, so they are *alternatives*, not a partition. Each is what the
    # contract is worth if declarer plays that card now and follows the same pi
    # afterwards -- whichever pi was passed, not a lowest-card rule. They do
    # not sum to P_make, and P_make is whichever one pi chose.
    assert value["root_is_declaring_side"], "a defender root would partition"
    for card, p_make in sorted(value["root_children"], key=lambda entry: -entry[1]):
        print(f"    lead {format_card(card)}   {p_make:.4f}")
    print()


if __name__ == "__main__":
    main()
