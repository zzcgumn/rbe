#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <utility/constants.h>

#include <belief_evaluation/defender_split.hpp>
#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// Whether a `ConstrainedDecomposition` describes a non-empty space, and if
/// not, which of the three genuinely different ways it can be empty. Lumping
/// them into one "the space is empty" would throw away the diagnosis a
/// caller needs to tell a self-contradictory history from a root the
/// history simply does not fit.
enum class ConstrainedSpaceStatus
{
    /// The space is well-formed; `forced_to_fixed_seat`, `forced_to_other_seat`,
    /// `free_cards` and `fixed_seat_needed` all mean what their own doxygen
    /// says.
    Ok,

    /// Both defenders are void in a suit the pool still contains: a pool
    /// card of that suit is simultaneously forced away from the fixed seat
    /// (the fixed seat is void) and forced to it (the other seat is void) --
    /// unresolvable. The history is self-contradictory, or does not belong
    /// to this root.
    ContradictoryVoid,

    /// More cards are forced to the fixed seat than it holds at the root
    /// (`forced_to_fixed_seat.size() > pool.fixed_seat_count`). Again a
    /// contradiction between the history and the root, not a shape the
    /// fixed seat's own hand size can satisfy.
    ForcedExceedsFixedSeatCount,

    /// The fixed seat cannot reach its own hand size from what is left
    /// once the forced cards are set aside (`fixed_seat_needed >
    /// free_cards.size()`) -- the mirror of `ForcedExceedsFixedSeatCount`.
    InsufficientFreeCards,
};

/// The pool, partitioned by what the two defenders' void sets force: cards
/// a suit-void forces away from the fixed seat (`forced_to_other_seat`),
/// cards a suit-void forces onto it (`forced_to_fixed_seat`), and
/// everything else (`free_cards`) -- the cards this root's history leaves
/// genuinely undetermined, over which a constrained enumeration actually
/// varies. `fixed_seat_needed` is how many of `free_cards` the fixed seat
/// still needs, after the forced cards already account for some of its
/// hand size.
///
/// With no voids at all, every pool card is free, `fixed_seat_needed`
/// equals `pool.fixed_seat_count`, and `constrained_space_size` below
/// reproduces the existing unconstrained `binomial_coefficient(pool.cards.size(),
/// pool.fixed_seat_count)` exactly -- a strict generalisation, not a
/// parallel implementation (see this module's own tests for the direct
/// comparison).
///
/// Every field below is populated for every status *except*
/// `ContradictoryVoid`: `ForcedExceedsFixedSeatCount` and
/// `InsufficientFreeCards` are both count problems, diagnosed only once the
/// full partition and `fixed_seat_needed` are known, so both are left
/// populated as the evidence for why that status was returned.
/// `ContradictoryVoid` is different in kind -- the card that triggers it has
/// no well-defined side to be on at all -- so it returns every field below
/// at its default (empty, zero) rather than a partial classification.
struct ConstrainedDecomposition
{
    ConstrainedSpaceStatus status = ConstrainedSpaceStatus::Ok;

    /// Pool cards a void forces to the fixed seat: the other defender is
    /// void in that card's suit, so it cannot be theirs.
    std::vector<Card> forced_to_fixed_seat;

    /// Pool cards a void forces to the other defender: the fixed seat is
    /// void in that card's suit, so it cannot be theirs.
    std::vector<Card> forced_to_other_seat;

    /// Every pool card neither void forces -- in the same canonical order
    /// `DefenderPool::cards` already carries (suits ascending, ranks
    /// ascending within a suit), filtered rather than re-sorted, so that
    /// unranking over it stays deterministic across calls with the same
    /// inputs. See `DefenderPool::cards`' own doxygen for why that order is
    /// load-bearing.
    std::vector<Card> free_cards;

    /// How many of `free_cards` the fixed seat still needs:
    /// `pool.fixed_seat_count - forced_to_fixed_seat.size()`. Can be
    /// negative -- that is exactly what `ForcedExceedsFixedSeatCount`
    /// reports, and this is the number that says so.
    int fixed_seat_needed = 0;
};

/// Applies `fixed_seat_voids` and `other_seat_voids` (one entry per suit,
/// `true` where that defender has shown void) to `pool`, exactly as
/// `defender_pool_decomposition` already produces it. Takes the two void
/// sets already extracted from a seat -> suit-set derivation such as
/// `derive_voids` rather than that derivation's own output plus seat
/// numbers, so this stays testable on hand-built inputs with no `Deal`,
/// `LayoutSource` or seat-numbering convention in sight -- a caller wiring
/// this to `derive_voids`'s `VoidsBySeat` passes `voids[fixed_seat]` and
/// `voids[other_seat]` directly.
auto decompose_constrained(
    DefenderPool const& pool,
    std::array<bool, DDS_SUITS> const& fixed_seat_voids,
    std::array<bool, DDS_SUITS> const& other_seat_voids) -> ConstrainedDecomposition;

/// `C(free_cards.size(), fixed_seat_needed)` when `decomposition.status ==
/// Ok`; `0` for every empty-space status. The *distinction* between why the
/// space is empty lives on `decomposition.status` itself, not here --
/// collapsing three different causes into the same `0` is exactly what
/// `ConstrainedSpaceStatus` exists to avoid, and this function's caller
/// still has the status available alongside this value.
auto constrained_space_size(ConstrainedDecomposition const& decomposition) -> std::uint64_t;

}  // namespace dds::belief_evaluation
