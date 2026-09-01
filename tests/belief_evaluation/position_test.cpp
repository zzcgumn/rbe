#include <gtest/gtest.h>

#include <array>

#include <utility/constants.h>

#include <belief_evaluation/position.hpp>
#include <belief_evaluation/renumber.hpp>

// A namespace alias plus targeted `using` declarations, not `using namespace
// dds::belief_evaluation;` -- see the other test files in this directory for
// why: kept consistent even though this particular file doesn't yet include
// anything that makes api/dds_data_types.hpp's global ::Card visible.
namespace be = dds::belief_evaluation;
using be::Position;
using be::legal_plays;
using be::play_card;
using be::renumber;
using be::trick_winner;

namespace
{
    // The pool (OR of all four hands' holdings) per suit, fixed for the
    // whole deal — renumbering is applied once, up front, to the starting
    // position, exactly as algorithm.md's "randomised array" model does.
    auto pool_per_suit(Position const& position) -> std::array<unsigned, DDS_SUITS>
    {
        std::array<unsigned, DDS_SUITS> pool{};
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            unsigned p = 0;
            for (int hand = 0; hand < DDS_HANDS; ++hand)
            {
                p |= position.holding[hand][suit];
            }
            pool[suit] = p;
        }
        return pool;
    }

    auto renumbered_position(Position const& original, std::array<unsigned, DDS_SUITS> const& pool)
        -> Position
    {
        Position result{};
        result.trump = original.trump;
        result.leader = original.leader;
        for (int hand = 0; hand < DDS_HANDS; ++hand)
        {
            for (int suit = 0; suit < DDS_SUITS; ++suit)
            {
                result.holding[hand][suit] = renumber(original.holding[hand][suit], pool[suit]);
            }
        }
        return result;
    }

    // Bit position of the single set bit in a one-card mask.
    auto single_bit_position(unsigned mask) -> int
    {
        for (int bit = 0; bit < 13; ++bit)
        {
            if ((mask & (1u << bit)) != 0)
            {
                return bit;
            }
        }
        return -1;
    }

    auto total_holding(Position const& position, int hand) -> unsigned
    {
        unsigned total = 0;
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            total |= position.holding[hand][suit];
        }
        return total;
    }

    // Exhaustively walks every legal line of play from `orig` (the caller's
    // position) alongside its fixed renumbering `renum`, asserting at every
    // node that legal move sets correspond under `renumber` and, at every
    // completed trick, that the winning hand corresponds; at the end of the
    // deal, that every hand's trick count corresponds.
    class IsomorphismWalk
    {
    public:
        IsomorphismWalk(Position const& original, std::array<unsigned, DDS_SUITS> const& pool)
        : pool_(pool)
        {
            walk(
                original,
                renumbered_position(original, pool),
                original.leader,
                -1,
                {},
                {},
                {},
                0,
                original.leader,
                {},
                {});
        }

    private:
        std::array<unsigned, DDS_SUITS> pool_;

        auto walk(
            Position orig,
            Position renum,
            int hand,
            int led_suit,
            std::array<int, 4> suits,
            std::array<int, 4> orig_bits,
            std::array<int, 4> renum_bits,
            int count_in_trick,
            int trick_leader,
            std::array<int, 4> orig_tricks,
            std::array<int, 4> renum_tricks) -> void
        {
            if (count_in_trick == 0 && total_holding(orig, hand) == 0)
            {
                // Deal complete: every hand's trick count must correspond.
                // Also assert every hand is actually empty, not just the one
                // about to lead — a starting position with unequal card
                // counts per hand would otherwise let this walk terminate
                // early and silently under-cover the isomorphism property,
                // passing without ever having walked the whole deal.
                for (int h = 0; h < DDS_HANDS; ++h)
                {
                    EXPECT_EQ(total_holding(orig, h), 0u)
                        << "hand " << h << " still holds cards; the starting "
                           "position is unbalanced";
                    EXPECT_EQ(orig_tricks[h], renum_tricks[h]);
                }
                return;
            }

            auto const orig_legal = legal_plays(orig, hand, led_suit);
            auto const renum_legal = legal_plays(renum, hand, led_suit);

            // A hand that is unexpectedly empty mid-trick (not at a trick
            // boundary) returns an all-zero legal set here, and the loop
            // below then has nothing to iterate — the walk would otherwise
            // dead-end silently, firing no assertion at all rather than
            // failing loudly, hiding the same class of unbalanced-position
            // bug the trick-boundary check above guards against.
            EXPECT_NE(
                orig_legal[0] | orig_legal[1] | orig_legal[2] | orig_legal[3], 0u)
                << "hand " << hand << " has no legal play mid-trick; the "
                   "starting position is unbalanced";

            for (int suit = 0; suit < DDS_SUITS; ++suit)
            {
                EXPECT_EQ(renumber(orig_legal[suit], pool_[suit]), renum_legal[suit])
                    << "hand=" << hand << " suit=" << suit;
            }

            for (int suit = 0; suit < DDS_SUITS; ++suit)
            {
                unsigned remaining = orig_legal[suit];
                while (remaining != 0)
                {
                    int const bit = single_bit_position(remaining);
                    remaining &= ~(1u << bit);

                    Position next_orig = orig;
                    play_card(next_orig, hand, suit, bit);

                    int const renum_bit =
                        single_bit_position(renumber(1u << bit, pool_[suit]));
                    Position next_renum = renum;
                    play_card(next_renum, hand, suit, renum_bit);

                    auto next_suits = suits;
                    auto next_orig_bits = orig_bits;
                    auto next_renum_bits = renum_bits;
                    next_suits[count_in_trick] = suit;
                    next_orig_bits[count_in_trick] = bit;
                    next_renum_bits[count_in_trick] = renum_bit;

                    int const new_led_suit = (count_in_trick == 0) ? suit : led_suit;

                    if (count_in_trick == 3)
                    {
                        int const orig_winner =
                            trick_winner(orig.trump, trick_leader, next_suits, next_orig_bits);
                        int const renum_winner =
                            trick_winner(renum.trump, trick_leader, next_suits, next_renum_bits);
                        EXPECT_EQ(orig_winner, renum_winner);

                        auto next_orig_tricks = orig_tricks;
                        auto next_renum_tricks = renum_tricks;
                        ++next_orig_tricks[orig_winner];
                        ++next_renum_tricks[renum_winner];

                        walk(
                            next_orig,
                            next_renum,
                            orig_winner,
                            -1,
                            {},
                            {},
                            {},
                            0,
                            orig_winner,
                            next_orig_tricks,
                            next_renum_tricks);
                    }
                    else
                    {
                        walk(
                            next_orig,
                            next_renum,
                            (hand + 1) % DDS_HANDS,
                            new_led_suit,
                            next_suits,
                            next_orig_bits,
                            next_renum_bits,
                            count_in_trick + 1,
                            trick_leader,
                            orig_tricks,
                            renum_tricks);
                    }
                }
            }
        }
    };

    auto assert_isomorphic(Position const& original) -> void
    {
        IsomorphismWalk(original, pool_per_suit(original));
    }
}  // namespace

