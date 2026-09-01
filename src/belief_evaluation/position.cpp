#include <belief_evaluation/position.hpp>

namespace dds::belief_evaluation
{

auto legal_plays(Position const& position, int hand, int led_suit)
    -> std::array<unsigned, DDS_SUITS>
{
    std::array<unsigned, DDS_SUITS> result{};

    if (led_suit != -1 && position.holding[hand][led_suit] != 0)
    {
        result[led_suit] = position.holding[hand][led_suit];
        return result;
    }

    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        result[suit] = position.holding[hand][suit];
    }
    return result;
}

auto trick_winner(
    int trump,
    int leader,
    std::array<int, 4> const& suit_played,
    std::array<int, 4> const& bit_played) -> int
{
    int const led_suit = suit_played[0];
    int best_index = 0;
    bool best_is_trump = (trump != DDS_NOTRUMP && suit_played[0] == trump);

    for (int i = 1; i < 4; ++i)
    {
        bool const is_trump = (trump != DDS_NOTRUMP && suit_played[i] == trump);

        if (is_trump && ! best_is_trump)
        {
            best_index = i;
            best_is_trump = true;
        }
        else if (is_trump == best_is_trump)
        {
            bool const comparable = is_trump || suit_played[i] == led_suit;
            if (comparable && bit_played[i] > bit_played[best_index])
            {
                best_index = i;
            }
        }
    }

    return (leader + best_index) % DDS_HANDS;
}

auto play_card(Position& position, int hand, int suit, int bit_position) -> void
{
    position.holding[hand][suit] &= ~(1u << bit_position);
}

}  // namespace dds::belief_evaluation
