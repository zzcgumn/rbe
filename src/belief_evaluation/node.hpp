#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/layout_source.hpp>
#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// One node of the belief-evaluation recursion: what every layout in the
/// node shares (`ObservationState`), the surviving layouts, and each
/// layout's un-normalised probability `p`.
///
/// `kappa` is the node's own sample weight, never folded into `p` as a
/// stored `w = kappa * p_i`, so that rescaling `kappa` moves every layout's
/// weight at once.
///
/// `layouts`, `p` and `root_keys` are one three-way invariant, not two:
/// position `i` describes one layout, its weight and its root-space
/// identity together.
struct BeliefNode
{
    ObservationState state;

    /// Can grow after construction — a replenishment appends — but only at
    /// node entry in `p_make()`, before any cut is evaluated and before any
    /// `BeliefView` is built. A `BeliefEntry` holds `Deal const&` into this
    /// vector, so growing it while a view is live would dangle every
    /// reference that view holds. Outside that window this is as stable as
    /// it ever was.
    std::vector<Deal> layouts;
    std::vector<Probability> p;  ///< parallel to layouts

    /// Each layout's identity in **root space**, parallel to `layouts` —
    /// see docs/module_map.md, "Identity is root-space", for why this is
    /// computed once at `make_root` and never recomputed at depth.
    std::vector<std::uint64_t> root_keys;

    SampleWeight kappa = 0.0;

    /// Whether this node's layouts are a sample of a larger space rather
    /// than the whole of it. Set at construction (see `make_root`) and
    /// inherited by every child in both expansion paths.
    ///
    /// **Not monotone.** A node whose own replenishment scan reaches
    /// `ScanOutcome::SourceExhausted` sets this back to `false` there,
    /// whatever its parent's value: that scan genuinely covered the whole
    /// of its path's remaining belief space. The flip never propagates
    /// upward — it says nothing about the parent's own prefix. Feeds
    /// `BeliefView::is_sample`, and gates `tier2_dead()`.
    bool is_sample = false;

    /// Whether a replenishment scan somewhere on the path here has already
    /// reached `ScanOutcome::SourceExhausted`. Set from the same signal as
    /// `is_sample`, but **a separate field, because the two must be able to
    /// diverge**: `is_sample` is about whether *this* node holds everything
    /// its own path admits; this is about whether scanning again *below*
    /// here could find anything new. A descendant's history is a strict
    /// extension of this node's, so anything matching the descendant would
    /// already have matched here — which is why a scan below an exhausted
    /// ancestor is pointless whatever the descendant's `is_sample` reads.
    ///
    /// Never cleared: the source does not change mid-search. Checked before
    /// the trigger scans, so the recursion pays for "still nothing" at most
    /// once per path.
    bool no_more_available = false;
};

/// Why `make_root` could not build a node — `None` when it could.
/// Deliberately not `ValidationError`: that enum names rejection causes for
/// a *callback* return, and `source` is not a callback.
enum class RootFailure
{
    None,
    SourceNotEnumerable,   ///< source.size() is std::nullopt
    NoLayoutSurvived,      ///< the whole source was scanned, but no candidate passed the consistency filter
    /// `RootOptions::scan_budget` ran out before a single consistent layout
    /// was found, with the source not yet exhausted. Distinct from
    /// `NoLayoutSurvived`: that one says fix your source, this one says
    /// raise the budget.
    ScanBudgetExhausted,
    /// `RootOptions::sample_size` was present and exactly 0 — rejected
    /// before the scan makes a single `source.at()` call. Distinct from
    /// `NoLayoutSurvived` because the request itself is degenerate whatever
    /// the source holds.
    SampleSizeZero,
};

/// Why a scan — `make_root`'s root-level draw, or a node-local
/// replenishment scan — stopped where it did. `SourceExhausted` covers the
/// exhaustive case too: with no cap set, a scan always ends that way, so
/// there is no separate "not sampling" value.
enum class ScanOutcome
{
    /// The whole source was scanned. The node holds every consistent layout
    /// that exists, whatever the caps were. `is_sample` is false.
    SourceExhausted,
    /// `sample_size` was reached before the source was exhausted. More
    /// consistent layouts may exist beyond where scanning stopped.
    /// `is_sample` is true.
    SampleFilled,
    /// `scan_budget` was reached first — a degraded draw, not a failure,
    /// provided at least one layout survived (see
    /// `RootFailure::ScanBudgetExhausted` for when none did). `is_sample`
    /// is true.
    BudgetExhausted,
};

/// `make_root`'s result. `node.has_value()` and `failure == None` always
/// agree. `outcome` is meaningful only on success — why the scan stopped,
/// not just whether it succeeded.
struct RootConstructionResult
{
    std::optional<BeliefNode> node;
    RootFailure failure = RootFailure::None;
    ScanOutcome outcome = ScanOutcome::SourceExhausted;
};

/// `make_root`'s optional behaviour. Distinct from `EvaluateOptions`
/// because `make_root` is usable standalone, and `bound`,
/// `delta_is_double_dummy_optimal` and `retain_root` mean nothing here.
struct RootOptions
{
    /// Cap the number of layouts drawn from `source`, absent for exhaustive
    /// enumeration. A present value of exactly 0 is a degenerate request,
    /// not a valid draw of nothing: `RootFailure::SampleSizeZero`.
    std::optional<std::uint64_t> sample_size;

