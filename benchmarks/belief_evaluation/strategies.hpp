#pragma once

// The one declarer/defender strategy pair every fixture, guard, and
// instrument under this directory needs when it does not care which
// strategy is running -- only that the search has somewhere legal to go.
// Deterministic and scripted (no solver, no randomness), matching
// library/tests/belief_evaluation/test_support.hpp's own
// lowest_legal_card/single_card_declarer_play/single_card_defender
// precedent exactly; duplicated rather than reused because that header is
// private to the core C++ test suite (its own comment says so) and a
// ladder belongs beside the instrument that uses it, not folded into it.
//
// A benchmark that needs a *different* strategy (a real double-dummy
// delta, in particular) adds its own alongside this one -- this file is
// the shared default, not the only strategy this directory will ever run.

#include <vector>

#include <api/dds_data_types.hpp>
#include <api/dds_constants.hpp>

#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation::benchmarks
{

/// dds::belief_evaluation::Card, not the global ::Card of the same name --
/// see namespace_collision_test.cpp for the two coexisting, and every
/// caller here (WeightedCard, DeclarerStrategy::play) wants this one.
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

/// A DeclarerStrategy built on scripted_declarer_play(), id 1 -- the only
/// strategy id this directory's own instruments and guards compare
/// against, so a caller never has to invent one.
inline auto scripted_strategy() -> DeclarerStrategy
{
    return DeclarerStrategy{.id = 1, .play = scripted_declarer_play, .state_key = nullptr};
}

}  // namespace dds::belief_evaluation::benchmarks
