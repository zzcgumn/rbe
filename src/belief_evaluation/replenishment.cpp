#include <belief_evaluation/replenishment.hpp>

#include <cassert>
#include <unordered_set>

#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/layout_key.hpp>
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
                return ReplayResult{std::nullopt, 0.0, ValidationError::None, -1, Deal{}};
            }

            DefenderQuery const query{working, seat, state};
            std::vector<WeightedCard> const distribution = delta(query);
            if (distribution.empty())
            {
                return ReplayResult{std::nullopt, 0.0, ValidationError::DistributionEmpty, seat, working};
            }
            ValidationError const contract_error =
                validate_defender_distribution(working, seat, distribution);
            if (contract_error != ValidationError::None)
            {
                return ReplayResult{std::nullopt, 0.0, contract_error, seat, working};
            }

            Probability const probability = probability_of(distribution, card);
            if (probability <= 0.0)
            {
                return ReplayResult{std::nullopt, 0.0, ValidationError::None, -1, Deal{}};
            }
            p_j *= probability;
        }

        working = play(working, card);
        state = advance_state(state, card);
    }
    return ReplayResult{working, p_j, ValidationError::None, -1, Deal{}};
}

auto scan_for_replenishment(
    BeliefNode const& node,
    Deal const& root_layout,
    LayoutSource const& source,
    DefenderStrategy const& delta,
    std::uint64_t wanted,
    std::optional<std::uint64_t> budget) -> ScanResult
{
    std::optional<std::uint64_t> const size = source.size();
    // Guaranteed: this node could not exist at all unless some earlier
    // make_root call already required source.size() to be present -- a
    // node-local scan is never the first thing to ask.
    assert(size.has_value());

    int const declarer = node.state.declarer;
    int const dummy = (declarer + 2) % DDS_HANDS;
    int const fixed_seat = (declarer + 1) % DDS_HANDS;

    std::unordered_set<std::uint64_t> const already_present(node.root_keys.begin(), node.root_keys.end());

    ScanResult result;
    std::uint64_t scanned = 0;
    std::uint64_t i = 0;
    for (; i < *size; ++i)
    {
        // wanted checked before budget at every step, mirroring make_root's
        // own tie-break: a candidate that fills the want is never charged
        // against the budget.
        if (result.candidates.size() >= wanted)
        {
            break;
        }
        if (budget.has_value() && scanned >= *budget)
        {
            break;
        }
        Deal const candidate = source.at(i);
        ++scanned;

        if (! is_consistent(candidate, root_layout, declarer, dummy))
        {
            continue;  // not in this belief space at all -- the cheap check, tried first
        }
        std::uint64_t const key = layout_key(candidate, fixed_seat);
        if (already_present.contains(key))
        {
            continue;  // already in the node -- no delta call spent finding that out
        }

        ReplayResult const replay = replay_candidate(candidate, root_layout, node.state, delta);
        if (replay.error != ValidationError::None)
        {
            return ScanResult{{}, ScanOutcome::SourceExhausted, replay.error, replay.seat, replay.offending_layout};
        }
        if (! replay.layout.has_value())
        {
            continue;  // did not follow this line -- the ordinary rejection
        }
        result.candidates.push_back(ScanCandidate{*replay.layout, replay.p_j, key});
    }

    // Why the scan stopped, mirroring make_root's own derivation: the
    // source ran out (i reached *size, whatever either cap was), else
    // whichever cap actually bound, wanted taking priority to match the
    // loop's own check order above.
    result.outcome = (i >= *size)
        ? ScanOutcome::SourceExhausted
        : ((result.candidates.size() >= wanted) ? ScanOutcome::SampleFilled : ScanOutcome::BudgetExhausted);
    return result;
}

}  // namespace dds::belief_evaluation
