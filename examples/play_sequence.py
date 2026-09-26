"""Playing a hand out to the position you want to evaluate.

`belief_space_local_evaluation` evaluates *one* root: a position, a declarer,
and how many more tricks are still needed. It does not play cards, and it
carries no trick counter -- `evaluate()` is handed the answer to "how many
more?" rather than working it out. Getting to an interesting root therefore
means playing the earlier tricks yourself, and that is all this module does.

It also collects the thing a root alone cannot express: the **play history**.
Handed to `ExhaustiveLayoutSource`, that history is what rules out the
layouts in which a defender holds a suit they have already shown out of. See
"The play history: needed, not merely optional" in
docs/belief_space_local_evaluation.md -- omitting it does not fail, it
quietly answers a different question.

The trick mechanics themselves are **not** here any more. `bsle.seat_on_play`,
`bsle.legal_cards`, `bsle.play` and `bsle.trick_complete_winner` are the
library's own, the same four the evaluator applies to the root you hand it, so
this module no longer carries a second copy of the follow-suit and trick-winner
rules that could disagree with them. Nor the derivations a strategy needs: a
declarer strategy reads `state.seat_on_play`, `state.trick_leader`,
`state.current_trick` and `state.legal_cards` off the ObservationState it is
handed. What is left is the bookkeeping the library genuinely does not do:
counting tricks, collecting the history, and turning hand records into cards.
"""

import belief_space_local_evaluation as bsle

from bridge_notation import (
    SEAT_NAMES,
    format_card,
    parse_cards,
)


def cards_on_trick(deal: dict) -> list:
    """The cards already played to the trick in progress, in play order --
    empty when the seat on play is leading.

    These are *not* in anybody's `remain_cards`: a card is removed from the
    hand that played it as it is played. The trick in progress lives only
    here.
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

    The play itself is the library's. What is added here is a diagnostic:
    `bsle.play` has a precondition rather than a check, so a hand record with
    a card in the wrong place would otherwise produce a nonsense position
    instead of an error naming the card. Transcription mistakes are the
    common case for this module's callers, so they are worth catching by name.
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

    Play tricks onto it, then read off the three things `evaluate()` wants --
    `current_deal`, `tricks_needed`, and the `history`/`opening_leader` pair
    an `ExhaustiveLayoutSource` needs.
    """

    def __init__(self, deal: dict, declarer: int, trump: int, level: int):
        self.declarer = declarer
        self.level = level
        self.opening_leader = (declarer + 1) % 4
        self.history = []
        self.tricks_won_by_declarer = 0
        self.completed_tricks = []
        self.current_deal = dict(deal, trump=trump, first=self.opening_leader)

    @property
    def dummy(self) -> int:
        return (self.declarer + 2) % 4

    @property
    def tricks_to_make(self) -> int:
        """What the contract needs in total: six plus the level."""
        return self.level + 6

    @property
    def tricks_needed(self) -> int:
        """What is still needed from here -- `evaluate()`'s third argument.
        Counted from the root, which is why it shrinks as tricks are won."""
        return self.tricks_to_make - self.tricks_won_by_declarer

    def play(self, *cards) -> "PlaySequence":
        """Play cards in order, from whoever is on play. Accepts Cards, or
        notation as one string per card or one string for the lot."""
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
            winner = self.current_deal["first"]
            self.completed_tricks.append((leader, self.history[-4:], winner))
            if winner in (self.declarer, self.dummy):
                self.tricks_won_by_declarer += 1

    def play_trick(self, cards) -> "PlaySequence":
        """Play four cards, checking they really are a whole trick. Worth
        preferring over `play` when transcribing a hand record: a dropped card
        otherwise shows up as every later trick being played by the wrong
        seats."""
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
