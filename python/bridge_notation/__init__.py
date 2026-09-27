"""Bridge notation to and from the deal dicts `belief_space_local_evaluation`
takes and returns.

The library speaks dds's own vocabulary throughout: seats and suits as small
integers, ranks as absolute 2..14, a holding as a bitmask with bit `r` set
for rank `r`. That is the right surface for a strategy, which is called
millions of times and should not be parsing strings -- but it is an
unreadable way to *write down* a deal, and an unreadable way to print one.
This module is the translation layer at the edges of an example, and nothing
here is on any hot path.
"""

# Seats, in dds's order -- also the clockwise order PBN lists hands in, which
# is why a PBN hand list maps onto this one by rotation alone.
NORTH, EAST, SOUTH, WEST = 0, 1, 2, 3
SEAT_NAMES = ("North", "East", "South", "West")
SEAT_LETTERS = "NESW"

# Suits, in dds's order -- also the order PBN writes a hand's four holdings
# in, highest-ranking first.
SPADES, HEARTS, DIAMONDS, CLUBS = 0, 1, 2, 3
SUIT_LETTERS = "SHDC"
SUIT_SYMBOLS = ("♠", "♥", "♦", "♣")

# A denomination, in dds's `trump` encoding: the four suits keep their own
# numbers and notrump follows them.
NOTRUMP = 4

RANK_LETTERS = "23456789TJQKA"


def rank_from_letter(letter: str) -> int:
    """'2'..'9', 'T', 'J', 'Q', 'K', 'A' -> the absolute rank 2..14."""
    index = RANK_LETTERS.find(letter.upper())
    if not letter or index < 0:
        # str.find("") returns 0, so the empty string would otherwise parse
        # as the first letter -- silently, and as a legal value.
        raise ValueError(f"not a rank: {letter!r}")
    return index + 2


def rank_letter(rank: int) -> str:
    if not 2 <= rank <= 14:
        raise ValueError(f"rank out of range: {rank}")
    return RANK_LETTERS[rank - 2]


def suit_from_letter(letter: str) -> int:
    index = SUIT_LETTERS.find(letter.upper())
    if not letter or index < 0:
        # str.find("") returns 0, so the empty string would otherwise parse
        # as the first letter -- silently, and as a legal value.
        raise ValueError(f"not a suit: {letter!r}")
    return index


def seat_from_letter(letter: str) -> int:
    index = SEAT_LETTERS.find(letter.upper())
    if not letter or index < 0:
        # str.find("") returns 0, so the empty string would otherwise parse
        # as the first letter -- silently, and as a legal value.
        raise ValueError(f"not a seat: {letter!r}")
    return index


def holding(*ranks: int) -> int:
    """The bitmask for a set of absolute ranks -- bit `r` for rank `r`, the
    convention `remain_cards` uses (not the bit `r-2` convention `RankMap.aggr`
    uses)."""
    mask = 0
    for rank in ranks:
        mask |= 1 << rank
    return mask


def ranks_in(mask: int) -> list:
    """The absolute ranks a `remain_cards` holding contains, highest first."""
    return [rank for rank in range(14, 1, -1) if mask & (1 << rank)]


def parse_card(text: str):
    """'SQ' or 'QS' -> a Card. Suit letter first is what this module writes;
    rank first is accepted because it is what a lot of hand records use."""
    from belief_space_local_evaluation import Card

    if len(text) != 2:
        raise ValueError(f"not a card: {text!r}")
    first, second = text[0].upper(), text[1].upper()
    if first in SUIT_LETTERS:
        return Card(suit_from_letter(first), rank_from_letter(second))
    if second in SUIT_LETTERS:
        return Card(suit_from_letter(second), rank_from_letter(first))
    raise ValueError(f"not a card: {text!r}")


def parse_cards(text: str) -> list:
    """'C6 C4 C9 CQ' -> a list of Card, in the order written."""
    return [parse_card(token) for token in text.split()]


def format_card(card, symbols: bool = True) -> str:
    letters = SUIT_SYMBOLS if symbols else SUIT_LETTERS
    return f"{letters[card.suit]}{rank_letter(card.rank)}"


def parse_deal(text: str) -> dict:
    """A PBN-style deal string -> a deal dict with nobody having played yet.

        parse_deal("N: KJ7.QJ.QT65.AJ42 A653.86432.J2.T9 T9.AKT.AK98.KQ73 Q842.975.743.865")

    Four hands, clockwise from the named seat, each written as its spade,
    heart, diamond and club holdings separated by dots. `trump` and `first`
    are not part of a deal string -- a deal is not a contract -- so they come
    out as NOTRUMP and North; `PlaySequence` sets both from the contract it
    is given.

    All 52 cards must be present exactly once. A deal that does not say that
    is a typo, and one caught here names the offending card instead of
    surfacing later as an unexplained belief space of the wrong size.
    """
    leader_text, _, hands_text = text.partition(":")
    if not _:
        raise ValueError("a deal string needs a leading seat, as in \"N: ...\"")
    first_seat = seat_from_letter(leader_text.strip())

    hands = hands_text.split()
    if len(hands) != 4:
        raise ValueError(f"expected 4 hands, got {len(hands)}")

    remain_cards = [[0, 0, 0, 0] for _ in range(4)]
    seen = set()
    for offset, hand in enumerate(hands):
        seat = (first_seat + offset) % 4
        suits = hand.split(".")
        if len(suits) != 4:
            raise ValueError(f"expected 4 suits in {hand!r}, got {len(suits)}")
        for suit, letters in enumerate(suits):
            for letter in letters:
                rank = rank_from_letter(letter)
                if (suit, rank) in seen:
                    raise ValueError(
                        f"{SUIT_LETTERS[suit]}{letter.upper()} appears twice")
                seen.add((suit, rank))
                remain_cards[seat][suit] |= 1 << rank

    if len(seen) != 52:
        missing = sorted(
            (suit, rank)
            for suit in range(4) for rank in range(2, 15)
            if (suit, rank) not in seen)
        raise ValueError(
            "deal is incomplete, missing "
            + " ".join(f"{SUIT_LETTERS[s]}{rank_letter(r)}" for s, r in missing))

    return {
        "trump": NOTRUMP,
        "first": NORTH,
        "remain_cards": remain_cards,
        "current_trick_suit": (0, 0, 0),
        "current_trick_rank": (0, 0, 0),
    }


def format_hand(remain_cards_row, symbols: bool = True) -> str:
    """One seat's holding as 'S KJ7  H Q  D -  C -'."""
    letters = SUIT_SYMBOLS if symbols else SUIT_LETTERS
    parts = []
    for suit in range(4):
        ranks = "".join(rank_letter(rank) for rank in ranks_in(remain_cards_row[suit]))
        parts.append(f"{letters[suit]} {ranks or '-'}")
    return "  ".join(parts)


def format_denomination(trump: int, symbols: bool = True) -> str:
    if trump == NOTRUMP:
        return "NT"
    return SUIT_SYMBOLS[trump] if symbols else SUIT_LETTERS[trump]
