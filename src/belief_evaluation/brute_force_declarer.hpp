#pragma once

#include <exception>
#include <optional>

#include <belief_evaluation/brute_force_cache.hpp>
#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/double_dummy_bound.hpp>
#include <belief_evaluation/double_dummy_defender.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/validation.hpp>

/// Forward-declared, not included. See double_dummy_defender.hpp.
class SolverContext;

namespace dds::belief_evaluation
{

/// The quantity BruteForceDeclarer maximises. Fixed for the lifetime of
/// an instance, because all searches share one cache.
enum class DeclarerObjective
{
    MaximizeExpectedTricks,
    MaximizeProbabilityToMake,
};

/// Limits for a belief space or a play too large to search in full. Both
/// absent by default: search to a real terminal, over the whole belief
/// space.
struct BruteForceOptions
{
    /// Plies to search before scoring each layout with DoubleDummyBound
    /// instead of branching.
    std::optional<int> max_depth;

    /// A node with more layouts than this keeps only the max_layouts
    /// layouts with the highest posterior. Pruning happens before the
    /// depth cutoff pays for DoubleDummyBound calls. **The dropped mass is
    /// not redistributed, so the value is an under-count.**
    ///
    /// **Not applied to a terminal node.** A terminal leaf needs no solver,
    /// so pruning would only lose mass. is_terminal() is checked on the
    /// full layout set first.
    std::optional<std::uint64_t> max_layouts;
};

/// Thrown from the `play` of BruteForceDeclarer::as_strategy() when
/// `opponent_model` breaks the DefenderStrategy contract. The checks are
/// those of validate_defender_distribution. The seat and layout are the
/// opponent model's, not this strategy's.
///
/// evaluate() catches the base class, DeclarerStrategyContractViolation,
/// and reports it as an ordinary EvaluationError. Only a caller who calls
/// `play()` directly sees the exception. See declarer_strategy.hpp.
struct BruteForceOpponentModelError : DeclarerStrategyContractViolation
{
    BruteForceOpponentModelError(ValidationError validation, int seat, Deal const& offending_layout)
    : DeclarerStrategyContractViolation(validation, seat, offending_layout)
    {
    }

    auto what() const noexcept -> char const* override
    {
        return "BruteForceDeclarer's own internal opponent model violated DefenderStrategy's contract";
    }
};

/// The value of a node where `is_terminal(node)` holds.
/// - `MaximizeProbabilityToMake`: `terminal_value()`.
/// - `MaximizeExpectedTricks`: node mass times `tricks_won_by_declarer`,
///   which is the same in every layout at a terminal.
///
/// Public so that tests can check it on a hand-built node.
auto terminal_leaf_value(BeliefNode const& node, DeclarerObjective objective) -> double;

/// The value of a node at the depth cutoff. Layouts can differ in
/// double-dummy tricks here, so this sums
/// `node.kappa * node.p[i] * per-layout-value` over every layout. `bound`
/// gives declarer's double-dummy tricks whoever is on lead. See
/// DoubleDummyBound::as_bound().
auto cutoff_leaf_value(BeliefNode const& node, LayoutBound const& bound, DeclarerObjective objective)
    -> double;

/// `node` with only its `keep` highest-posterior layouts, or `node`
/// unchanged if it holds no more than that.
///
/// **The dropped mass is not redistributed, so values are under-counted.**
/// Redistributing would overweight the survivors.
///
/// Ties are broken by layout_key, not by position in `node.layouts`. So
/// the survivors do not depend on enumeration order.
auto drop_lowest_posterior_layouts(BeliefNode const& node, std::uint64_t keep) -> BeliefNode;

/// The leaf decision for one node. Either `node` is a leaf, and `value` is
/// set, or `continuation` holds the node to branch from.
struct LeafCheckResult
{
    /// Set when this node is a leaf. Empty when search() must branch.
    std::optional<double> value;

