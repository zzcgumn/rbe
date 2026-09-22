#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <api/dds_constants.hpp>

#include <belief_evaluation/defender_split.hpp>
#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// Whether a `ConstrainedDecomposition` describes a non-empty space, and if
/// not, which of the three genuinely different ways it is empty. One "the
/// space is empty" value would throw away the diagnosis that tells a
/// self-contradictory history from a root the history simply does not fit.
enum class ConstrainedSpaceStatus
{
    /// The space is well-formed; every other field means what its own
    /// doxygen says.
    Ok,

    /// Both defenders are void in a suit the pool still contains, so a card
    /// of that suit is forced both away from and onto the fixed seat. The
    /// history is self-contradictory, or does not belong to this root.
    ContradictoryVoid,

    /// More cards are forced to the fixed seat than it holds at the root.
    ForcedExceedsFixedSeatCount,

    /// The fixed seat cannot reach its own hand size from what is left once
    /// the forced cards are set aside — the mirror of
    /// `ForcedExceedsFixedSeatCount`.
    InsufficientFreeCards,
};

/// The pool, partitioned by what the two defenders' voids force: cards
/// forced away from the fixed seat, cards forced onto it, and the rest —
/// the cards the history leaves undetermined, over which a constrained
/// enumeration actually varies.
///
/// With no voids at all every card is free, `fixed_seat_needed` equals
/// `pool.fixed_seat_count`, and `constrained_space_size` reproduces the
/// unconstrained `binomial_coefficient(pool.cards.size(),
/// pool.fixed_seat_count)` exactly — a strict generalisation, not a
/// parallel implementation.
///
/// Every field is populated for every status *except* `ContradictoryVoid`:
/// the other two are count problems, diagnosed only once the full partition
/// is known, so their fields stand as the evidence. `ContradictoryVoid` is
/// different in kind — the offending card has no well-defined side — and
/// returns every field at its default rather than a partial answer.
struct ConstrainedDecomposition
{
    ConstrainedSpaceStatus status = ConstrainedSpaceStatus::Ok;

    /// Pool cards a void forces to the fixed seat: the other defender is
    /// void in that card's suit, so it cannot be theirs.
    std::vector<Card> forced_to_fixed_seat;

    /// Pool cards a void forces to the other defender: the fixed seat is
    /// void in that card's suit, so it cannot be theirs.
    std::vector<Card> forced_to_other_seat;

    /// Every pool card neither void forces, in `DefenderPool::cards`'
    /// canonical order — filtered, never re-sorted, so unranking over it
    /// stays deterministic.
    std::vector<Card> free_cards;

    /// How many of `free_cards` the fixed seat still needs:
    /// `pool.fixed_seat_count - forced_to_fixed_seat.size()`. Can be
    /// negative — that is exactly what `ForcedExceedsFixedSeatCount`
    /// reports, and this is the number that says so.
    int fixed_seat_needed = 0;
};

/// Applies `fixed_seat_voids` and `other_seat_voids` (one entry per suit) to
/// `pool`. Takes the two void sets already extracted rather than a
/// `VoidsBySeat` plus seat numbers, so this stays testable on hand-built
/// inputs with no `Deal` or seat-numbering convention in sight; a caller
/// wiring it to `derive_voids` passes `voids[fixed_seat]` and
/// `voids[other_seat]`.
auto decompose_constrained(
    DefenderPool const& pool,
    std::array<bool, DDS_SUITS> const& fixed_seat_voids,
    std::array<bool, DDS_SUITS> const& other_seat_voids) -> ConstrainedDecomposition;

/// `C(free_cards.size(), fixed_seat_needed)` when the status is `Ok`; `0`
/// for every empty-space status. Which kind of empty lives on
/// `decomposition.status`, which the caller still has alongside this value.
auto constrained_space_size(ConstrainedDecomposition const& decomposition) -> std::uint64_t;

}  // namespace dds::belief_evaluation
