"""Playing a hand out to the position you want to evaluate.

`evaluate()` evaluates one root: a position, a declarer, and the tricks
still needed. It does not play cards or count tricks. To reach a mid-play
root, you play the earlier tricks yourself. This module does that.

It also records the **play history**. Pass it to `ExhaustiveLayoutSource` to
rule out layouts in which a defender holds a suit they have shown out of.
Without it, nothing fails, but the answer is for a different question. See
"Supply the play history" in docs/belief_space_local_evaluation.md.

The trick rules come from the library: `bsle.seat_on_play`,
`bsle.legal_cards`, `bsle.play` and `bsle.trick_complete_winner`. These are
the rules the evaluator uses. This module adds only what the library does not
do: count tricks, record the history, and parse hand records.
"""

import dataclasses

import belief_space_local_evaluation as bsle

from bridge_notation import (
    SEAT_NAMES,
    format_card,
    parse_cards,
)


@dataclasses.dataclass(frozen=True)
class DeclarerView:
    """What declarer knows at this point in a PlaySequence. Use it to
    inspect a position outside evaluate().

    It has the same shape as ObservationState, which Python cannot
    construct.

    play_history is the full record from trick one. It has a different name
    from ObservationState.history, which starts at the root.
    """
    declarer_hand: list
    dummy_hand: list
    seat_on_play: int
    trick_leader: int
    current_trick: list
    position_in_trick: int
    legal_cards: list
    can_follow_led_suit: bool
    is_declaring_side: bool
    play_history: list
    tricks_won_by_declarer: int
    tricks_needed: int


def _hand(remain_cards_row: list) -> list:
    """A seat's remaining cards as a list of Card, sorted by suit, then rank,
    ascending. This matches the order of `legal_cards`, which is a subset of
    one of these hands. (`bridge_notation.ranks_in` sorts highest first.)"""
    return [
        bsle.Card(suit, rank)
        for suit in range(4)
        for rank in range(2, 15)
        if remain_cards_row[suit] & (1 << rank)
    ]


def cards_on_trick(deal: dict) -> list:
    """The cards already played to the trick in progress, in order. Empty
    when the seat on play is leading.

    These cards are in no `remain_cards`: a played card leaves the hand at
    once.
    """
    return [
        bsle.Card(suit, rank)
        for suit, rank in zip(deal["current_trick_suit"], deal["current_trick_rank"])
        if rank != 0
    ]


def suit_led(deal: dict) -> int:
    """The suit led to the trick in progress, or -1 if nobody has led yet."""
    if deal["current_trick_rank"][0] == 0:
        return -1
    return deal["current_trick_suit"][0]


def play_card(deal: dict, card) -> dict:
    """The deal after the seat on play plays `card`, via `bsle.play`.

    Adds a check: `bsle.play` does not check legality, so a hand record with
    a card in the wrong place would give a nonsense position. This raises
    ValueError naming the card.
    """
    seat = bsle.seat_on_play(deal)
    if deal["remain_cards"][seat][card.suit] & (1 << card.rank) == 0:
        raise ValueError(
            f"{SEAT_NAMES[seat]} does not hold {format_card(card, symbols=False)}")
    if card not in bsle.legal_cards(deal, seat):
        raise ValueError(
            f"{SEAT_NAMES[seat]} must follow suit and cannot play "
            f"{format_card(card, symbols=False)}")

    return bsle.play(deal, card)


