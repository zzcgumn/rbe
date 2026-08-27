#include <belief_evaluation/spread.hpp>

#include <cassert>
#include <map>

#include <utility/constants.h>

namespace
{
    /// The cards named by `fut.equals[i]` -- a rank bitmask in Deal's own
    /// bit convention, excluding `fut.rank[i]` itself -- as Cards in
    /// `fut.suit[i]`.
    auto touching_cards(FutureTricks const& fut, int i) -> std::vector<Card>
    {
        std::vector<Card> cards;
        auto const mask = static_cast<unsigned>(fut.equals[i]);
        for (int rank = 2; rank <= 14; ++rank)
        {
            if ((mask & (1u << rank)) != 0)
            {
                cards.push_back(Card{fut.suit[i], rank});
            }
        }
        return cards;
    }

    /// A total order over Card for deduplication -- two fut entries in the
    /// same touching sequence must not double-count the cards they share.
    /// Collision-free only for a well-formed card (suit 0..3, rank 2..14),
    /// which is all `FutureTricks` and `touching_cards()` above ever
    /// populate -- asserted rather than silently tolerated, since a
    /// malformed suit/rank here would collide or go negative without
    /// either failing loudly or being a valid card to begin with.
    auto card_key(Card const& card) -> int
    {
        assert(card.suit >= 0 && card.suit < DDS_SUITS);
        assert(card.rank >= 2 && card.rank <= 14);
        return card.suit * 100 + card.rank;
    }

    auto insert_candidate(std::map<int, Card>& candidates, Card const& card) -> void
    {
        candidates.emplace(card_key(card), card);
    }

    /// The single canonical best entry: the highest score, breaking ties by
    /// keeping the first entry reached (dds's own ordering).
    auto canonical_best(FutureTricks const& fut) -> int
    {
        int best = 0;
        for (int i = 1; i < fut.cards; ++i)
        {
            if (fut.score[i] > fut.score[best])
            {
                best = i;
            }
        }
        return best;
    }

    auto max_score(FutureTricks const& fut) -> int
    {
        int max = fut.score[0];
        for (int i = 1; i < fut.cards; ++i)
        {
            if (fut.score[i] > max)
            {
                max = fut.score[i];
            }
        }
        return max;
    }
}

auto spread(FutureTricks const& fut, SpreadPolicy policy) -> std::vector<WeightedCard>
{
    if (fut.cards <= 0)
    {
        return {};
    }

    std::map<int, Card> candidates;  // keyed for both dedup and a deterministic order

    if (policy == SpreadPolicy::TouchingSequence)
    {
        int const best = canonical_best(fut);
        insert_candidate(candidates, Card{fut.suit[best], fut.rank[best]});
        for (Card const& touching : touching_cards(fut, best))
        {
            insert_candidate(candidates, touching);
        }
    }
    else  // AllOptimal
    {
        int const best_score = max_score(fut);
        for (int i = 0; i < fut.cards; ++i)
        {
            if (fut.score[i] != best_score)
            {
                continue;
            }
            insert_candidate(candidates, Card{fut.suit[i], fut.rank[i]});
            for (Card const& touching : touching_cards(fut, i))
            {
                insert_candidate(candidates, touching);
            }
        }
    }

    std::vector<WeightedCard> distribution;
    distribution.reserve(candidates.size());
    double const probability = 1.0 / static_cast<double>(candidates.size());
    for (auto const& [key, card] : candidates)
    {
        distribution.push_back(WeightedCard{card, probability});
    }
    return distribution;
}
