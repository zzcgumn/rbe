#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/layout_source.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/validation.hpp>

namespace dds::belief_evaluation
{

/// Which callback — or which stage of the evaluator itself — produced a
/// reported EvaluationError.
enum class EvaluationCallback
{
    RootConstruction,  ///< make_root() rejected `source`; not a callback, but reported the same way
    DeclarerPlay,
    DefenderStrategy,
};

/// A user-supplied callback's contract violation (or, for
/// `RootConstruction`, an unusable `LayoutSource`), with enough context to
/// locate the offending call: which callback, which seat was asked, and
/// the layout it was evaluated against.
///
/// `layout` is `node.state.known_holdings` for a `DeclarerPlay` error (the
/// card is checked against declarer's or dummy's exact holding, common to
/// the whole node); the one specific layout whose distribution violated the
/// contract for a `DefenderStrategy` error; and the root layout passed to
/// `evaluate()` for a `RootConstruction` error.
///
/// `validation` is always `ValidationError::None` for a `RootConstruction`
/// error — there is no callback return to validate; `root_failure` carries
/// the actual cause instead, and is meaningful only for a `RootConstruction`
/// error (`RootFailure::None` otherwise, the same way `seat` and `layout`
/// carry a different meaning per `callback` value already).
struct EvaluationError
{
    ValidationError validation;
    EvaluationCallback callback;
    int seat;
    Deal layout;
    RootFailure root_failure = RootFailure::None;
};

/// The value of one candidate action at the root, and the card that leads
/// to it. See EvaluationValue::root_children for what "candidate" means
/// when the root is a declarer node versus a defender node.
struct RootChildValue
{
    Card card;
    double value;
};

/// A caller-supplied double-dummy upper bound on the tricks declarer can
/// take from a given layout, for EvaluateOptions::bound. Consumed by
/// `tier2_dead()` (below) — kept as a seam separate from the cut itself so
/// the cut is testable with a scripted bound and no solver in the loop,
/// exactly as `spread()` is testable without `solve_board`.
///
/// **Not validated, and cannot be**: π's card is checkable against the
/// layout, δ's distribution is checkable against the contract, but a
/// claimed trick bound is checkable against nothing short of solving the
/// position — which is the work the bound exists to avoid. A bound that is
/// too high makes `tier2_dead()` fire when it should not and silently
/// reports zero for a contract that makes. This is one of two
/// unvalidatable obligations a caller supplying a bound takes on; see
/// EvaluateOptions::delta_is_double_dummy_optimal for the other, and
/// `DeclarerStrategy::state_key`'s own doxygen for the same register
/// applied to a different obligation already in this module.
using LayoutBound = std::function<int(Deal const&)>;

/// Opt-in behaviour for `evaluate()`. Off by default: nodes are built on
/// the recursion stack and released as each subtree completes, so a
/// retained tree — a belief set kept at every node — is affordable only
/// when asked for; see EvaluationValue::retained_root.
///
/// Four fields now, and growing as later capabilities add their own — this
/// stays a flat struct of independently-defaulted options rather than
/// acquiring internal structure of its own; if that stops reading clearly
/// as options accumulate, that is worth revisiting then, not pre-empting
/// here.
struct EvaluateOptions
{
    bool retain_root = false;

    /// Populate EvaluationValue::counters. Off by default, so the ordinary
    /// path pays nothing to collect what nothing is asking for. Collecting
    /// counters must never change `p_make` or `root_children` — every
    /// counter measures this run's own shape or cost and none of them
    /// feeds back into the recursion (see counters_test.cpp's paired-run
    /// check, both directions).
    bool collect_counters = false;

    /// See LayoutBound's own doxygen. Absent by default (a default-
    /// constructed `std::function` is empty), which leaves `tier2_dead()`
    /// disabled the same way `delta_is_double_dummy_optimal` being unset
    /// does — the two are separate obligations and neither implies the
    /// other; see that field for why they are not collapsed into one.
    LayoutBound bound;

