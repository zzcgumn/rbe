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

        // The cap before ruffing: the length of the longer defending hand. While
        // both hands hold the suit, each round uses a card from each. So the
        // tricks are bounded by the longer hand, not the sum. (An ace in one hand
        // and a king in the other fall on the same trick: one trick, not two.)
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

        // Our candidates: the cap highest cards of both defending hands. There
        // are only cap rounds, so a lower card never decides one.
        std::vector<int> defending = sorted_ranks_descending(layout.remainCards[defender_seat][suit]);
        std::vector<int> const partner_ranks = sorted_ranks_descending(layout.remainCards[partner_seat][suit]);
        defending.insert(defending.end(), partner_ranks.begin(), partner_ranks.end());
        std::sort(defending.begin(), defending.end(), std::greater<>());
        if (defending.size() > static_cast<std::size_t>(cap))
        {
            defending.resize(static_cast<std::size_t>(cap));
        }

        // The highest card either opponent holds. While they can still follow,
        // our next card wins only if it beats this. Once it does not, the
        // opponents win the trick and the lead, and nothing later is certain.
        // 0 means no opposing card.
        std::vector<int> const opp_a_ranks = sorted_ranks_descending(layout.remainCards[opponent_a][suit]);
        std::vector<int> const opp_b_ranks = sorted_ranks_descending(layout.remainCards[opponent_b][suit]);
        int const opponent_max =
            std::max(opp_a_ranks.empty() ? 0 : opp_a_ranks.front(), opp_b_ranks.empty() ? 0 : opp_b_ranks.front());

        // How many rounds the opponents can follow: a hand with k cards is void
        // after k rounds, whatever happened in them. After that many rounds every
        // further card of ours within cap wins.
        int const opponent_rounds =
            std::max(static_cast<int>(opp_a_ranks.size()), static_cast<int>(opp_b_ranks.size()));

        int count = 0;
        for (int const our_card : defending)  // already highest first
        {
            if (count >= opponent_rounds)
            {
                ++count;  // the opposing side is provably void by now
                continue;
            }
            if (our_card <= opponent_max)
            {
                break;
            }
            ++count;
        }

        result[static_cast<std::size_t>(suit)] = count;
    }

    return result;
}

}  // namespace dds::belief_evaluation
