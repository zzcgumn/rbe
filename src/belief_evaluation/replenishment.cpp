#include <belief_evaluation/replenishment.hpp>

#include <cassert>
#include <unordered_set>

#include <api/dds_constants.hpp>
#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/layout_key.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/trick.hpp>

namespace dds::belief_evaluation
{

namespace
{
    /// The probability `delta` gives `card`, matching suit and rank. An
    /// omitted card gives 0, the same as an explicit 0. Both reject (see
    /// `ReplayResult`).
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

    // The layout and the state advance together from the same skip, so they
    // cannot disagree about which ply is which.
    //
    // state starts from the root's ObservationState, not from node_state.
    // known_holdings, ranks and tricks_won_by_declarer differ at each depth,
    // and delta may read them. Replaying from the root reproduces the
    // original query exactly.
    Deal working = candidate;
    ObservationState state = root_observation_state(
        root_layout, node_state.declarer, node_state.tricks_needed, node_state.play_record);

    Probability p_j = 1.0;
    for (int i = skip; i < node_state.history.number; ++i)
    {
        Card const card{node_state.history.suit[i], node_state.history.rank[i]};
        int const seat = seat_on_play(working);
        bool const held = (working.remainCards[seat][card.suit] & (1u << card.rank)) != 0;
        bool const is_declarer_or_dummy = seat == node_state.declarer || seat == dummy;

        if (is_declarer_or_dummy)
        {
            // Declarer and dummy cards are common knowledge, so always held here. A
            // failure is an internal bug, not a rejection. Adds nothing to p_j, as
            // in ordinary expansion.
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
    if (wanted == 0)
    {
        // The loop would stop at once, but only after calling source.size() and
        // building already_present. Skip both. This happens at every node when
        // replenish_below is above sample_size.
        return ScanResult{{}, ScanOutcome::SampleFilled, ValidationError::None, -1, Deal{}, 0};
    }

    std::optional<std::uint64_t> const size = source.size();
    // make_root already required source.size(), or this node would not exist.
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
        // wanted is checked before budget, as in make_root.
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
            continue;  // not in this belief space (the cheap check)
        }
        std::uint64_t const key = layout_key(candidate, fixed_seat);
        if (already_present.contains(key))
        {
            continue;  // already in the node; no delta call needed
        }

        ReplayResult const replay = replay_candidate(candidate, root_layout, node.state, delta);
        if (replay.error != ValidationError::None)
        {
            return ScanResult{
                {}, ScanOutcome::SourceExhausted, replay.error, replay.seat, replay.offending_layout, scanned};
        }
        if (! replay.layout.has_value())
        {
            continue;  // did not follow this line
        }
        result.candidates.push_back(ScanCandidate{*replay.layout, replay.p_j, key});
    }

    // Why the scan stopped, as in make_root.
    result.outcome = (i >= *size)
        ? ScanOutcome::SourceExhausted
        : ((result.candidates.size() >= wanted) ? ScanOutcome::SampleFilled : ScanOutcome::BudgetExhausted);
    result.at_calls = scanned;
    return result;
}

}  // namespace dds::belief_evaluation
