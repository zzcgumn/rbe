#pragma once

#include <optional>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// Plays a root-space `candidate` forward along `node_state`'s own played
/// sequence, or rejects it. Mechanical replay only — no probability is
/// accumulated here; see the node-local scan for that.
///
/// The first `history_for(root_layout).number` entries of
/// `node_state.history` are skipped: those cards are already reflected in
/// `candidate`'s own `currentTrick*` fields (a candidate that reached this
/// point already passed `make_root`'s consistency filter, which requires
/// sharing `root_layout`'s current-trick state exactly), so replaying them
/// from index 0 would play them a second time — producing a `Deal` that
/// looks like a legal position and is wrong. This is the same prefix
/// `make_root` itself skips when seeding `ObservationState::history`, and
/// both go through `history_for` so the two can never disagree about where
/// it ends.
///
/// At each remaining entry, the seat is derived via `seat_on_play` on the
/// candidate *as replayed so far* — never read from `node_state.first`,
/// which is the root's own leader and is unrelated once play has moved on
/// (see `ObservationState`'s own doxygen on `tricks_remaining` for the same
/// point made about a different field). A declarer or dummy card is always
/// legal in `candidate` — those holdings are common knowledge and
/// bit-identical across every consistent layout — and is asserted rather
/// than checked, matching this module's convention of asserting on an
/// internal invariant and reporting on user-shaped input. A **defender**
/// card `candidate` does not hold at that seat means `candidate` did not
/// follow this line: an ordinary negative result, returned as
/// `std::nullopt`, not an error and not an assert.
///
/// Returns `candidate` played forward to `node_state`'s own depth on
/// success. A replayed candidate that is already one of the node's own
/// drawn layouts (fed back through this function) produces a `Deal` equal
/// to that entry's, bit for bit.
auto replay_candidate(Deal const& candidate, Deal const& root_layout, ObservationState const& node_state)
    -> std::optional<Deal>;

}  // namespace dds::belief_evaluation
