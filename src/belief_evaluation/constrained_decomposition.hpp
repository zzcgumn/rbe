#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <api/dds_constants.hpp>

#include <belief_evaluation/defender_split.hpp>
#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// Whether a `ConstrainedDecomposition` has a non-empty space and, if not,
/// which of three reasons makes it empty. Each reason needs a different
/// fix.
enum class ConstrainedSpaceStatus
{
    /// The space is well-formed.
    Ok,

    /// Both defenders are void in a suit the pool still contains. The
    /// history contradicts itself, or does not belong to this root.
    ContradictoryVoid,

    /// More cards are forced to the fixed seat than it holds at the root.
    ForcedExceedsFixedSeatCount,

    /// Too few cards are left for the fixed seat to reach its hand size.
    /// The mirror of `ForcedExceedsFixedSeatCount`.
    InsufficientFreeCards,
};

/// The pool, split by what the voids force: cards forced away from the
/// fixed seat, cards forced onto it, and the free cards. A constrained
/// enumeration varies only the free cards.
///
/// With no voids, every card is free and `constrained_space_size` equals
/// `binomial_coefficient(pool.cards.size(), pool.fixed_seat_count)`.
///
/// Every field is filled for every status *except* `ContradictoryVoid`.
/// That status returns every field at its default.
struct ConstrainedDecomposition
{
    ConstrainedSpaceStatus status = ConstrainedSpaceStatus::Ok;

    /// Pool cards forced to the fixed seat: the other defender is void in
    /// the suit.
    std::vector<Card> forced_to_fixed_seat;

    /// Pool cards forced to the other defender: the fixed seat is void in
    /// the suit.
    std::vector<Card> forced_to_other_seat;

    /// The pool cards no void forces, in the order of
    /// `DefenderPool::cards`. Filtered, never re-sorted, so unranking stays
    /// deterministic.
    std::vector<Card> free_cards;

    /// How many of `free_cards` the fixed seat still needs:
    /// `pool.fixed_seat_count - forced_to_fixed_seat.size()`. Negative for
    /// `ForcedExceedsFixedSeatCount`.
    int fixed_seat_needed = 0;
};

/// Applies the voids of each seat (one entry per suit) to `pool`. A caller
/// using `derive_voids` passes `voids[fixed_seat]` and `voids[other_seat]`.
auto decompose_constrained(
    DefenderPool const& pool,
    std::array<bool, DDS_SUITS> const& fixed_seat_voids,
    std::array<bool, DDS_SUITS> const& other_seat_voids) -> ConstrainedDecomposition;

/// `C(free_cards.size(), fixed_seat_needed)` when the status is `Ok`.
/// Otherwise `0`; `decomposition.status` gives the reason.
auto constrained_space_size(ConstrainedDecomposition const& decomposition) -> std::uint64_t;

}  // namespace dds::belief_evaluation
