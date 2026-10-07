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

        // Our own candidates for these cap rounds: the cap highest cards
        // across both defending hands combined. A card beyond the top
        // cap is never the deciding card of any round -- there are only
        // cap rounds to win -- so it is excluded rather than confusing
        // the matching below with a card that can never actually win a
        // trick (and could otherwise pointlessly absorb an opposing
        // stopper that a higher card of ours still needs).
        std::vector<int> defending = sorted_ranks_descending(layout.remainCards[defender_seat][suit]);
        std::vector<int> const partner_ranks = sorted_ranks_descending(layout.remainCards[partner_seat][suit]);
        defending.insert(defending.end(), partner_ranks.begin(), partner_ranks.end());
        std::sort(defending.begin(), defending.end(), std::greater<>());
        if (defending.size() > static_cast<std::size_t>(cap))
        {
            defending.resize(static_cast<std::size_t>(cap));
        }
        std::sort(defending.begin(), defending.end());  // ascending, for the matching below

        // The opposing side's stoppers, pooled together: whichever
        // specific opponent hand holds the needed card, *some* opponent
        // uses it (never an overestimate if the pooling itself is
        // generous to the opponents, which merging rather than tracking
        // each hand's own depletion timing separately is). A real
        // opponent ducks under a lead their smallest surviving card
        // cannot beat, saving a larger card to stop a higher lead of
        // ours instead -- so each of our candidates, processed from the
        // lowest upward, must be matched against the smallest *sufficient*
        // remaining stopper, not assumed beaten by whichever opposing
        // card a purely rank-parallel walk happens to compare it to.
        std::vector<int> opponents = sorted_ranks_descending(layout.remainCards[opponent_a][suit]);
        std::vector<int> const opponent_b_ranks = sorted_ranks_descending(layout.remainCards[opponent_b][suit]);
        opponents.insert(opponents.end(), opponent_b_ranks.begin(), opponent_b_ranks.end());
        std::sort(opponents.begin(), opponents.end());  // ascending

        int count = 0;
        for (int const our_card : defending)
        {
            // No duplicate ranks exist within one suit across different
            // hands, so upper_bound's "first strictly greater" is exactly
            // the smallest card that beats our_card.
            auto const stopper = std::upper_bound(opponents.begin(), opponents.end(), our_card);
            if (stopper == opponents.end())
            {
                ++count;
            }
            else
            {
                opponents.erase(stopper);
            }
        }

        result[static_cast<std::size_t>(suit)] = count;
    }

    return result;
}

}  // namespace dds::belief_evaluation
