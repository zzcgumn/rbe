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
"""

from bridge_notation import (
    NOTRUMP,
    SEAT_NAMES,
    format_card,
    parse_cards,
    ranks_in,
)


def seat_on_play(deal: dict) -> int:
    """The seat to play next: the trick's leader, advanced by the number of
    cards already played to the trick in progress."""
    played = 0
    for rank in deal["current_trick_rank"]:
        if rank == 0:
            break
        played += 1
    return (deal["first"] + played) % 4


def trick_leader(deal: dict) -> int:
    """The seat that led to the trick *in progress*.

    Not `ObservationState.first`, which is the seat on lead at the **root**
    and never moves for the whole evaluation. A deal's own `first` is
    reassigned to the winner every time a trick resolves. The two agree at
    the root and diverge from the second trick on, so a strategy that reaches
    for `state.first` here is right in testing and wrong in play.
    """
    return deal["first"]


def cards_on_trick(deal: dict) -> list:
    """The cards already played to the trick in progress, in play order --
    empty when the seat on play is leading.

    These are *not* in anybody's `remain_cards`: a card is removed from the
    hand that played it as it is played. The trick in progress lives only
    here.
    """
    from belief_space_local_evaluation import Card

    return [
        Card(suit, rank)
        for suit, rank in zip(deal["current_trick_suit"], deal["current_trick_rank"])
        if rank != 0
    ]


def suit_led(deal: dict) -> int:
    """The suit led to the trick in progress, or -1 if nobody has led yet."""
    if deal["current_trick_rank"][0] == 0:
        return -1
    return deal["current_trick_suit"][0]


def legal_cards(deal: dict, seat: int) -> list:
    """Every card `seat` may legally play next, highest first within a suit.

    The whole rule: follow the suit led if you hold any of it, otherwise play
    anything.
    """
    from belief_space_local_evaluation import Card

    remain_cards = deal["remain_cards"][seat]
    led = suit_led(deal)
    suits = [led] if led >= 0 and remain_cards[led] != 0 else range(4)
    return [Card(suit, rank) for suit in suits for rank in ranks_in(remain_cards[suit])]


def _trick_winner(trump: int, first: int, cards: list) -> int:
    """Which seat wins a complete trick: highest trump if any was played,
    otherwise highest card of the suit led."""
    led = cards[0].suit
    contest = trump if trump != NOTRUMP and any(c.suit == trump for c in cards) else led
    best = max(
        (i for i, card in enumerate(cards) if card.suit == contest),
        key=lambda i: cards[i].rank)
    return (first + best) % 4


def play_card(deal: dict, card) -> dict:
    """The deal after the seat on play plays `card` -- the card removed from
    their holding and either appended to the trick in progress or, when it
    completes the trick, the trick resolved and `first` set to the winner.

    Pure: `deal` is not modified. This mirrors `play()` in
    src/belief_evaluation/trick.hpp, which is the library's own version of
    the same mechanics but is not exposed to Python.
    """
    from belief_space_local_evaluation import Card

    seat = seat_on_play(deal)
    if deal["remain_cards"][seat][card.suit] & (1 << card.rank) == 0:
        raise ValueError(
            f"{SEAT_NAMES[seat]} does not hold {format_card(card, symbols=False)}")
    legal = legal_cards(deal, seat)
    if not any(c.suit == card.suit and c.rank == card.rank for c in legal):
        raise ValueError(
            f"{SEAT_NAMES[seat]} must follow suit and cannot play "
            f"{format_card(card, symbols=False)}")

    remain_cards = [list(row) for row in deal["remain_cards"]]
    remain_cards[seat][card.suit] &= ~(1 << card.rank)

    suits = list(deal["current_trick_suit"])
    ranks = list(deal["current_trick_rank"])
    played = sum(1 for rank in ranks if rank != 0)

    if played < 3:
        suits[played], ranks[played] = card.suit, card.rank
        first = deal["first"]
    else:
        trick = [Card(suits[i], ranks[i]) for i in range(3)] + [card]
        first = _trick_winner(deal["trump"], deal["first"], trick)
        suits, ranks = [0, 0, 0], [0, 0, 0]

    return {
        "trump": deal["trump"],
        "first": first,
        "remain_cards": remain_cards,
        "current_trick_suit": tuple(suits),
        "current_trick_rank": tuple(ranks),
    }


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
