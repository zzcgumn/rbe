#include <belief_evaluation/suit_top_tricks.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <vector>

#include <api/dds_constants.hpp>

namespace dds::belief_evaluation
{

namespace
{
    auto card_count(unsigned holding) -> int
    {
        return std::popcount(static_cast<std::uint32_t>(holding));
    }

    auto sorted_ranks_descending(unsigned holding) -> std::vector<int>
    {
        std::vector<int> ranks;
        for (int rank = 14; rank >= 2; --rank)
        {
            if ((holding & (1u << rank)) != 0)
            {
                ranks.push_back(rank);
            }
        }
        return ranks;
    }
}

auto suit_top_tricks(Deal const& layout, int defender_seat, int trump) -> std::array<int, 4>
{
    int const partner_seat = (defender_seat + 2) % DDS_HANDS;
    int const opponent_a = (defender_seat + 1) % DDS_HANDS;
    int const opponent_b = (defender_seat + 3) % DDS_HANDS;

    std::array<int, 4> result{};

    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        int const seat_count = card_count(layout.remainCards[defender_seat][suit]);
        int const partner_count = card_count(layout.remainCards[partner_seat][suit]);

        // The sound upper bound before any ruffing reduction: a round of
        // this suit, for as long as *both* defending hands still hold it,
        // consumes one card from each of them simultaneously -- whoever
        // is not on lead that round still has to follow suit in the same
        // trick, not save its card for a separate one. So the two hands
        // cannot cash seat_count + partner_count separate tricks merely
        // by combining counts; the number of rounds the suit survives is
        // bounded by whichever defending hand is longer, not their sum
        // (the shorter hand becomes void partway through, after which
        // the longer one keeps running the suit alone for the remaining
        // rounds). Summing was tried first and rejected after tracing a
        // single ace-with-one-partner/king-with-the-other round by hand:
        // both cards are consumed in the very same trick, giving one
        // cashable trick there, not two.
        int cap = std::max(seat_count, partner_count);

        if (trump != DDS_NOTRUMP && suit != trump)
        {
            if (card_count(layout.remainCards[opponent_a][trump]) > 0)
            {
                cap = std::min(cap, card_count(layout.remainCards[opponent_a][suit]));
            }
            if (card_count(layout.remainCards[opponent_b][trump]) > 0)
            {
                cap = std::min(cap, card_count(layout.remainCards[opponent_b][suit]));
            }
        }

        std::vector<int> defending = sorted_ranks_descending(layout.remainCards[defender_seat][suit]);
        std::vector<int> const partner_ranks = sorted_ranks_descending(layout.remainCards[partner_seat][suit]);
        defending.insert(defending.end(), partner_ranks.begin(), partner_ranks.end());
        std::sort(defending.begin(), defending.end(), std::greater<>());

        std::vector<int> const opp_a_ranks = sorted_ranks_descending(layout.remainCards[opponent_a][suit]);
        std::vector<int> const opp_b_ranks = sorted_ranks_descending(layout.remainCards[opponent_b][suit]);

        int count = 0;
        std::size_t defending_index = 0;
        std::size_t opp_a_index = 0;
        std::size_t opp_b_index = 0;

        while (count < cap && defending_index < defending.size())
        {
            int const lead = defending[defending_index];
            bool const a_beats = opp_a_index < opp_a_ranks.size() && opp_a_ranks[opp_a_index] > lead;
            bool const b_beats = opp_b_index < opp_b_ranks.size() && opp_b_ranks[opp_b_index] > lead;
            if (a_beats || b_beats)
            {
                break;
            }

            ++count;
            ++defending_index;
            if (opp_a_index < opp_a_ranks.size())
            {
                ++opp_a_index;
            }
            if (opp_b_index < opp_b_ranks.size())
            {
                ++opp_b_index;
            }
        }

        result[static_cast<std::size_t>(suit)] = count;
    }

    return result;
}

}  // namespace dds::belief_evaluation
