#include <belief_evaluation/replenishment.hpp>

#include <cassert>

#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/trick.hpp>
#include <utility/constants.h>

namespace dds::belief_evaluation
{

namespace
{
    /// `delta`'s own probability for `card` in `distribution`, matching on
    /// suit and rank. An omitted card and one explicitly given probability
    /// 0 are indistinguishable here on purpose -- see `ReplayResult`'s own
    /// doxygen for why both mean "this candidate did not follow the line".
    auto probability_of(std::vector<WeightedCard> const& distribution, Card const& card) -> Probability
    {
        for (WeightedCard const& entry : distribution)
        {
            if (entry.card.suit == card.suit && entry.card.rank == card.rank)
            {
                return entry.probability;
            }
        }
        return 0.0;
    }
}

auto replay_candidate(
    Deal const& candidate,
    Deal const& root_layout,
    ObservationState const& node_state,
    DefenderStrategy const& delta) -> ReplayResult
{
    int const skip = history_for(root_layout).number;
    int const dummy = (node_state.declarer + 2) % DDS_HANDS;

    // Replayed in lockstep with the layout, both starting from exactly the
    // same skip: two passes over the same history is where an off-by-one
    // would get the layout and the state disagreeing about which ply is
    // which.
    //
    // state starts from the root's own ObservationState -- not from
    // node_state with its history truncated -- because known_holdings,
    // ranks and tricks_won_by_declarer at depth `skip` are genuinely
    // different values from node_state's own (which reflect the node's
    // full depth), and delta may read any of them. Only trump, first,
    // declarer and tricks_needed are unaffected by advance_state and so are
    // already identical between the root and node_state; the rest must be
    // rebuilt from the root to reproduce the exact query the original
    // expansion made.
    Deal working = candidate;
    ObservationState state =
        root_observation_state(root_layout, node_state.declarer, node_state.tricks_needed);

    Probability p_j = 1.0;
    for (int i = skip; i < node_state.history.number; ++i)
    {
        Card const card{node_state.history.suit[i], node_state.history.rank[i]};
        int const seat = seat_on_play(working);
        bool const held = (working.remainCards[seat][card.suit] & (1u << card.rank)) != 0;
        bool const is_declarer_or_dummy = seat == node_state.declarer || seat == dummy;

        if (is_declarer_or_dummy)
        {
            // Common knowledge, identical across every consistent layout --
            // always legal in candidate by construction. An internal
            // invariant failure, not a rejection; see ReplayResult's own
            // doxygen for why the two cases are handled differently.
            // Contributes nothing to p_j, matching how p is built during
            // ordinary expansion.
            assert(held);
        }
        else
        {
            if (! held)
            {
                return ReplayResult{std::nullopt, 0.0, ValidationError::None, -1};
            }

            DefenderQuery const query{working, seat, state};
            std::vector<WeightedCard> const distribution = delta(query);
            if (distribution.empty())
            {
                return ReplayResult{std::nullopt, 0.0, ValidationError::DistributionEmpty, seat};
            }
            ValidationError const contract_error =
                validate_defender_distribution(working, seat, distribution);
            if (contract_error != ValidationError::None)
            {
                return ReplayResult{std::nullopt, 0.0, contract_error, seat};
            }

            Probability const probability = probability_of(distribution, card);
            if (probability <= 0.0)
            {
                return ReplayResult{std::nullopt, 0.0, ValidationError::None, -1};
            }
            p_j *= probability;
        }

        working = play(working, card);
        state = advance_state(state, card);
    }
    return ReplayResult{working, p_j, ValidationError::None, -1};
}

}  // namespace dds::belief_evaluation
