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

/// Forward-declared rather than included -- see double_dummy_defender.hpp's
/// own doxygen on this same declaration for why.
class SolverContext;

namespace dds::belief_evaluation
{

/// Which quantity BruteForceDeclarer's own internal search maximises.
/// Locked for one instance's whole lifetime, never a per-call parameter:
/// mixing objectives within one shared cache corrupts backed-up values the
/// same way mixing SpreadPolicy values would for DoubleDummyDefender's own
/// instance-level policy.
enum class DeclarerObjective
{
    MaximizeExpectedTricks,
    MaximizeProbabilityToMake,
};

/// Escape valves for a belief space or a remaining play too large to
/// search exhaustively. Both absent by default, matching every other
/// opt-in field in this module (EvaluateOptions::bound,
/// EvaluateOptions::sampling): absent means today's behaviour, i.e. search
/// to a real terminal or leaf every time, over the whole belief space the
/// caller handed in.
struct BruteForceOptions
{
    /// Plies before falling back to a per-layout DoubleDummyBound call
    /// rather than continuing to branch.
    std::optional<int> max_depth;

    /// A node whose own surviving layout count exceeds this is pruned down
    /// to the max_layouts highest-posterior layouts before being searched
    /// further, including before a depth cutoff's own per-layout
    /// DoubleDummyBound cost is paid -- see drop_lowest_posterior_layouts()
    /// and search()'s own ordering. The dropped mass is never
    /// redistributed; this is a deliberate under-count, not an
    /// approximation that could go either way.
    ///
    /// **Does not apply to a genuine terminal node.** A terminal leaf
    /// calls no solver and is already O(1) -- there is no cost there for
    /// pruning to bound, only mass to lose -- so search() checks
    /// is_terminal() against the node's own full, unpruned layout set
    /// before this cap is ever consulted.
    std::optional<std::uint64_t> max_layouts;
};

/// Thrown from a DeclarerStrategy::play call built by
/// BruteForceDeclarer::as_strategy() when the caller-supplied
/// opponent_model violates DefenderStrategy's own contract -- the same
/// violations validate_defender_distribution already catches at
/// evaluate()'s own outer level, carried here via the same ValidationError
/// enum and offending layout ExpandDefenderResult already provides.
///
/// Derives from DeclarerStrategyContractViolation (declarer_strategy.hpp),
/// not std::exception directly: expand_declarer_node catches that base
/// type at evaluate()'s own call boundary and converts it into an ordinary
/// ValidationError-carrying EvaluationResult, so this exception never
/// reaches evaluate()'s own public entry point -- only a caller invoking
/// play() directly, bypassing evaluate(), sees it propagate as a C++
/// exception (see that base type's own doxygen for the full reasoning, and
/// AMalformedOpponentModelThrowsRatherThanMiscomputing for that
/// direct-caller case, which is what offending_layout below -- a field
/// evaluate()'s own error path never needs, since it already has
/// node.state.known_holdings -- exists for).
struct BruteForceOpponentModelError : DeclarerStrategyContractViolation
{
    Deal offending_layout;

    BruteForceOpponentModelError(ValidationError validation, Deal const& offending_layout)
    : DeclarerStrategyContractViolation(validation), offending_layout(offending_layout)
    {
    }

