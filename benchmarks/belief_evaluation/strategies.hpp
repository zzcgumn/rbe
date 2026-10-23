#pragma once

// The declarer and defender strategy pair every fixture, guard and
// instrument here uses when it needs only a legal move, not a particular
// strategy. Deterministic and scripted: no solver, no randomness. It
// copies lowest_legal_card, single_card_declarer_play and
// single_card_defender from tests/belief_evaluation/test_support.hpp,
// which is private to the core C++ tests.
//
// A benchmark that needs another strategy, such as a real double-dummy
// delta, adds it beside this one. This is the shared default.

#include <vector>

#include <api/dds_data_types.hpp>
#include <api/dds_constants.hpp>

#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation::benchmarks
{

/// dds::belief_evaluation::Card, not the global ::Card of the same name
/// (see namespace_collision_test.cpp). Every caller here (WeightedCard,
/// DeclarerStrategy::play) wants this one.
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

inline auto seat_on_play(Deal const& deal) -> int
{
    int played = 0;
    for (int i = 0; i < 3 && deal.currentTrickRank[i] != 0; ++i)
    {
        ++played;
    }
    return (deal.first + played) % DDS_HANDS;
}

inline auto scripted_declarer_play(ObservationState const& state, BeliefView const&) -> Card
{
    return lowest_legal_card(state.known_holdings, seat_on_play(state.known_holdings));
}

inline auto scripted_defender_play(DefenderQuery const& query) -> std::vector<WeightedCard>
{
    return {WeightedCard{lowest_legal_card(query.layout, query.seat), 1.0}};
}

/// A DeclarerStrategy built on scripted_declarer_play(), with id 1: the
/// only strategy id the instruments and guards here compare against.
inline auto scripted_strategy() -> DeclarerStrategy
{
    return DeclarerStrategy{.id = 1, .play = scripted_declarer_play, .state_key = nullptr};
}

}  // namespace dds::belief_evaluation::benchmarks