// Direct correctness checks against actual bridge rules — independent of the
// renumbering isomorphism, which is agnostic to *which* order-based rule
// trick_winner applies (an order-preserving bijection keeps "lowest wins"
// just as internally consistent as "highest wins"). These pin the rule down.

TEST(TrickWinner, HighestCardOfLedSuitWinsWithNoTrumpInPlay)
{
    // Leader (hand 0) leads suit 0, bit 3; others follow suit with lower
    // cards. No trump in play (NOTRUMP).
    std::array<int, 4> suits{0, 0, 0, 0};
    std::array<int, 4> bits{3, 1, 2, 0};
    EXPECT_EQ(trick_winner(DDS_NOTRUMP, 0, suits, bits), 0);
}

TEST(TrickWinner, HighestCardOfLedSuitWinsWhenNotTheLeader)
{
    std::array<int, 4> suits{0, 0, 0, 0};
    std::array<int, 4> bits{1, 3, 2, 0};
    // Play order is (leader, leader+1, leader+2, leader+3) = hands (2,3,0,1)
    // when leader = 2. The highest bit is at play-index 1 -> hand 3.
    EXPECT_EQ(trick_winner(DDS_NOTRUMP, 2, suits, bits), 3);
}

TEST(TrickWinner, AnyTrumpBeatsAnyCardOfTheLedSuit)
{
    // Led suit 0 (spades); play-index 2 discards a low trump (suit 1).
    std::array<int, 4> suits{0, 0, 1, 0};
    std::array<int, 4> bits{5, 6, 0, 4};  // trump bit 0 is the lowest card there is
    EXPECT_EQ(trick_winner(/* trump = */ 1, 0, suits, bits), 2);
}

TEST(TrickWinner, HighestTrumpWinsWhenSeveralTrumpsArePlayed)
{
    std::array<int, 4> suits{0, 1, 1, 0};
    std::array<int, 4> bits{5, 2, 7, 4};
    EXPECT_EQ(trick_winner(/* trump = */ 1, 0, suits, bits), 2);
}