    auto what() const noexcept -> char const* override
    {
        return "BruteForceDeclarer's own internal opponent model violated DefenderStrategy's contract";
    }
};

/// The node's value when `is_terminal(node)` already holds: `terminal_value()`
/// reused verbatim for `MaximizeProbabilityToMake` (made/not-made is
/// already what that function answers), and the node's own mass-weighted
/// trick count for `MaximizeExpectedTricks` -- `tricks_won_by_declarer` is
/// common knowledge at a genuine terminal node (nothing is left to play),
/// so no per-layout loop is needed here, unlike cutoff_leaf_value below.
///
/// Exposed as a free function, like already_made()/is_dead()/tier2_dead()
/// in evaluate.hpp, so a test can build a node by hand and check this
/// without the recursion.
auto terminal_leaf_value(BeliefNode const& node, DeclarerObjective objective) -> double;

/// The node's value at a depth cutoff reached before a genuine terminal.
/// Unlike terminal_leaf_value, the layouts surviving here can have
/// genuinely different per-layout double-dummy trick counts from each
/// other -- a cutoff, by definition, stops before the position is common
/// knowledge about who wins what -- so this sums
/// `node.kappa * node.p[i] * per-layout-value` across every layout rather
/// than reading one node-wide number. `bound` is declarer's own
/// double-dummy trick count from a given layout, regardless of who is
/// actually on lead there -- see DoubleDummyBound::as_bound().
auto cutoff_leaf_value(BeliefNode const& node, LayoutBound const& bound, DeclarerObjective objective)
    -> double;

/// `node`, pruned down to its `keep` highest-posterior layouts when it
/// holds more than that, else `node` unchanged. The dropped mass is never
/// redistributed onto the survivors -- a deliberate under-count, matching
/// this module's own convention for an approximation that must never
/// overestimate (`suit_top_tricks.hpp`'s own doxygen is the same shape of
/// trade-off, cited for the pattern, not because the two are related).
/// Redistributing would instead systematically overweight whichever
/// layouts happen to survive, the wrong direction for a cap whose purpose
/// is to keep the search from being misled by a too-small sample. Ties
/// broken by each layout's own exact identity (layout_key), not by its
/// position in `node.layouts` -- so two belief spaces that are pure
/// permutations of each other keep the same survivors, matching
/// state_key's/make_brute_force_cache_key's own order-independence rather
/// than silently depending on the caller's own enumeration order.
auto drop_lowest_posterior_layouts(BeliefNode const& node, std::uint64_t keep) -> BeliefNode;

/// What search() decides at one node before it would otherwise look up the
/// cache and branch: either `node` (or its pruned form) is already a leaf,
/// with `value` set, or it is not, with `continuation` holding what the
/// rest of search() must branch from.
struct LeafCheckResult
{
    /// Set when this node is a leaf; empty when search() must branch.
    std::optional<double> value;

    /// What to continue with when `value` is empty: `node` exactly as
    /// given (nothing needed pruning, or max_layouts is absent), or its
    /// pruned form. Also populated, to `node` unchanged, when `value` is
    /// set because the node was terminal -- a caller that only cares about
    /// `value` can ignore this field in that case.
    BeliefNode continuation;
};

/// The leaf decision search() makes for one node, factored out to a free
/// function search() itself calls -- not a parallel reimplementation of
/// its ordering -- so a test can exercise the actual production decision
/// without driving the whole recursion: is_terminal checked first, against
/// `node` exactly as given (a terminal leaf has no solver cost for
/// max_layouts to bound, only mass it would lose with nothing gained); only
/// then, if not terminal, max_layouts pruning, ahead of the depth-cutoff
/// check (cutoff_leaf_value's own per-layout DoubleDummyBound call is a
/// real cost worth bounding, unlike the terminal case). See search()'s own
/// `.cpp` comment for the full reasoning this mirrors.
auto check_leaf(
    BeliefNode const& node, int depth, BruteForceOptions const& options, LayoutBound const& bound,
    DeclarerObjective objective) -> LeafCheckResult;

/// A DeclarerStrategy backed by an exhaustive (or depth/size-bounded)
/// lookahead search over the belief space, keeping the best legal card
/// under a fixed DeclarerObjective -- the first lookahead-based (rather
/// than fixed-rule) declarer strategy in this module. The caveats worth
/// being explicit about on this class itself:
///
/// - **This presumes its own internal opponent model is what the paired
///   delta actually is.** Paired with a different delta, evaluate() still
///   returns a well-defined P_make -- just not "the double-dummy-optimal
///   P_make" this strategy's own search would compute against a model
///   matching its internal one.
/// - **The cache this owns is safe to share across this strategy's own
///   calls only when `max_depth` is absent.** With it absent (any number
///   of evaluate() runs, any order, within one instance's lifetime), a
///   node's value depends purely on its own content. With `max_depth`
///   set, the cutoff it triggers depends on *call-relative* recursion
///   depth -- the same logical node reached at a different depth by a
///   later call has a genuinely different correct answer -- so each
///   `play()` call starts from a clean cache instead; see `as_strategy()`'s
///   own `.cpp` comment. Never shared across two differently-configured
///   instances either way -- a different DeclarerObjective or a different
///   opponent model corrupts backed-up values silently, the same way
///   mixing SpreadPolicy values would for DoubleDummyDefender's own
///   instance-level policy.
/// - **The opponent model must answer from position alone, never from
///   `ObservationState::history` or `::play_record`.** `BruteForceCacheKey`
///   (brute_force_cache.hpp) deliberately omits both -- that omission is
///   exactly what lets the cache above reuse one node's value across
///   *different* calls reached by different real play (two different
///   `play()` calls along one actually-played line routinely differ in
///   `history`/`play_record` while sharing every field the key does
///   track). A `DefenderStrategy` is free, by its own contract, to read
///   `DefenderQuery::state` in full -- `ScriptedDefender` is a real example
///   that keys its own response off `state.history` -- but supplying one
///   as this strategy's `opponent_model` breaks the cache's own soundness:
///   two calls reaching "the same" node by different play could get
///   different opponent responses conflated under one cached value.
///   Nothing here enforces this (an arbitrary `std::function` offers no
///   seam to check), so it is a caller precondition, not a runtime-checked
///   one, the same way the two caveats above are.
///
/// `ctx` is not owned, and not thread-safe -- the same contract
/// DoubleDummyDefender already carries; see that class's own doxygen.
class BruteForceDeclarer
{
public:
    /// `opponent_model` left default (empty) resolves, on construction, to
    /// an owned DoubleDummyDefender over the same `ctx` -- the common
    /// case, with no need for a caller who wants it to construct one
    /// themselves.
    explicit BruteForceDeclarer(
        SolverContext& ctx, DeclarerObjective objective = DeclarerObjective::MaximizeExpectedTricks,
        DefenderStrategy opponent_model = nullptr, BruteForceOptions options = {});

