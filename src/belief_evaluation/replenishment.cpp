#include <belief_evaluation/replenishment.hpp>

#include <cassert>

#include <belief_evaluation/node.hpp>
#include <belief_evaluation/trick.hpp>
#include <utility/constants.h>

namespace dds::belief_evaluation
{

auto replay_candidate(Deal const& candidate, Deal const& root_layout, ObservationState const& node_state)
    -> std::optional<Deal>
{
    int const skip = history_for(root_layout).number;

    Deal working = candidate;
    for (int i = skip; i < node_state.history.number; ++i)
    {
        Card const card{node_state.history.suit[i], node_state.history.rank[i]};
        int const seat = seat_on_play(working);
        bool const held = (working.remainCards[seat][card.suit] & (1u << card.rank)) != 0;

        bool const is_declarer_or_dummy =
            seat == node_state.declarer || seat == (node_state.declarer + 2) % DDS_HANDS;
        if (is_declarer_or_dummy)
        {
            // Common knowledge, identical across every consistent layout --
            // always legal in candidate by construction. An internal
            // invariant failure, not a rejection; see this function's own
            // doxygen for why the two cases are handled differently.
            assert(held);
        }
        else if (! held)
        {
            return std::nullopt;  // candidate did not follow this line -- the filter, not an error
        }

        working = play(working, card);
    }
    return working;
}

}  // namespace dds::belief_evaluation
