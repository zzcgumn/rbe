#pragma once

// The declarer/defender strategy pair timer.cpp (and, ported line-for-line,
// timer.py) time -- the fixture-side counterpart to
// benchmarks/belief_evaluation/strategies.hpp, which already established
// the precedent this file follows: a small strategy belongs beside the
// benchmark that uses it, not folded into
// library/tests/belief_evaluation/test_support.hpp (private to the core
// C++ test suite -- its own module comment says so, and this plan's own
// task breakdown says explicitly not to extend it). `real_work_declarer_
// play`'s own correctness is proven once, independent of any fixture or
// evaluate() call, in safety_score_declarer_play_test.cpp beside this
// header and its independent Python port,
// python/tests/test_belief_space_local_evaluation_python_cost.py -- timer.cpp
// only times this copy, it does not re-prove it.

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

/// The trivial pi/delta this comparison's other side of the ratio needs --
/// O(1) per call, matching single_card_declarer_play/single_card_defender's
/// own rule (library/tests/belief_evaluation/test_support.hpp) exactly, so
/// that rule is duplicated a third time here rather than the correctness
/// this plan already proved for it (parity_reference.cpp's own parity
/// test) being re-proven. Named for their role in this comparison
/// (trivial_*), not for the rule they implement, since that is what every
/// caller of this header actually wants to select between.
inline auto trivial_declarer_play(ObservationState const& state, BeliefView const&) -> Card
{
    return lowest_legal_card(state.known_holdings, seat_on_play(state.known_holdings));
}

inline auto trivial_defender_play(DefenderQuery const& query) -> std::vector<WeightedCard>
{
    return {WeightedCard{lowest_legal_card(query.layout, query.seat), 1.0}};
}

/// Every card `seat` may legally play in `deal` -- the suit led to the
/// trick in progress if `seat` holds it, else every suit `seat` holds
/// anything in. `lowest_legal_card`'s own first branch picks the lowest of
/// this same set; this returns the whole set, for `real_work_declarer_play`
/// below, which has to choose among more than one. Named
/// `enumerate_legal_cards`, not `legal_cards`: trick.hpp already declares a
/// public `legal_cards()` returning a different, per-suit-bitmask shape,
/// and a return-type-only overload is ill-formed rather than a shadow.
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

/// How many of `declarer`'s two opponents' cards in `suit`, combined, in
/// this one concrete `layout`, outrank `rank` -- the two defender seats are
/// (declarer+1)%DDS_HANDS and (declarer+3)%DDS_HANDS regardless of which of
/// them is actually on play, since `layout` is one full, concrete deal (a
/// BeliefEntry's own layout, not the aggregate pool ObservationState
/// carries for a defender seat).
inline auto higher_defender_count(Deal const& layout, int declarer, int suit, int rank) -> int
{
    int const east = (declarer + 1) % DDS_HANDS;
    int const west = (declarer + 3) % DDS_HANDS;
    unsigned const holding = layout.remainCards[east][suit] | layout.remainCards[west][suit];
    unsigned const above_rank = ~((1u << (rank + 1)) - 1u);
    return std::popcount(holding & above_rank);
}

/// A DeclarerStrategy::play with real per-call work, for the Python-vs-C++
/// per-callback cost comparison timer.{cpp,py} run: among the legal cards
/// the seat on play holds, scores each one by the posterior-weighted count
/// of defender cards (summed over every entry in `view`) that would beat
/// it, and plays the lowest-scoring (safest) one, breaking a tie by rank.
/// Genuinely `entries * legal cards` work, unlike trivial_declarer_play's
/// O(1) rule above -- built to give that comparison a callback with real
/// per-call cost inside it, not to be a good bridge heuristic (it never
/// looks past this one trick).
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
