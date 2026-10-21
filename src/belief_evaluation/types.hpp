#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/play_record.hpp>

namespace dds::belief_evaluation
{

/// A single card, for callback arguments and returns.
///
/// `api/dds.h` has an unrelated `::Card` at global scope with the same
/// layout but a different rank convention. The namespaces keep them apart.
/// `namespace_collision_test.cpp` uses both.
struct Card
{
    int suit;  ///< 0=S, 1=H, 2=D, 3=C
    int rank;  ///< 2..14
};

/// Identifies a declarer strategy among strategies compared together. Only
/// checked for distinctness.
using StrategyId = std::uint32_t;

/// An opaque byte string. It declares what `play` reads beyond the
/// position. Equal keys promise the same future play from the same
/// position. See `DeclarerStrategy::state_key`.
using StateKey = std::string;

/// p: the probability that the defenders played a card sequence in one
/// layout. In a BeliefEntry, a normalised posterior. A separate alias from
/// SampleWeight, as in the notation of algorithm.md.
using Probability = double;

/// kappa: the sample weight along a search path. 1/M at the root of a
/// sampled evaluation.
using SampleWeight = double;

/// The outstanding cards of one node, and the map between absolute and
/// relative ranks. The same in every layout of the node. See rank_map.hpp.
struct RankMap
{
    /// Every card still in **any** of the four hands, per suit, excluding
    /// the trick in progress. **Not only the defenders' cards.**
    /// `DefenderPool` (defender_split.hpp) uses "outstanding" for the
    /// defenders' cards alone.
    ///
    /// Bit `r - 2` for absolute rank `r`. `Deal::remainCards` uses bit `r`.
    /// Only `to_compacted` in `trick.hpp` converts between them. A stray
    /// `>> 2` anywhere else is a bug.
    std::array<unsigned, DDS_SUITS> aggr;

    /// Absolute rank to relative rank, 1 = highest. 0 if the card is not
    /// outstanding. Also 0 for a `suit` outside `0..DDS_SUITS` or a `rank`
    /// outside `2..14`.
    auto to_relative(int suit, int rank) const -> int;

    /// Relative rank (1 = highest) to absolute rank. 0 for a `suit` outside
    /// `0..DDS_SUITS` or an `ordinal` outside `1..13`.
    auto to_absolute(int suit, int ordinal) const -> int;
};

/// The common knowledge at a node: the same in every layout. A declarer
/// strategy may read it directly.
struct ObservationState
{
    int trump;
    int first;                  ///< seat on lead at the root
    PlayTraceBin history;       ///< every card played since the root, in order,
                                 ///< including cards already in the root's trick.
                                 ///< Never cards from earlier tricks: a Deal keeps
                                 ///< no record of them. See play_record.
    int declarer;                ///< seat; dummy is (declarer + 2) % 4
    int tricks_needed;           ///< tricks still required to make the contract
    int tricks_won_by_declarer;
    Deal known_holdings;         ///< declarer + dummy exact; defender entries are the union pool
    RankMap ranks;

    /// Every card played before the root. Fixed for the whole evaluation,
    /// unlike `history`. Null when the caller supplied no `PlayRecord`.
    ///
    /// **Not owned.** It points into the `EvaluateOptions` passed to
    /// `evaluate()`, which must outlive every `ObservationState` the
    /// evaluation builds. `EvaluationValue::retained_root` outlives the
    /// call, so `evaluate()` sets this to null there.
    PlayRecord const* play_record = nullptr;

    /// Set only by the Python binding. Empty for every C++ caller.
    ///
    /// The binding builds `EvaluateOptions` for one call. A Python strategy
    /// that keeps `state` after the call would read freed memory through
    /// `play_record`. So: empty means no guard. False means the call has
    /// returned, and the Python `.play_record` property raises.
    std::shared_ptr<bool> play_record_valid;
};

/// One layout in a belief view, with its normalised posterior.
struct BeliefEntry
{
    Deal const& layout;
    Probability posterior;  ///< normalised; the entries of one BeliefView sum to 1
};

/// The belief space as declarer knows it. Only the normalised posterior
/// reaches this view, never sample weights.
struct BeliefView
{
    std::span<BeliefEntry const> entries;
    bool is_sample;           ///< false only when this node holds the whole remaining space
    std::size_t space_size;   ///< layouts believed consistent, if known; 0 when unknown
};

/// Per-child records for a declarer node, for a future reuse cache.
/// **Never filled in.** Nothing writes or tests it, so do not rely on any
/// field.
struct NodeSearchInfo
{
    Deal renumbered;   ///< remaining cards, gaps removed
    int max_tricks;    ///< strategy-independent upper bound
    int min_tricks;    ///< strategy-independent lower bound
    std::map<std::pair<StrategyId, int>, Probability> p_make;  ///< keyed by (strategy, tricks needed)
};

}  // namespace dds::belief_evaluation
