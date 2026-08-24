#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <utility>

#include <api/dll.h>
#include <utility/constants.h>

/// A single card. Suits and ranks are otherwise passed as separate `int`s
/// throughout dds; this exists purely as a convenient callback return/param.
struct Card
{
    int suit;  ///< 0=S, 1=H, 2=D, 3=C
    int rank;  ///< 2..14
};

/// Identifies a declarer strategy among strategies compared together.
/// Unused beyond distinctness validation today; load-bearing once strategy
/// comparison is added.
using StrategyId = std::uint32_t;

/// Opaque byte string a strategy uses to declare what `play` consults beyond
/// the position the evaluator already keys on. The evaluator never
/// interprets the contents; equal keys assert identical future play for
/// identical remaining position. See `DeclarerStrategy::state_key`.
using StateKey = std::string;

/// p — probability the defenders played a given card sequence in one layout,
/// or (for BeliefEntry) a normalised posterior. Kept as a distinct alias from
/// SampleWeight because the evaluation note keeps the two distinct in the
/// notation even though both are `double` underneath.
using Probability = double;

/// kappa — the sample weight carried along a search path; 1/M at the root of
/// a sampled evaluation. Distinct from Probability for the same reason.
using SampleWeight = double;

/// Outstanding-card pool and the absolute/relative rank mapping over it, for
/// one belief-evaluation node. Layout-invariant across the node: every
/// layout in a node shares the same outstanding cards per suit and differs
/// only in how the defenders' cards are split. See rank_map.hpp for the
/// method bodies and how `aggr` is built from a Deal.
struct RankMap
{
    std::array<unsigned, DDS_SUITS> aggr;  ///< outstanding pool per suit

    /// Absolute rank -> relative, 1 = highest, 0 if not outstanding. Also 0,
    /// rather than undefined behaviour, for a `suit` outside `0..DDS_SUITS`
    /// or a `rank` outside `2..14` — indistinguishable from "not
    /// outstanding" by design, since no valid holding could contain either.
    auto to_relative(int suit, int rank) const -> int;

    /// Relative ordinal (1 = highest) -> absolute rank. Also 0, rather than
    /// undefined behaviour, for a `suit` outside `0..DDS_SUITS` or an
    /// `ordinal` outside `1..13`.
    auto to_absolute(int suit, int ordinal) const -> int;
};

/// The commonly-known part of a belief-evaluation node: identical in every
/// layout of the belief space, and everything a declarer strategy may
/// condition on directly (as opposed to through the belief view).
struct ObservationState
{
    int trump;
    int first;                  ///< seat on lead at the root
    PlayTraceBin history;       ///< every card played so far, in order
    int declarer;                ///< seat; dummy is (declarer + 2) % 4
    int tricks_needed;           ///< tricks still required to make the contract
    int tricks_won_by_declarer;
    Deal known_holdings;         ///< declarer + dummy exact; defender entries are the union pool
    RankMap ranks;
};

/// One layout in a belief view, paired with its normalised posterior.
struct BeliefEntry
{
    Deal const& layout;
    Probability posterior;  ///< normalised; the entries of one BeliefView sum to 1
};

/// What a declarer strategy reasons over: the belief space as declarer
/// currently knows it. Sample weights and any rescaling are the evaluator's
/// bookkeeping and never cross into this view — only the normalised
/// posterior does.
struct BeliefView
{
    std::span<BeliefEntry const> entries;
    bool is_sample;           ///< false only when this node holds the whole remaining space
    std::size_t space_size;   ///< layouts believed consistent, if known; 0 when unknown
};

/// A declarer node's per-child bookkeeping record, for a future reuse
/// cache. **Not yet populated.** The exhaustive evaluator has no cache and
/// declarer nodes have exactly one child, so nothing here has a reason to
/// write to this struct or a way to test a value it wrote —
/// half-populating it now would let it silently acquire fields a future
/// caller trusts without ever having been exercised. The `p_make` map
/// shape is chosen for a future strategy-comparison search, not used
/// before then.
struct NodeSearchInfo
{
    Deal renumbered;   ///< remaining cards, gaps removed
    int max_tricks;    ///< strategy-independent upper bound
    int min_tricks;    ///< strategy-independent lower bound
    std::map<std::pair<StrategyId, int>, Probability> p_make;  ///< keyed by (strategy, tricks needed)
};