    /// When `value` is empty: `node`, or its pruned form. When the node is
    /// terminal: `node` unchanged.
    BeliefNode continuation;
};

/// The leaf decision search() makes, so that tests can call it directly.
/// The order:
/// 1. is_terminal, on `node` as given;
/// 2. max_layouts pruning;
/// 3. the depth cutoff.
auto check_leaf(
    BeliefNode const& node, int depth, BruteForceOptions const& options, LayoutBound const& bound,
    DeclarerObjective objective) -> LeafCheckResult;

/// A DeclarerStrategy that searches ahead over the belief space and plays
/// the best legal card under a fixed DeclarerObjective. The search is
/// exhaustive unless BruteForceOptions limits it.
///
/// **Caller preconditions. Nothing checks them.**
/// - **The opponent model should be the paired delta.** With a different
///   delta, evaluate() still returns a valid P_make. It is just not the
///   double-dummy-optimal value this search computes.
/// - **The opponent model must answer from the position alone.** It must
///   not read `ObservationState::history` or `::play_record`.
///   `BruteForceCacheKey` omits both, so a cached value can come from a
///   different line of play. `ScriptedDefender` reads `state.history`, so
///   it is not a valid opponent model.
///
/// **The cache.** Each instance owns one.
/// - **It is shared across calls only when `max_depth` is absent.** With
///   `max_depth` set, a node's value depends on the depth where the search
///   reached it. So each `play()` call starts with an empty cache.
/// - **It is never shared between instances.** Sharing one between a
///   different DeclarerObjective or opponent model would make values
///   silently wrong.
///
/// The ctx rules of DoubleDummyDefender apply.
class BruteForceDeclarer
{
public:
    /// An empty `opponent_model` means an owned DoubleDummyDefender over
    /// the same `ctx`.
    explicit BruteForceDeclarer(
        SolverContext& ctx, DeclarerObjective objective = DeclarerObjective::MaximizeExpectedTricks,
        DefenderStrategy opponent_model = nullptr, BruteForceOptions options = {});

    /// Not copyable or movable. The default `opponent_model_` captures the
    /// address of `default_opponent_`. A copy would still call the
    /// original's defender, or freed memory.
    BruteForceDeclarer(BruteForceDeclarer const&) = delete;
    auto operator=(BruteForceDeclarer const&) -> BruteForceDeclarer& = delete;
    BruteForceDeclarer(BruteForceDeclarer&&) = delete;
    auto operator=(BruteForceDeclarer&&) -> BruteForceDeclarer& = delete;

    /// The returned DeclarerStrategy captures `this`. It must not outlive
    /// this BruteForceDeclarer.
    auto as_strategy() -> DeclarerStrategy;

private:
    /// Builds bound_ on the first call, from state.declarer. Asserts that
    /// every later call has the same declarer.
    auto bound_for(ObservationState const& state) -> DoubleDummyBound&;

    /// The recursion. At a leaf, the leaf value. Otherwise a cache lookup,
    /// then:
    /// - declarer or dummy: try every legal card and keep the best;
    /// - defender: group and sum the opponent model's responses.
    ///
    /// The result is stored in the cache. The spec, `BruteForceDeclarer`
    /// under "Key entry points", explains why no normalisation is needed.
    auto search(BeliefNode const& node, int depth) -> double;

    /// True when `seat` is declarer or dummy. A copy of the private check
    /// in evaluate.cpp.
    static auto is_declarer_side(ObservationState const& state, int seat) -> bool;

    SolverContext& ctx_;
    DeclarerObjective objective_;
    DefenderStrategy opponent_model_;
    BruteForceOptions options_;
    BruteForceCache cache_;
    std::optional<DoubleDummyDefender> default_opponent_;  // set only when the caller supplied no opponent_model
    std::optional<DoubleDummyBound> bound_;                // built on first use; see bound_for()
    int bound_declarer_ = -1;                              // declarer of bound_, for the assertion above
};

}  // namespace dds::belief_evaluation
