#pragma once

#include <optional>

#include <belief_evaluation/brute_force_cache.hpp>
#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/double_dummy_bound.hpp>
#include <belief_evaluation/double_dummy_defender.hpp>
#include <belief_evaluation/node.hpp>

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
    MaximiseExpectedTricks,
    MaximiseProbabilityToMake,
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
    /// further -- see drop_lowest_posterior_layouts(). The dropped mass is
    /// never redistributed; this is a deliberate under-count, not an
    /// approximation that could go either way.
    std::optional<std::uint64_t> max_layouts;
};

/// The node's value when `is_terminal(node)` already holds: `terminal_value()`
/// reused verbatim for `MaximiseProbabilityToMake` (made/not-made is
/// already what that function answers), and the node's own mass-weighted
/// trick count for `MaximiseExpectedTricks` -- `tricks_won_by_declarer` is
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

/// A DeclarerStrategy backed by an exhaustive (or depth/size-bounded)
/// lookahead search over the belief space, keeping the best legal card
/// under a fixed DeclarerObjective -- the first lookahead-based (rather
/// than fixed-rule) declarer strategy in this module. The two caveats
/// worth being explicit about on this class itself:
///
/// - **This presumes its own internal opponent model is what the paired
///   delta actually is.** Paired with a different delta, evaluate() still
///   returns a well-defined P_make -- just not "the double-dummy-optimal
///   P_make" this strategy's own search would compute against a model
///   matching its internal one.
/// - **The cache this owns is safe to share across this strategy's own
///   calls** (any number of evaluate() runs, any order, within one
///   instance's lifetime), **never across two differently-configured
///   instances** -- a different DeclarerObjective or a different opponent
///   model corrupts backed-up values silently, the same way mixing
///   SpreadPolicy values would for DoubleDummyDefender's own
///   instance-level policy.
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
        SolverContext& ctx, DeclarerObjective objective = DeclarerObjective::MaximiseExpectedTricks,
        DefenderStrategy opponent_model = nullptr, BruteForceOptions options = {});

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