    /// The caller's declaration that `delta` holds declarer to the
    /// double-dummy trick count whatever declarer does — i.e. that `delta`
    /// is double-dummy optimal *for trick count*. Unset by default,
    /// **separate** from `bound` itself: supplying a bound and making this
    /// declaration assert different things (a function, versus a claim
    /// about δ's behaviour), and a caller may legitimately want a bound
    /// computed for instrumentation while their δ does not qualify.
    /// Collapsing the two into one flag would make the unsound
    /// configuration the easy one.
    ///
    /// This does **not** conflict with the separate, already-documented
    /// fact that a trick-maximising defender is not best defence against a
    /// *contract* (see `DoubleDummyDefender`'s own doxygen) — that caveat
    /// is about the contract; this declaration is about trick count, and a
    /// trick-maximising δ (`DoubleDummyDefender` under either
    /// `SpreadPolicy`) satisfies it against any π, since maximising tricks
    /// for both sides at `target = -1` holds declarer to the double-dummy
    /// trick count regardless of which card declarer actually plays.
    ///
    /// **Cannot be validated, and a wrong declaration is silently wrong in
    /// only one direction.** If `delta` does not actually hold declarer to
    /// the double-dummy trick count — a scripted δ that ducks a trick it
    /// need not have lost, for instance — `tier2_dead()` can discard a
    /// layout where declarer, following π, would actually have made the
    /// contract. The bound is an upper bound on double-dummy play; nothing
    /// here checks that δ delivers double-dummy play.
    bool delta_is_double_dummy_optimal = false;

    /// Cap the number of layouts drawn for the root, absent for exhaustive
    /// enumeration (every consistent layout, unchanged behaviour). When
    /// present, threaded straight through to `make_root` as
    /// `RootOptions::sample_size` — see that field, and `make_root`'s own
    /// doxygen, for the exact scanning and `is_sample` semantics. No seed
    /// anywhere: the source's own ordering is where any randomness has to
    /// live (see `LayoutSource::at`'s doxygen), not here.
    std::optional<std::uint64_t> sample_size;
};

/// Instrumentation `evaluate()` can report about its own run, populated
/// only when EvaluateOptions::collect_counters is set — see
/// EvaluationValue::counters. Nothing here is read back into `p_make`;
/// every field is purely a fact about this run's own shape or cost.
///
/// This is the module's shared instrumentation mechanism, not a type
/// specific to whatever fills it in first. Later additions belong here
/// rather than in a parallel mechanism of their own — some of what a
/// future capability adds will naturally be per-depth rather than scalar
/// (sample size, replenishment count, and scan-to-hit, each broken out by
/// recursion depth, are the known examples). That arrives as its own
/// `std::vector<...>` member indexed by depth, added alongside the scalar
/// fields below, not a reshaping of this type — nothing here needs to
/// anticipate that further than leaving room for it.
struct EvaluationCounters
{
    /// Every BeliefNode reached and evaluated for a value — terminal or
    /// expanded, including the root itself. Meaningful on its own with no
    /// cuts implemented at all: a cut that fires reduces this count below
    /// the same fixture's uncut run, which is how a cut's tests prove it
    /// actually fired rather than merely computing the right number.
    std::uint64_t nodes_visited = 0;

    /// Every already_made() cut that fired: the contract is already made in
    /// every layout the node holds, so the node's mass is returned directly
    /// without expanding it.
    std::uint64_t tier1_made_cuts = 0;

    /// Every is_dead() cut that fired: the contract cannot be made from the
    /// node in even one layout, so 0.0 is returned directly without
    /// expanding it.
    std::uint64_t tier1_dead_cuts = 0;

    /// Every tier2_dead() cut that fired: every layout the node holds is
    /// dead under the caller's injected bound and delta_is_double_dummy_optimal
    /// declaration. Gated on !is_sample, so this stays 0 on any run where
    /// the root (or an ancestor) was sampled.
    std::uint64_t tier2_cuts = 0;
};

/// `P_make` for one declarer strategy against one defender strategy, plus
/// root-child values for a search layer above the evaluator to compare
/// candidate root actions without re-running the whole search once per
/// candidate.
///
/// - If the root is a declarer (or dummy) node: one entry per card
///   declarer or dummy may legally play first, each the `P_make` of
///   committing to that card and following `pi` thereafter. These are
///   **alternatives, not a partition** — `p_make` equals whichever entry
///   corresponds to `pi`'s actual choice at the root, not their sum.
/// - If the root is a defender node: one entry per card `delta` assigns
///   positive probability to somewhere in the belief space — exactly the
///   children defender-node expansion produces. These **do** sum to
///   `p_make`, since defender children partition mass by construction (see
///   specs/replenished-belief-evaluation.md's mass-conservation invariant).
/// - Empty at a terminal root (no cards left to play a first card from), or
///   at a root where declarer has already banked every trick the contract
///   needs before any card is played -- in both cases there is no
///   first-card decision left to report alternatives for.
struct EvaluationValue
{
    double p_make = 0.0;
    std::vector<RootChildValue> root_children;

    /// The root BeliefNode, populated only when EvaluateOptions::retain_root
    /// is set. Not a full retained tree — see evaluate.cpp and the spec for
    /// why a full tree is not retained by default.
    std::optional<BeliefNode> retained_root;

