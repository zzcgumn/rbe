#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/layout_source.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/validation.hpp>

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
/// `validation` is `ValidationError::None` for a `RootConstruction` error —
/// `make_root()` does not yet distinguish "source.size() is nullopt" from
/// "no layout survived filtering"; `callback` alone identifies the failure.
struct EvaluationError
{
    ValidationError validation;
    EvaluationCallback callback;
    int seat;
    Deal layout;
};

/// The value of one candidate action at the root, and the card that leads
/// to it. See EvaluationValue::root_children for what "candidate" means
/// when the root is a declarer node versus a defender node.
struct RootChildValue
{
    Card card;
    double value;
};

/// Opt-in behaviour for `evaluate()`. Off by default: nodes are built on
/// the recursion stack and released as each subtree completes, so a
/// retained tree — a belief set kept at every node — is affordable only
/// when asked for; see EvaluationValue::retained_root.
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

/// Exhaustively evaluates `P_make` for `pi` against `delta` over every
/// layout `source` enumerates that is consistent with `root_layout` (see
/// `make_root`). No sampling, no replenishment, no cuts of any kind — see
/// `specs/replenished-belief-evaluation.md`.
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
