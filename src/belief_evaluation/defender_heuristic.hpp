#pragma once

#include <cassert>
#include <functional>
#include <optional>
#include <vector>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/trick.hpp>
#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// All state a heuristic needs, assembled once per HeuristicDefender call
/// from the same (layout, state, seat) a DefenderStrategy is always given,
/// plus the FutureTricks that same call already solved.
///
/// `layout.trump`'s convention (confirmed against DDS_NOTRUMP and this
/// module's own existing tests, not from either doxygen comment on the raw
/// `::Deal` struct, which disagree with each other): 0 = Spades, 1 =
/// Hearts, 2 = Diamonds, 3 = Clubs, 4 (DDS_NOTRUMP) = no trump.
struct DefenderHeuristicContext
{
    Deal const& layout;
    ObservationState const& state;

    /// The same solve the candidate set (`best_cards`, passed alongside
    /// this context to every DefenderHeuristic call) came from. Carries
    /// exact touching-group membership (`fut.equals`) -- see
    /// `second_seat_low`'s own doxygen for the one rule that reads it.
    FutureTricks const& fut;

    int seat;               ///< == seat_on_play(layout)
    int position_in_trick;  ///< 0 = lead, 1, 2, 3
    int on_lead_to_trick;   ///< layout.first
    bool was_on_lead;       ///< position_in_trick == 0
    bool can_follow_led_suit;
    bool defending_side;  ///< seat % 2 != state.declarer % 2
    int trump;             ///< layout.trump; see this struct's own doxygen above
};

/// One rule in a defender heuristic chain. Returns the chosen card, or
/// std::nullopt to defer to the next rule -- or, if this is the chain's
/// last rule, to HeuristicDefender's own fallback spread. A plain
/// callable, not an interface: a caller's own rule and a rule this module
/// ships are, structurally, the identical kind of value, which is what
/// lets DefenderHeuristicChain::add() compose them on equal footing in any
/// order, including from Python with no subclassing or trampoline class
/// (mirroring DefenderStrategy and DeclarerStrategy, both already plain
/// callables for the same reason).
using DefenderHeuristic =
    std::function<std::optional<Card>(DefenderHeuristicContext const&, std::vector<Card> const&)>;

/// Builds the context every DefenderHeuristic call reads. `seat` must be
/// the seat actually on play at `layout` -- a DefenderStrategy is only
/// ever invoked for that seat, so this is an invariant to assert, not a
/// case to handle.
inline auto make_defender_heuristic_context(
    Deal const& layout, ObservationState const& state, int seat, FutureTricks const& fut)
    -> DefenderHeuristicContext
{
    assert(seat == seat_on_play(layout));

    // seat_on_play(layout) == (layout.first + played_count) % DDS_HANDS,
    // so played_count -- the trick position -- is recoverable from seat
    // and layout.first alone, without a second, separately-maintained copy
    // of trick.cpp's own played_count() (which is file-local there).
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
    };
}

}  // namespace dds::belief_evaluation
