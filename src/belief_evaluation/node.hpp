#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/layout_source.hpp>
#include <belief_evaluation/types.hpp>

namespace dds::belief_evaluation
{

/// One node of the recursion: the state every layout shares
/// (`ObservationState`), the surviving layouts, and each layout's
/// un-normalised probability `p`.
///
/// `kappa` is the node's sample weight. It is kept apart from `p`, so that
/// rescaling `kappa` moves every layout's weight at once.
///
/// `layouts`, `p` and `root_keys` are parallel: position `i` is one
/// layout, its weight and its root-space identity.
struct BeliefNode
{
    ObservationState state;

    /// Replenishment can append, but only at node entry in `p_make()`,
    /// before any cut runs and before any `BeliefView` is built.
    /// **A `BeliefEntry` holds `Deal const&` into this vector.** Growing it
    /// while a view is live leaves those references dangling.
    std::vector<Deal> layouts;
    std::vector<Probability> p;  ///< parallel to layouts

    /// Each layout's identity in **root space**, parallel to `layouts`.
    /// Computed once at `make_root`. See docs/module_map.md, "Identity is
    /// root-space".
    std::vector<std::uint64_t> root_keys;

    SampleWeight kappa = 0.0;

    /// True when the layouts are a sample of a larger space. Set by
    /// `make_root` and inherited by every child.
    ///
    /// **Not monotone.** A replenishment scan that ends with
    /// `ScanOutcome::SourceExhausted` sets it back to `false` at that node,
    /// because the scan covered the whole remaining space. The change never
    /// reaches the parent. Feeds `BeliefView::is_sample` and gates
    /// `tier2_dead()`.
    bool is_sample = false;

    /// True when a replenishment scan on the path to this node reached
    /// `ScanOutcome::SourceExhausted`. Then no scan below can find
    /// anything new: a descendant's history extends this node's, so any
    /// match there would already have matched here.
    ///
    /// **A separate field from `is_sample`, and the two can differ.**
    /// `is_sample` says whether this node holds everything its path
    /// admits. This field says whether scanning below could help.
    ///
    /// Never cleared: the source does not change during a search. Checked
    /// before the trigger scans, so a path pays for "still nothing" at
    /// most once.
    bool no_more_available = false;
};

/// Why `make_root` could not build a node. `None` when it could. This is
/// not a `ValidationError`, which names causes for a *callback* return.
enum class RootFailure
{
    None,
    SourceNotEnumerable,   ///< source.size() is std::nullopt
    NoLayoutSurvived,      ///< the whole source was scanned and no candidate was consistent
    /// `RootOptions::scan_budget` ran out before any consistent layout was
    /// found, and the source was not exhausted. Raise the budget. (For
    /// `NoLayoutSurvived`, fix the source.)
    ScanBudgetExhausted,
    /// `RootOptions::sample_size` was 0. Rejected before any `source.at()`
    /// call.
    SampleSizeZero,
};

/// Why a scan stopped. This applies to the root scan and to each
/// replenishment scan. A scan with no cap always ends `SourceExhausted`.
enum class ScanOutcome
{
    /// The whole source was scanned. The node holds every consistent
    /// layout. `is_sample` is false.
    SourceExhausted,
    /// `sample_size` was reached first. More consistent layouts may
    /// exist. `is_sample` is true.
    SampleFilled,
    /// `scan_budget` was reached first. A smaller draw, not a failure,
    /// if at least one layout survived (otherwise
    /// `RootFailure::ScanBudgetExhausted`). `is_sample` is true.
    BudgetExhausted,
};

/// The result of `make_root`. `node.has_value()` is true exactly when
/// `failure == None`. `outcome` is meaningful only on success.
struct RootConstructionResult
{
    std::optional<BeliefNode> node;
    RootFailure failure = RootFailure::None;
    ScanOutcome outcome = ScanOutcome::SourceExhausted;
};

/// Options for `make_root`, which can be called on its own.
struct RootOptions
{
    /// The maximum number of layouts drawn from `source`. Absent means
    /// exhaustive enumeration. Exactly 0 is rejected:
    /// `RootFailure::SampleSizeZero`.
    std::optional<std::uint64_t> sample_size;