    /// This run's instrumentation, populated only when
    /// EvaluateOptions::collect_counters is set.
    std::optional<EvaluationCounters> counters;
};

/// Either the value, or the EvaluationError a callback's return (or
/// `source`) violated — never both. Never an exception across the callback
/// boundary: a callback is user input, not an internal, so a contract
/// violation is reported here rather than thrown. A genuine internal
/// invariant failure (mass conservation off by more than tolerance) is a
/// different category and asserts rather than reporting through this type.
struct EvaluationResult
{
    /// `pi.id`'s dense-mapped entry. Exactly one entry today — `evaluate()`
    /// takes a single DeclarerStrategy — but keyed by the caller's own
    /// StrategyId (not an internal dense index) so a caller can look its
    /// own strategy up directly, and so the shape survives a future
    /// multi-strategy comparison without changing.
    std::map<StrategyId, EvaluationValue> by_strategy;
    std::optional<EvaluationError> error;  ///< meaningful only when by_strategy is empty
};

/// Tier 1's already-made cut: true once declarer has banked every trick
/// the contract needs, whatever is left to play. Sound with no
/// precondition at all -- tricks_won_by_declarer and tricks_needed are
/// both common knowledge, identical across every layout the node holds,
/// so once this holds the contract is made in every layout of the node
/// and nothing about pi, delta, or sampling enters the argument. No gate.
///
/// Exposed (rather than kept private to evaluate.cpp) for the same reason
/// `is_terminal()`/`terminal_value()` are: so a test can construct an
/// `ObservationState`/`BeliefNode` by hand and check the cut condition
/// directly, including states the evaluator itself cannot yet produce
/// (see `tier2_dead()`'s own `is_sample` note).
auto already_made(ObservationState const& state) -> bool;

/// Tier 1's dead cut, the mirror of already_made(): true once declarer
/// cannot reach tricks_needed even by winning every remaining trick.
/// Sound with no precondition, same argument as already_made() --
/// tricks_won_by_declarer, tricks_needed and the outstanding pool
/// (tricks_remaining() reads it) are all common knowledge, identical
/// across every layout the node holds. No gate.
auto is_dead(ObservationState const& state) -> bool;

/// Tier 2's node-level cut: true only when the caller has made the
/// EvaluateOptions::delta_is_double_dummy_optimal declaration *and* every
/// layout at `node` is dead by the injected bound (EvaluateOptions::bound).
/// Absent either, this never fires, whatever the bound says -- the
/// declaration is not a performance switch (see that field's own doxygen
/// for why R <= DD is false against a defence that errs).
///
/// Gated on `! node.is_sample`, unlike either tier-1 cut above: those read
/// only common knowledge, identical across every layout regardless of
/// whether the node is a full space or a sample of one. This cut instead
/// concludes "every layout in this node is dead" from the layouts the node
/// happens to hold; on a sample that is only "every layout *drawn* is
/// dead", which says nothing about every layout in the true space. Nothing
/// in this evaluator sets `is_sample` yet, so this gate is a no-op today --
/// load-bearing only once a future sampling evaluator sets it true, and
/// testable today only by constructing a `BeliefNode` with `is_sample =
/// true` by hand.
///
/// Stops at the first live layout (`bound(layout) >=` what is still
/// needed) rather than calling `bound` for every layout: each call is a
/// double-dummy solve in production.
///
/// **Never a make-cut.** This function only ever answers "is every layout
/// dead" -- it has no "not dead" branch that concludes anything about a
/// make, because DD >= rho implies nothing about R: pi may play worse than
/// double dummy, so the single solve behind a bound must never be reused
/// to conclude the contract makes. Node-level, not per-layout: dropping a
/// dead layout here would renormalise every surviving layout's posterior,
/// changing what an arbitrary caller-supplied pi does with the belief view
/// it is given -- this function returns before any view is built at or
/// below `node`, so that problem cannot arise.
auto tier2_dead(BeliefNode const& node, EvaluateOptions const& options) -> bool;

/// Exhaustively evaluates `P_make` for `pi` against `delta` over every
/// layout `source` enumerates that is consistent with `root_layout` (see
/// `make_root`). No sampling, no replenishment. Early cuts (already_made(),
/// is_dead(), tier2_dead() above) skip subtrees that are guaranteed to
/// contribute exactly zero to the result — none of them change any answer;
/// see `specs/replenished-belief-evaluation.md` for what makes each sound.
///
/// `state_key` is never called: there is no cache yet.
auto evaluate(
    Deal const& root_layout,
    int declarer,
    int tricks_needed,
    LayoutSource const& source,
    DeclarerStrategy const& pi,
    DefenderStrategy const& delta,
    EvaluateOptions const& options = {}) -> EvaluationResult;

}  // namespace dds::belief_evaluation