class PlaySequence:
    """A deal, a contract, and the cards played so far.

    Play tricks onto it. Then read the inputs for `evaluate()`:
    `current_deal`, `tricks_needed`, and the `history` and `opening_leader`
    that `ExhaustiveLayoutSource` needs.
    """

    def __init__(self, deal: dict, declarer: int, trump: int, level: int):
        self.declarer = declarer
        self.level = level
        self.opening_leader = (declarer + 1) % 4
        self.history = []
        self.completed_tricks = []
        self._initial_deal = dict(deal, trump=trump, first=self.opening_leader)
        self.current_deal = self._initial_deal

    @property
    def dummy(self) -> int:
        return (self.declarer + 2) % 4

    @property
    def tricks_to_make(self) -> int:
        """What the contract needs in total: six plus the level."""
        return self.level + 6

    @property
    def tricks_won_by_declarer(self) -> int:
        """Tricks won by declarer's side so far, dummy's included.

        Computed by `bsle.play_out`, not by a counter here. An off-by-one would
        not fail: it would evaluate a different contract.
        """
        _root, won = bsle.play_out(
            self._initial_deal, self.history, self.opening_leader, self.declarer)
        return won

    @property
    def tricks_needed(self) -> int:
        """The tricks still needed: the third argument of `evaluate()`. It
        shrinks as declarer wins tricks."""
        return self.tricks_to_make - self.tricks_won_by_declarer

    def play(self, *cards) -> "PlaySequence":
        """Play cards in order, from the seat on play. Accepts Cards, one
        string per card, or one string for all of them."""
        for card in cards:
            for one in (parse_cards(card) if isinstance(card, str) else [card]):
                self._play_one(one)
        return self

    def _play_one(self, card) -> None:
        leader = self.current_deal["first"]
        before = sum(1 for rank in self.current_deal["current_trick_rank"] if rank != 0)
        self.current_deal = play_card(self.current_deal, card)
        self.history.append(card)
        if before == 3:
            # For `format_tricks` only. This does not count tricks:
            # `tricks_won_by_declarer` does.
            self.completed_tricks.append((leader, self.history[-4:], self.current_deal["first"]))

    def play_trick(self, cards) -> "PlaySequence":
        """Play four cards, and check that they are a whole trick. Prefer it
        to `play` when you transcribe a hand record. Otherwise a missing card
        makes every later trick start from the wrong seat."""
        cards = parse_cards(cards) if isinstance(cards, str) else list(cards)
        if len(cards) != 4:
            raise ValueError(f"a trick is 4 cards, got {len(cards)}")
        if any(rank != 0 for rank in self.current_deal["current_trick_rank"]):
            raise ValueError("a trick is already in progress")
        return self.play(*cards)

    def format_tricks(self, symbols: bool = True) -> str:
        lines = []
        for number, (leader, cards, winner) in enumerate(self.completed_tricks, start=1):
            played = " ".join(format_card(card, symbols) for card in cards)
            lines.append(
                f"{number:>3}. {SEAT_NAMES[leader]:>5} led  {played}   "
                f"won by {SEAT_NAMES[winner]}")
        return "\n".join(lines)

    def declarer_view(self) -> DeclarerView:
        """What declarer knows now, as a DeclarerView.

        Raises ValueError if a defender is on play. The legal cards would come
        from a defender's hand, which declarer cannot see."""
        seat = bsle.seat_on_play(self.current_deal)
        if seat not in (self.declarer, self.dummy):
            raise ValueError(
                f"{SEAT_NAMES[seat]} does not have the turn to play; "
                "declarer_view() is only meaningful when it is declarer's "
                "or dummy's turn")

        remain_cards = self.current_deal["remain_cards"]
        current_trick = cards_on_trick(self.current_deal)
        led_suit = self.current_deal["current_trick_suit"][0]
        can_follow_led_suit = bool(current_trick) and remain_cards[seat][led_suit] != 0

        return DeclarerView(
            declarer_hand=_hand(remain_cards[self.declarer]),
            dummy_hand=_hand(remain_cards[self.dummy]),
            seat_on_play=seat,
            trick_leader=self.current_deal["first"],
            current_trick=current_trick,
            position_in_trick=len(current_trick),
            legal_cards=bsle.legal_cards(self.current_deal, seat),
            can_follow_led_suit=can_follow_led_suit,
            is_declaring_side=True,
            play_history=list(self.history),
            tricks_won_by_declarer=self.tricks_won_by_declarer,
            tricks_needed=self.tricks_needed,
        )