    /// Deleted rather than left to the implicit default: when no
    /// `opponent_model` is supplied, `opponent_model_` is a lambda
    /// (`DoubleDummyDefender::as_strategy()`) that captures `this` as the
    /// address of *this instance's own* `default_opponent_` member. A
    /// copy or move would duplicate or relocate `default_opponent_` but
    /// leave `opponent_model_` still bound to the original's address --
    /// the destination would silently call back into the source (or, once
    /// the source is destroyed, into freed memory) instead of its own
    /// defender. Nothing about this class needs copying or moving --
    /// every caller constructs one in place and calls `as_strategy()` on
    /// it -- so deleting is the whole fix, not a stand-in for a real one.
    BruteForceDeclarer(BruteForceDeclarer const&) = delete;
    auto operator=(BruteForceDeclarer const&) -> BruteForceDeclarer& = delete;
    BruteForceDeclarer(BruteForceDeclarer&&) = delete;
    auto operator=(BruteForceDeclarer&&) -> BruteForceDeclarer& = delete;

    /// The returned DeclarerStrategy captures `this` and must not outlive
    /// this BruteForceDeclarer -- the same contract
    /// DoubleDummyDefender::as_strategy() already carries.
    auto as_strategy() -> DeclarerStrategy;

private:
    /// Lazily constructs bound_ from the first state.declarer this
    /// instance's play() is ever called with -- the constructor cannot do
    /// this itself, since no state is available yet at construction time.
    /// Asserts every subsequent call agrees: this strategy, like
    /// DoubleDummyBound itself, is used for one declarer seat for its
    /// whole lifetime.
    auto bound_for(ObservationState const& state) -> DoubleDummyBound&;

    /// The recursion: terminal_leaf_value/cutoff_leaf_value at a leaf,
    /// else a cache lookup, else branch on whose turn it is -- every
    /// legal card tried and the best kept at a declarer/dummy node
    /// (make_declarer_children, one call, every child), every surviving
    /// layout's own opponent_model_ response grouped and summed at a
    /// defender node (expand_defender_node) -- and the result stored back
    /// in the cache before returning. See this header's own class doxygen,
    /// and `specs/replenished-belief-evaluation.md`'s `BruteForceDeclarer`
    /// entry under "Key entry points", for why a plain sum/max needs no
    /// normalisation step given this class's own root-construction
    /// convention (kappa = 1, p = posterior).
    auto search(BeliefNode const& node, int depth) -> double;

    /// True when `seat` is declarer or dummy -- the same two-line check
    /// evaluate.cpp's own file-local is_declarer_side makes; duplicated
    /// here rather than shared, since it is genuinely two lines and
    /// evaluate.cpp's own copy is anonymous-namespace-private to that
    /// translation unit.
    static auto is_declarer_side(ObservationState const& state, int seat) -> bool;

    SolverContext& ctx_;
    DeclarerObjective objective_;
    DefenderStrategy opponent_model_;
    BruteForceOptions options_;
    BruteForceCache cache_;
    std::optional<DoubleDummyDefender> default_opponent_;  // engaged only when the caller supplied no opponent_model
    std::optional<DoubleDummyBound> bound_;                // lazily constructed; see bound_for()
    int bound_declarer_ = -1;                              // tracks bound_'s own declarer, for the assertion above
};

}  // namespace dds::belief_evaluation