    /// The maximum number of `source.at()` calls the scan may make. Absent
    /// means no limit. It counts `at()` calls because those are the
    /// expensive, caller-written operation. Applies whether or not
    /// `sample_size` is set.
    std::optional<std::uint64_t> scan_budget;
};

/// The cards already played to the current trick of `root_layout` (0..3),
/// in order. A `Deal` records no earlier tricks. Rank 0 marks an empty
/// slot.
///
/// Public so that a node-local replay skips exactly the same prefix.
auto history_for(Deal const& root_layout) -> PlayTraceBin;

/// The `ObservationState` that `make_root` builds before scanning:
/// - `trump` and `first` from `root_layout`;
/// - `history` from `history_for()`, whether or not `record` is given;
/// - `declarer` and `tricks_needed` from the caller;
/// - `tricks_won_by_declarer = 0` (a trick in progress is not won);
/// - `known_holdings` and `ranks` as `make_root` describes;
/// - `play_record` from `record`, or null.
///
/// Public so that a node-local replay can rebuild the states δ saw, by
/// calling `advance_state` one card at a time. The recursion keeps none
/// of them.
auto root_observation_state(
    Deal const& root_layout, int declarer, int tricks_needed, PlayRecord const* record = nullptr)
    -> ObservationState;

/// True when `candidate` belongs in the same belief space as `root`. See
/// `make_root` for the rule. The root scan and every replenishment scan
/// use this one function.
auto is_consistent(Deal const& candidate, Deal const& root, int declarer, int dummy) -> bool;

/// Builds the root node. Scans `source` from index 0 and takes every
/// layout consistent with `root_layout`, at `p_i = 1`, up to
/// `options.sample_size` and `options.scan_budget`.
///
/// `kappa = 1 / node.layouts.size()` always, so the root's mass is exactly 1.
///
/// **`node.is_sample` is true exactly when `outcome != SourceExhausted`.**
/// It means a cap stopped the scan, not that a cap was given. A
/// `sample_size` at or above the number of consistent layouts gives the
/// exhaustive result, byte for byte, with `is_sample` false.
///
/// If both caps would stop the scan at the same step, `sample_size` wins
/// (`SampleFilled`).
///
/// A candidate is consistent with `root_layout` when it has:
/// - the same trump, `first` and current trick;
/// - the same holdings for declarer and dummy (`(declarer + 2) % DDS_HANDS`);
/// - the same outstanding pool, suit by suit, split between the defenders.
///   The split may differ. This keeps `RankMap::aggr` the same across the
///   node.
///
/// Failures are reported, not asserted, because `source` is caller input:
/// - `source.size()` is absent;
/// - `sample_size` is 0;
/// - nothing in the source is consistent;
/// - the budget ran out before any layout was found.
///
/// A budget that runs out after at least one layout is not a failure. The
/// node is returned with `BudgetExhausted`.
///
/// `record`, when given, becomes `node.state.play_record`. It does not
/// interact with `options`.
auto make_root(
    Deal const& root_layout,
    int declarer,
    int tricks_needed,
    LayoutSource const& source,
    RootOptions const& options = {},
    PlayRecord const* record = nullptr) -> RootConstructionResult;

/// The node's total probability mass, kappa * Sigma_i p_i, summed with
/// KahanAccumulator.
auto node_mass(BeliefNode const& node) -> double;

/// The value of a node with no cards left: `node_mass(node)` if declarer
/// has `tricks_needed`, else 0.
auto terminal_value(BeliefNode const& node) -> double;

/// True when no hand has a card left. One layout is enough to check: every
/// layout has the same outstanding pool, and `node.layouts` is never empty.
auto is_terminal(BeliefNode const& node) -> bool;

/// The number of tricks still to play from `state`. The same in every
/// layout.
///
/// Computed from declarer's holding in `state.known_holdings`. It is one
/// short mid-trick if declarer has already played to the trick. That uses
/// the `first` of `known_holdings`, not `state.first`, which is the root's
/// leader.
auto tricks_remaining(ObservationState const& state) -> int;

}  // namespace dds::belief_evaluation
