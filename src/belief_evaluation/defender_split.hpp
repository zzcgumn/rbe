#pragma once

#include <cstdint>
#include <vector>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// The outstanding defender cards at a root layout, flattened into one
/// ordered sequence across all four suits, and how many of them the fixed
/// seat -- `(declarer + 1) % DDS_HANDS`, the same seat `BeliefNode::root_keys`
/// and `scan_for_replenishment` fix on -- holds there. This is the whole
/// input a defender-split enumeration is a function of: everything else
/// `is_consistent` compares (trump, `first`, the current trick, declarer's
/// and dummy's holdings) is fixed by the root, and the only freedom left is
/// which of these pooled cards each defender holds.
///
/// `cards` is every card either defender holds at the root: the union,
/// suit by suit, of both defenders' `remainCards` -- equivalent to
/// `node.cpp`'s own (unexported) `defender_pool()` called once per suit and
/// flattened into individual `Card`s, and verified against it directly in
/// this module's own tests.
struct DefenderPool
{
    /// Canonically ordered: suits ascending (0=S, 1=H, 2=D, 3=C, matching
    /// `Card::suit`'s own convention), and within a suit, ranks ascending
    /// (2 low .. 14 high -- the same direction `Deal::remainCards`' bit
    /// position already orders them in, bit *r* for absolute rank *r*, with
    /// no shift). This order is load-bearing: a caller mapping an index to
    /// a position in this sequence relies on two decompositions of the same
    /// root producing the same order every time, which is what makes
    /// `LayoutSource::at` deterministic once built on top of this. Do not
    /// change the direction once anything depends on it.
    std::vector<Card> cards;

    /// How many of `cards` the fixed seat holds at the root this was built
    /// from. Read directly from the root layout via `std::popcount` across
    /// its four `remainCards` entries, never derived as half of
    /// `cards.size()`: mid-trick, the two defenders' remaining counts can
    /// legitimately differ by one (one defender has already played to the
    /// trick in progress), and a symmetric assumption gets that case wrong
    /// while still producing a plausible-looking, non-empty result.
    int fixed_seat_count = 0;
};

/// Builds `root`'s `DefenderPool` for `declarer`. An empty pool, or a
/// `fixed_seat_count` of 0 or of the whole pool, are legal inputs -- both
/// describe a root with only one legal defender split -- and are answered
/// cleanly rather than asserted against.
auto defender_pool_decomposition(Deal const& root, int declarer) -> DefenderPool;

}  // namespace dds::belief_evaluation
