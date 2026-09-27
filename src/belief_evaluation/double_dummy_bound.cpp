#include <belief_evaluation/double_dummy_bound.hpp>

#include <api/dds_constants.hpp>
#include <api/solve_board.hpp>
#include <solver_context/solver_context.hpp>

#include <belief_evaluation/trick.hpp>

namespace dds::belief_evaluation
{

namespace
{
    // One more than the most tricks any deal could ever have (13) -- see
    // as_bound()'s own doxygen for why too high, not too low.
    constexpr int SolverFailureSentinel = 14;
}

DoubleDummyBound::DoubleDummyBound(SolverContext& ctx, int declarer) : ctx_(ctx), declarer_(declarer)
{
}

auto DoubleDummyBound::as_bound() -> LayoutBound
{
    return [this](Deal const& layout) -> int
    {
        FutureTricks fut{};
        // solutions = 1: only the single best candidate's score is needed
        // here, unlike DoubleDummyDefender's solutions = 2 (which needs
        // every score-tied candidate for spread() to divide probability
        // over). A bound reads only the maximum.
        int const status =
            solve_board(ctx_, layout, /*target=*/-1, /*solutions=*/1, /*mode=*/0, &fut);
        if (status != RETURN_NO_FAULT)
        {
            return SolverFailureSentinel;
        }

        // Both orientations below read score[0] as a trick count taken by
        // one side of a zero-sum split, so the range it has to lie in is
        // the same for either: no side can take more tricks than remain,
        // and none can take fewer than none. Computed once here, before
        // the split, because the guard below applies to both.
        ObservationState state{};
        state.declarer = declarer_;
        state.known_holdings = layout;  // a raw layout Deal has every hand's exact holding, which is
                                        // more than tricks_remaining() needs (it only reads
                                        // declarer's own entry) but never less
        int const remaining = tricks_remaining(state);

        // The status guard above does not catch everything solve_board can
        // answer with. At solutions = 1 it can return score[0] == -2 --
        // "not evaluated" rather than a trick count -- carrying
        // RETURN_NO_FAULT, and on the declarer-on-lead branch that -2 would
        // be returned as a bound directly: negative, hence below any
        // tricks_needed, firing the caller's cut on a live node.
        //
        // Deliberately never a clamp: clamping -2 into range gives 0, and 0
        // is below every tricks_needed >= 1, so the cut would still fire
        // while the bug looked fixed. An out-of-range score is not a number
        // to repair; it is the absence of an answer.
        //
        // Which positions answer this way is still not established, and the
        // retry below sidesteps that question rather than answering it. See
        // double_dummy_bound_test.cpp's no_search_cases() for the measured
        // table and what it refutes.
        if (fut.score[0] < 0 || fut.score[0] > remaining)
        {
            // solutions = 3 scores every position measured to decline at
            // solutions = 1, so ask again before giving up: the sentinel is
            // sound but prunes nothing, and on a position the cut would
            // otherwise catch that is a real loss. Costs a second solve on
            // exactly the positions that would otherwise contribute no
            // pruning at all.
            FutureTricks retry{};
            int const retry_status =
                solve_board(ctx_, layout, /*target=*/-1, /*solutions=*/3, /*mode=*/0, &retry);
            if (retry_status != RETURN_NO_FAULT || retry.score[0] < 0 ||
                retry.score[0] > remaining)
            {
                // Both solves declined. The sentinel is the floor of the
                // design and stays for this case, even though nothing
                // measured reaches it: a bound that cannot be computed must
                // read as too high, never too low.
                return SolverFailureSentinel;
            }
            fut.score[0] = retry.score[0];
        }

        // score is tricks for whichever side is on lead at `layout` --
        // verified empirically, not assumed (see
        // double_dummy_bound_test.cpp's orientation tests). When declarer
        // or dummy is on lead, that already is declarer's own bound; when
        // a defender is on lead, score is the *defenders'* own best trick
        // count, and declarer's bound is the remainder -- bridge is
        // zero-sum trick by trick, so every remaining trick goes to
        // exactly one side.
        int const dummy = (declarer_ + 2) % DDS_HANDS;
        int const seat = seat_on_play(layout);
        bool const declarer_on_lead = seat == declarer_ || seat == dummy;
        if (declarer_on_lead)
        {
            return fut.score[0];
        }

        return remaining - fut.score[0];
    };
}

}  // namespace dds::belief_evaluation
