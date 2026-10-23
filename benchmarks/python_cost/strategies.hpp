#pragma once

// The declarer and defender strategies timer.cpp times; timer.py ports
// them line for line. As with benchmarks/belief_evaluation/strategies.hpp,
// a small strategy lives beside the benchmark that uses it, not in
// tests/belief_evaluation/test_support.hpp, which is private to the core
// C++ tests.
//
// safety_score_declarer_play_test.cpp proves real_work_declarer_play
// correct, independently of any fixture or evaluate() call. Its Python
// port is checked in
// python/tests/test_belief_space_local_evaluation_python_cost.py. timer.cpp
// only times it.

#include <cstdint>
#include <vector>

#include <bit>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation::benchmarks::python_cost
{

inline auto seat_on_play(Deal const& deal) -> int
{
    int played = 0;
    for (int i = 0; i < 3 && deal.currentTrickRank[i] != 0; ++i)
    {
        ++played;
    }
    return (deal.first + played) % DDS_HANDS;
}

inline auto lowest_legal_card(Deal const& deal, int seat) -> Card
{
    int led = -1;
    if (deal.currentTrickRank[0] != 0)
    {
        led = deal.currentTrickSuit[0];
    }
    if (led != -1 && deal.remainCards[seat][led] != 0)
    {
        for (int rank = 2; rank <= 14; ++rank)
        {
            if ((deal.remainCards[seat][led] & (1u << rank)) != 0)
            {
                return Card{led, rank};
            }
        }
    }
    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        unsigned const holding = deal.remainCards[seat][suit];
        for (int rank = 2; rank <= 14; ++rank)
        {
            if ((holding & (1u << rank)) != 0)
            {
                return Card{suit, rank};
            }
        }
    }
    return Card{};
}

/// The trivial pi and delta for the other side of the ratio: O(1) per
/// call. The rule is that of single_card_declarer_play and
/// single_card_defender (tests/belief_evaluation/test_support.hpp), whose
/// correctness the parity test already proves. The names say their role
/// in this comparison (trivial_*), which is what a caller chooses by.
inline auto trivial_declarer_play(ObservationState const& state, BeliefView const&) -> Card
{
    return lowest_legal_card(state.known_holdings, seat_on_play(state.known_holdings));
}

inline auto trivial_defender_play(DefenderQuery const& query) -> std::vector<WeightedCard>
{
    return {WeightedCard{lowest_legal_card(query.layout, query.seat), 1.0}};
}

/// Every card `seat` may legally play in `deal`: the suit led if `seat`
/// holds it, else every card `seat` holds. `lowest_legal_card` picks the
/// lowest of this set; this returns the whole set, for
/// `real_work_declarer_play`. Not named `legal_cards`: trick.hpp already
/// declares a public `legal_cards()` returning per-suit bitmasks, and an
/// overload that differs only in return type is ill-formed.
inline auto enumerate_legal_cards(Deal const& deal, int seat) -> std::vector<Card>
{
    std::vector<Card> cards;
    auto const collect = [&](int suit)
    {
        unsigned const suit_holding = deal.remainCards[seat][suit];
        for (int rank = 2; rank <= 14; ++rank)
        {
            if ((suit_holding & (1u << rank)) != 0)
            {
                cards.push_back(Card{suit, rank});
            }
        }
    };
    int led = -1;
    if (deal.currentTrickRank[0] != 0)
    {
        led = deal.currentTrickSuit[0];
    }
    if (led != -1 && deal.remainCards[seat][led] != 0)
    {
        collect(led);
        return cards;
    }
    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        collect(suit);
    }
    return cards;
}

/// How many cards in `suit`, held by either of `declarer`'s opponents in
/// `layout`, outrank `rank`. The defenders are (declarer+1)%DDS_HANDS and
/// (declarer+3)%DDS_HANDS, whoever is on play. `layout` is one concrete
/// deal (a BeliefEntry's layout), not the defender pool ObservationState
/// carries.
inline auto higher_defender_count(Deal const& layout, int declarer, int suit, int rank) -> int
{
    int const east = (declarer + 1) % DDS_HANDS;
    int const west = (declarer + 3) % DDS_HANDS;
    unsigned const holding = layout.remainCards[east][suit] | layout.remainCards[west][suit];
    unsigned const above_rank = ~((1u << (rank + 1)) - 1u);
    return std::popcount(holding & above_rank);
}

/// A DeclarerStrategy::play with real work per call, for the callback cost
/// comparison. For each legal card it sums, over every entry in `view`,
/// the posterior times the number of defender cards that beat it. It plays
/// the lowest-scoring (safest) card, breaking ties by rank. That is
/// `entries * legal cards` work, unlike trivial_declarer_play's O(1) rule.
/// It exists to give the comparison a callback with real cost. It is not a
/// good bridge heuristic: it never looks past this trick.
inline auto real_work_declarer_play(ObservationState const& state, BeliefView const& view) -> Card
{
    int const seat = seat_on_play(state.known_holdings);
    std::vector<Card> const candidates = enumerate_legal_cards(state.known_holdings, seat);

    Card best{};
    double best_score = 0.0;
    bool have_best = false;
    for (Card const& candidate : candidates)
    {
        double score = 0.0;
        for (BeliefEntry const& entry : view.entries)
        {
            score += entry.posterior
                * static_cast<double>(
                    higher_defender_count(entry.layout, state.declarer, candidate.suit, candidate.rank));
        }
        if (! have_best || score < best_score || (score == best_score && candidate.rank < best.rank))
        {
            best = candidate;
            best_score = score;
            have_best = true;
        }
    }
    return best;
}

}  // namespace dds::belief_evaluation::benchmarks::python_cost
