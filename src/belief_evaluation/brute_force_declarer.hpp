#pragma once

#include <optional>

#include <belief_evaluation/brute_force_cache.hpp>
#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
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

/// A DeclarerStrategy backed by an exhaustive (or depth/size-bounded)
/// lookahead search over the belief space, keeping the best legal card
/// under a fixed DeclarerObjective. Ported from BridgeLibraries-private's
/// own BeliefSpaceSearch; see brute_force_strategy.md
/// (../rbe-notes/plans/, not committed here) for the full design
/// rationale. The two caveats worth repeating on this class itself, not
/// only in a plan:
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
    SolverContext& ctx_;
    DeclarerObjective objective_;
    DefenderStrategy opponent_model_;
    BruteForceOptions options_;
    BruteForceCache cache_;
    std::optional<DoubleDummyDefender> default_opponent_;  // engaged only when the caller supplied no opponent_model
};

}  // namespace dds::belief_evaluation