TEST(TrickWinner, DiscardsOffSuitAndNotTrumpNeverWin)
{
    // Led suit 0; play-index 3 discards suit 2 (not trump); trump is suit 1
    // but nobody plays it, so the highest card of the led suit wins.
    std::array<int, 4> suits{0, 0, 0, 2};
    std::array<int, 4> bits{2, 5, 1, 12};
    EXPECT_EQ(trick_winner(/* trump = */ 1, 0, suits, bits), 1);
}

TEST(LegalPlays, MustFollowSuitWhenHoldingTheLedSuit)
{
    Position position{};
    position.holding[0][0] = 0b101;  // spades: two cards
    position.holding[0][1] = 0b010;  // hearts: one card

    auto const legal = legal_plays(position, 0, /* led_suit = */ 0);

    EXPECT_EQ(legal[0], 0b101u);
    EXPECT_EQ(legal[1], 0u);
}

TEST(LegalPlays, AnyHeldCardIsLegalWhenVoidInTheLedSuit)
{
    Position position{};
    position.holding[0][0] = 0;      // void in spades
    position.holding[0][1] = 0b010;

    auto const legal = legal_plays(position, 0, /* led_suit = */ 0);

    EXPECT_EQ(legal[0], 0u);
    EXPECT_EQ(legal[1], 0b010u);
}

TEST(LegalPlays, AnyHeldCardIsLegalWhenLeading)
{
    Position position{};
    position.holding[0][0] = 0b101;
    position.holding[0][1] = 0b010;

    auto const legal = legal_plays(position, 0, /* led_suit = */ -1);

    EXPECT_EQ(legal[0], 0b101u);
    EXPECT_EQ(legal[1], 0b010u);
}

TEST(PositionIsomorphism, TwoCardsPerHandOneSuit)
{
    // Spades only: N={0,1} E={2,3} S={4,5} W={6,7}, notrump.
    Position position{};
    position.trump = DDS_NOTRUMP;
    position.leader = 0;
    position.holding[0][0] = 0b00000011;
    position.holding[1][0] = 0b00001100;
    position.holding[2][0] = 0b00110000;
    position.holding[3][0] = 0b11000000;

    assert_isomorphic(position);
}

TEST(PositionIsomorphism, ThreeCardsPerHandTwoSuits)
{
    // Spades: 2 each. Hearts: 1 each. Notrump.
    Position position{};
    position.trump = DDS_NOTRUMP;
    position.leader = 1;
    position.holding[0][0] = 0b0011;
    position.holding[1][0] = 0b1100;
    position.holding[2][0] = 0b0011 << 4;
    position.holding[3][0] = 0b1100 << 4;
    position.holding[0][1] = 0b0001;
    position.holding[1][1] = 0b0010;
    position.holding[2][1] = 0b0100;
    position.holding[3][1] = 0b1000;

    assert_isomorphic(position);
}

TEST(PositionIsomorphism, FiveCardsPerHandThreeSuitsWithTrumpAndAVoid)
{
    // Every hand holds exactly 5 cards. Spades (trump): 2 each, 8 of the
    // suit's 13 bits. Hearts: N,E,W hold 2 each, South holds only 1 — South
    // goes void in hearts after playing it, enabling a ruff. Diamonds:
    // N,E,W hold 1 each, South holds 2, making up South's fifth card.
    //
    // (An earlier version of this test gave South 4 spades + 1 heart +
    // 2 diamonds = 7 cards against 5 for every other hand. The isomorphism
    // walk terminates once the hand about to lead is empty, so that
    // imbalance let N/E/W's exhaustion end the walk early — South's extra
    // two cards, and the tricks that would have played them, were never
    // walked, silently under-covering the property this test exists to
    // assert. Every hand having the same total is what makes "the hand
    // about to lead is empty" a sound stand-in for "the deal is complete";
    // the walk itself now also asserts this explicitly.)
    Position position{};
    position.trump = 0;  // spades
    position.leader = 0;

    position.holding[0][0] = 0b00000011;  // N spades: bits 0,1
    position.holding[1][0] = 0b00001100;  // E spades: bits 2,3
    position.holding[2][0] = 0b00110000;  // S spades: bits 4,5
    position.holding[3][0] = 0b11000000;  // W spades: bits 6,7

    position.holding[0][1] = 0b0000011;  // N hearts: bits 0,1
    position.holding[1][1] = 0b0001100;  // E hearts: bits 2,3
    position.holding[2][1] = 0b0010000;  // S hearts: bit 4 (void after played)
    position.holding[3][1] = 0b1100000;  // W hearts: bits 5,6

    position.holding[0][2] = 0b00001;  // N diamonds: bit 0
    position.holding[1][2] = 0b00010;  // E diamonds: bit 1
    position.holding[2][2] = 0b01100;  // S diamonds: bits 2,3
    position.holding[3][2] = 0b10000;  // W diamonds: bit 4

    assert_isomorphic(position);
}
