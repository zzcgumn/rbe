#include <belief_evaluation/double_dummy_bound.hpp>

#include <api/dds_constants.hpp>
#include <api/solve_board.hpp>
#include <solver_context/solver_context.hpp>

#include <belief_evaluation/trick.hpp>

namespace dds::belief_evaluation
{

namespace
{
    // One more than the most tricks a deal can have (13). See as_bound().
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
        // solutions = 1: a bound needs only the best score. (DoubleDummyDefender
        // uses solutions = 2, because spread() needs every tied candidate.)
        int const status =
            solve_board(ctx_, layout, /*target=*/-1, /*solutions=*/1, /*mode=*/0, &fut);
        if (status != RETURN_NO_FAULT)
        {
            return SolverFailureSentinel;
        }

        // Either side's score must lie in [0, tricks remaining]. Computed once,
        // because the guard below covers both orientations.
        ObservationState state{};
        state.declarer = declarer_;
        state.known_holdings = layout;  // tricks_remaining() reads only declarer's entry
        int const remaining = tricks_remaining(state);

        // The status check misses one case. At solutions = 1, solve_board can
        // return score[0] == -2 ("not evaluated") with RETURN_NO_FAULT. Returned
        // as a bound, -2 would fire the cut on a live node.
        //
        // Never clamp: -2 clamps to 0, and the cut still fires. An out-of-range
        // score is a missing answer, not a number to repair.
        //
        // Which positions do this is not known. See no_search_cases() in
        // double_dummy_bound_test.cpp.
        if (fut.score[0] < 0 || fut.score[0] > remaining)
        {
            // solutions = 3 scored every position measured to fail at
            // solutions = 1. The sentinel prunes nothing, so a second solve on these
            // positions is worth it.
            FutureTricks retry{};
            int const retry_status =
                solve_board(ctx_, layout, /*target=*/-1, /*solutions=*/3, /*mode=*/0, &retry);
            if (retry_status != RETURN_NO_FAULT || retry.score[0] < 0 ||
                retry.score[0] > remaining)
            {
                // Both solves failed. Nothing measured reaches here, but a bound that
                // cannot be computed must read too high, never too low.
                return SolverFailureSentinel;
            }
            fut.score[0] = retry.score[0];
        }

        // score is tricks for the side on lead at `layout` (checked by the
        // orientation tests). With declarer or dummy on lead, it is the bound.
        // With a defender on lead, the bound is the remaining tricks minus score.
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
