#pragma once

#include <cassert>
#include <functional>
#include <optional>
#include <vector>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/spread.hpp>
#include <belief_evaluation/trick.hpp>
#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// Everything a heuristic reads. Built once per HeuristicDefender call from
/// the (layout, state, seat) of a DefenderStrategy call, plus the
/// FutureTricks that call solved.
///
/// `layout.trump`: 0 = Spades, 1 = Hearts, 2 = Diamonds, 3 = Clubs,
/// 4 (DDS_NOTRUMP) = no trump. Checked against the tests: the two doxygen
/// comments on `::Deal` disagree.
struct DefenderHeuristicContext
{
    Deal const& layout;
    ObservationState const& state;

    /// The solve that produced the candidate set (`best_cards`, the second
    /// argument of every DefenderHeuristic). `fut.equals` gives exact
    /// touching groups. See `touching_group.hpp` for the precondition that
    /// links `fut` and `best_cards`.
    FutureTricks const& fut;

    int seat;               ///< == seat_on_play(layout)
    int position_in_trick;  ///< 0 = lead, 1, 2, 3
    int on_lead_to_trick;   ///< layout.first
    bool was_on_lead;       ///< position_in_trick == 0
    bool can_follow_led_suit;
    bool defending_side;  ///< seat % 2 != state.declarer % 2
    int trump;             ///< layout.trump; see above

    /// The policy `HeuristicDefender` falls back to when every rule
    /// defers. `SpreadPolicy::TouchingSequence` by default.
    ///
    /// **A rule that defers to get restricted choice must check this
    /// first** (see `second_seat_low`). Under `SpreadPolicy::AllOptimal`
    /// the fallback spreads over every tied candidate in every suit, not
    /// over one touching group.
    SpreadPolicy fallback_policy;
};

/// One rule in a defender heuristic chain. Returns the chosen card, or
/// std::nullopt to defer to the next rule. After the last rule,
/// HeuristicDefender uses its fallback spread.
///
/// A plain callable, so that a caller's rule and a shipped rule are the
/// same kind of value. DefenderHeuristicChain::add() takes either, in any
/// order, from C++ or Python.
using DefenderHeuristic =
    std::function<std::optional<Card>(DefenderHeuristicContext const&, std::vector<Card> const&)>;

/// Builds the context for DefenderHeuristic calls. `seat` must be the seat
/// on play at `layout` (asserted).
///
/// **Pass the same `fallback_policy` as the `HeuristicDefender` uses.**
/// Otherwise `DefenderHeuristicContext::fallback_policy` describes the
/// wrong fallback.
inline auto make_defender_heuristic_context(
    Deal const& layout, ObservationState const& state, int seat, FutureTricks const& fut,
    SpreadPolicy fallback_policy = SpreadPolicy::TouchingSequence)
    -> DefenderHeuristicContext
{
    assert(seat == seat_on_play(layout));

    // seat_on_play(layout) == (layout.first + played_count) % DDS_HANDS,
    // so the trick position follows from seat and layout.first. This
    // avoids copying the private played_count() of trick.cpp.
    int const position_in_trick = (seat - layout.first + DDS_HANDS) % DDS_HANDS;
    bool const was_on_lead = (position_in_trick == 0);

    int const led_suit = layout.currentTrickSuit[0];
    bool const can_follow_led_suit =
        !was_on_lead && (layout.remainCards[seat][led_suit] != 0);

    bool const defending_side = (seat % 2) != (state.declarer % 2);

    return DefenderHeuristicContext{
        layout,
        state,
        fut,
        seat,
        position_in_trick,
        layout.first,
        was_on_lead,
        can_follow_led_suit,
        defending_side,
        layout.trump,
        fallback_policy,
    };
}

}  // namespace dds::belief_evaluation
