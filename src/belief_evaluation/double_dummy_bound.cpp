#include <belief_evaluation/double_dummy_bound.hpp>

// api/dds.h -- pulled in transitively by solver_context.hpp and by
// api/solve_board.hpp -- declares its own unrelated struct Card that
// collides with belief_evaluation::Card the moment both are visible in one
// translation unit. See double_dummy_defender.cpp's own top-of-file
// comment for the full explanation; the same trick applies here, and
// these must be the first inclusion of solver_context.hpp / api/dds.h in
// this translation unit for the rename to take effect (double_dummy_
// bound.hpp forward-declares SolverContext for exactly this reason).
#define Card DdsInternalCard
#include <api/solve_board.hpp>
#include <solver_context/solver_context.hpp>
#undef Card

#include <belief_evaluation/trick.hpp>
#include <utility/constants.h>

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

        // solve_board's score is tricks for whichever side is on lead at
        // `layout` -- verified empirically, not assumed (see
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

        ObservationState temp{};
        temp.declarer = declarer_;
        temp.known_holdings = layout;  // a raw layout Deal has every hand's exact holding, which is
                                        // more than tricks_remaining() needs (it only reads
                                        // declarer's own entry) but never less
        return tricks_remaining(temp) - fut.score[0];
    };
}
