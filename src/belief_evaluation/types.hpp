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

/// A single card. Suits and ranks are otherwise passed as separate `int`s
/// throughout dds; this exists purely as a convenient callback return/param.
///
/// `api/dds.h` declares an unrelated, layout-identical `::Card` at global
/// scope, carrying a different rank convention. The two coexist by
/// namespace with no include ordering needed; `namespace_collision_test.cpp`
/// references both side by side.
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
    /// Every card still in **any** of the four hands, per suit — declarer's
    /// and dummy's included, and excluding the trick in progress. Not the
    /// defenders' cards: `DefenderPool` (defender_split.hpp) uses
    /// "outstanding" for those alone, so the same word names two different
    /// sets a few headers apart. Measured at one root, spades: `aggr` gave
    /// 8125, all four hands, against the defenders' own pool of 5149.
    ///
    /// Bit `r - 2` for absolute rank `r`, which is the *other* convention
    /// from `Deal::remainCards`'s bit `r`. `trick.hpp`'s `to_compacted` is
    /// where that boundary is crossed; a stray `>> 2` elsewhere is a bug.
    std::array<unsigned, DDS_SUITS> aggr;

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
    PlayTraceBin history;       ///< every card played so far, in order — including
                                 ///< cards already played to the root's trick in
                                 ///< progress, if any, but never cards from a trick
                                 ///< that completed before the root was constructed:
                                 ///< a Deal keeps no record of a resolved trick, so
                                 ///< that history is unrecoverable from the root
                                 ///< layout alone
    int declarer;                ///< seat; dummy is (declarer + 2) % 4
    int tricks_needed;           ///< tricks still required to make the contract
    int tricks_won_by_declarer;
    Deal known_holdings;         ///< declarer + dummy exact; defender entries are the union pool
    RankMap ranks;

    /// Every card played before this evaluation's root, invariant for the
    /// whole evaluation -- unlike `history` above, which is root-relative
    /// and grows as the search plays cards. Null when the caller supplied
    /// no `PlayRecord`. Non-owning: owned by the `EvaluateOptions` the
    /// caller passed to `evaluate()`, which outlives every
    /// `ObservationState` this evaluation builds -- the same shape
    /// `SolverContext&` already has. One exception: the copy embedded in
    /// `EvaluationValue::retained_root` outlives the call itself, with no
    /// such guarantee about `options` -- `evaluate()` clears this field
    /// there rather than leave it dangling; see that field's own doxygen.
    PlayRecord const* play_record = nullptr;

    /// Set only by the Python binding, never by this core engine itself --
    /// every C++ caller sees this default-empty and gets exactly today's
    /// behaviour (play_record read directly, no guard, correct for a
    /// caller who keeps EvaluateOptions alive for as long as its own
    /// ObservationState copies live, which is the documented contract
    /// above). The hazard this guards against is specific to the Python
    /// surface: `evaluate()`'s binding builds a transient, call-local
    /// EvaluateOptions whose lifetime a Python caller cannot see or
    /// control, so a Python strategy stashing `state` past the call that
    /// handed it over and later reading `.play_record` would otherwise
    /// read freed memory. Empty means "no guard applies"; non-empty and
    /// false means "the call that produced this copy has returned, and
    /// play_record no longer points at anything live" -- the Python
    /// `.play_record` property (belief_space_local_evaluation.cpp) checks
    /// this and raises rather than dereferencing in that case. Read
    /// access to play_record during the call this state was built for is
    /// unaffected either way.
    std::shared_ptr<bool> play_record_valid;
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
/// cache. **Not yet populated**, deliberately: with no cache and one child
/// per declarer node, nothing could write to it or test what it wrote, and
/// half-populating it would let it acquire fields a future caller trusts
/// without ever having been exercised.
struct NodeSearchInfo
{
    Deal renumbered;   ///< remaining cards, gaps removed
    int max_tricks;    ///< strategy-independent upper bound
    int min_tricks;    ///< strategy-independent lower bound
    std::map<std::pair<StrategyId, int>, Probability> p_make;  ///< keyed by (strategy, tricks needed)
};

}  // namespace dds::belief_evaluation