    /// Cap the number of `source.at()` calls the scan may make, absent for
    /// an unbounded scan. Counted in `at()` calls specifically — that is
    /// the expensive, user-implemented operation, and counting anything
    /// else would make root and node-local scan-to-hit measurements
    /// incommensurable. Applies whether or not `sample_size` is set, so a
    /// narrow budget degrades the root on its own.
    std::optional<std::uint64_t> scan_budget;
};

/// The cards already played to `root_layout`'s trick in progress (0..3), in
/// order — the only prior history a `Deal` can yield, since a resolved
/// trick leaves no record in it. Rank 0 is the empty-slot sentinel.
///
/// Exposed because a node-local replay must skip exactly this prefix too,
/// computed the same way, so the two can never disagree about where a
/// node's own history begins.
auto history_for(Deal const& root_layout) -> PlayTraceBin;

/// The `ObservationState` `make_root` builds before any layout is scanned:
/// `trump`/`first` from `root_layout`, `history` from `history_for()`,
/// `declarer`/`tricks_needed` from the caller, `tricks_won_by_declarer = 0`
/// (a trick in progress is not a trick already won), and `known_holdings`
/// and `ranks` as `make_root` describes.
///
/// Exposed so a node-local replay can rebuild the intermediate states the
/// original expansion queried δ with, by advancing from here one recorded
/// card at a time (`advance_state`). The recursion retains none of them, so
/// this is the only way to recover them.
auto root_observation_state(Deal const& root_layout, int declarer, int tricks_needed) -> ObservationState;

/// Whether `candidate` belongs in the same belief space as `root` — see
/// `make_root` for the rule. Both the root-level scan and every node-local
/// replenishment scan filter on this one function rather than two copies of
/// the same comparison.
auto is_consistent(Deal const& candidate, Deal const& root, int declarer, int dummy) -> bool;

/// Builds the root node over `source`: scans from index 0, taking every
/// layout consistent with `root_layout` at `p_i = 1`, up to
/// `options.sample_size` and `options.scan_budget` if supplied. With
/// neither, every consistent layout is taken.
///
/// `kappa = 1 / node.layouts.size()` in every case — neither cap reaches
/// the weight, so the node's mass is exactly 1 however many layouts it
/// holds and whyever the scan stopped.
///
/// **`node.is_sample` is true exactly when `outcome != SourceExhausted`**:
/// the scan stopped *because* a cap bound, not merely because one was
/// supplied. A `sample_size` at or above however many layouts survive
/// filtering gives a byte-for-byte exhaustive result. Deriving `is_sample`
/// from `options.sample_size.has_value()` instead would report a sample on
/// a node holding the whole space, breaking `space_size` and switching
/// tier 2's cut off where it is still sound.
///
/// If both caps would bind at the same step `sample_size` wins
/// (`SampleFilled`): it is checked first, so the layout that fills the
/// sample is never charged against the budget.
///
/// A candidate is consistent with `root_layout` when it shares its trump,
/// `first` and current-trick state exactly; shares declarer's and dummy's
/// (`(declarer + 2) % DDS_HANDS`) holdings exactly; and splits the same
/// outstanding pool, suit by suit, between the two defenders — *how* the
/// pool splits may differ, what the pool is may not. That is what keeps
/// `RankMap::aggr` invariant across the node.
///
/// Failures are reported, not asserted, since `source` is caller input:
/// `source.size()` absent; `sample_size` present and 0; nothing consistent
/// in the whole source; or the budget exhausted before a single layout
/// survived. A budget that ran out having found at least one layout is not
/// a failure — the node is returned, degraded, with `BudgetExhausted`.
auto make_root(
    Deal const& root_layout,
    int declarer,
    int tricks_needed,
    LayoutSource const& source,
    RootOptions const& options = {}) -> RootConstructionResult;

/// kappa * Sigma_i p_i, accumulated through KahanAccumulator. The node's
/// total probability mass, independent of what tricks_won_by_declarer says
/// about it.
auto node_mass(BeliefNode const& node) -> double;

/// The value of a node with no cards left to play: `node_mass(node)` if
/// declarer has banked `tricks_needed`, else 0. `tricks_won_by_declarer` is
/// common knowledge and lives only on `node.state`, so the per-layout
/// indicator form this guards against cannot even compile.
auto terminal_value(BeliefNode const& node) -> double;

/// True when no hand in the node has a card left. Checking one
/// representative layout suffices: every layout shares the same outstanding
/// pool, and `node.layouts` is never empty.
auto is_terminal(BeliefNode const& node) -> bool;

/// How many more tricks remain to be played out from `state` — common
/// knowledge, identical across every layout.
///
/// Derived from declarer's own holding in `state.known_holdings`, which is
/// exact; summing all four entries would double-count, since a defender's
/// entry is the shared pool. Declarer's card count equals tricks remaining
/// at a trick boundary and is one short mid-trick if declarer has already
/// played, determined from `known_holdings`'s own `first` — not from
/// `state.first`, which is the root's leader.
auto tricks_remaining(ObservationState const& state) -> int;

}  // namespace dds::belief_evaluation
