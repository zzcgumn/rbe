#include <belief_evaluation/evaluate.hpp>

#include <cassert>

#include <api/dds_constants.hpp>
#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/replenishment.hpp>
#include <belief_evaluation/trick.hpp>

namespace dds::belief_evaluation
{

namespace
{
    auto is_declarer_side(ObservationState const& state, int seat) -> bool
    {
        int const dummy = (state.declarer + 2) % DDS_HANDS;
        return seat == state.declarer || seat == dummy;
    }

    /// Builds the EvaluationError for a declarer ExpandResult with no child.
    /// Used at both expand_declarer_node call sites. If `pi` threw
    /// DeclarerStrategyContractViolation, reports the delegate's seat and
    /// layout as EvaluationCallback::DefenderStrategy. Otherwise `pi` returned
    /// a bad card: EvaluationCallback::DeclarerPlay, with this node's seat and
    /// known_holdings.
    auto declarer_expand_error(
        ExpandResult const& result, int seat, Deal const& known_holdings) -> EvaluationError
    {
        if (result.nested_seat.has_value())
        {
            return EvaluationError{
                result.error, EvaluationCallback::DefenderStrategy, *result.nested_seat,
                *result.nested_layout};
        }
        return EvaluationError{result.error, EvaluationCallback::DeclarerPlay, seat, known_holdings};
    }

    /// The card that led to a defender child: the last history entry, added
    /// by advance_state().
    auto last_played_card(BeliefNode const& node) -> Card
    {
        assert(node.state.history.number > 0);
        int const n = node.state.history.number - 1;
        return Card{node.state.history.suit[n], node.state.history.rank[n]};
    }

    // --- counters -----------------------------------------------------
    //
    // One helper per counter, each its only write site, each checking for a
    // null `counters`. All but the last are called from p_make() and from the
    // root block of evaluate().

    auto count_node(EvaluationCounters* counters) -> void
    {
        if (counters != nullptr)
        {
            counters->nodes_visited += 1;
        }
    }

    auto count_tier1_made_cut(EvaluationCounters* counters) -> void
    {
        if (counters != nullptr)
        {
            counters->tier1_made_cuts += 1;
        }
    }

    auto count_tier1_dead_cut(EvaluationCounters* counters) -> void
    {
        if (counters != nullptr)
        {
            counters->tier1_dead_cuts += 1;
        }
    }

    auto count_tier2_cut(EvaluationCounters* counters) -> void
    {
        if (counters != nullptr)
        {
            counters->tier2_cuts += 1;
        }
    }

    /// Called for every node reached, with the same node and depth as
    /// count_node().
    auto record_sample_size(EvaluationCounters* counters, BeliefNode const& node, int depth) -> void
    {
        if (counters == nullptr)
        {
            return;
        }
        auto const index = static_cast<std::size_t>(depth);
        if (index >= counters->sample_size_by_depth.size())
        {
            counters->sample_size_by_depth.resize(index + 1);
        }
        DepthSampleStats& stats = counters->sample_size_by_depth[index];
        auto const layouts = static_cast<std::uint64_t>(node.layouts.size());
        if (stats.nodes == 0 || layouts < stats.layout_min)
        {
            stats.layout_min = layouts;
        }
        stats.layout_sum += layouts;
        stats.nodes += 1;
    }

    /// Called only from replenish_node(). The root is never replenished.
    auto record_replenishment_attempt(
        EvaluationCounters* counters,
        int depth,
        bool succeeded,
        std::uint64_t layouts_added,
        std::uint64_t at_calls) -> void
    {
        if (counters == nullptr)
        {
            return;
        }
        auto const index = static_cast<std::size_t>(depth);
        if (index >= counters->replenishment_by_depth.size())
        {
            counters->replenishment_by_depth.resize(index + 1);
        }
        DepthReplenishmentStats& stats = counters->replenishment_by_depth[index];
        stats.attempted += 1;
        stats.succeeded += succeeded ? 1 : 0;
        stats.layouts_added += layouts_added;
        stats.at_calls += at_calls;
    }

    /// What the recursion carries unchanged from the root down. Add read-only
    /// context here, not to every parameter list.
    ///
    /// Not members: `error`, a mutable out-parameter; `depth`, which changes
    /// per call; `declarer` and `tricks_needed`, already in `node.state`.
    ///
    /// `root_layout` and `source` are owned by the caller of evaluate().
    struct SearchContext
    {
        DeclarerStrategy const& pi;
        DefenderStrategy const& delta;
        EvaluateOptions const& options;
        EvaluationCounters* counters;  // null unless collecting
        Deal const& root_layout;
        LayoutSource const& source;
    };

    /// Tops `node` up towards `sample_size` when it has fewer than
    /// `replenish_below` layouts. Rescales `kappa` so the mass is unchanged.
    /// Clears `is_sample` when the scan reaches `SourceExhausted`.
    ///
    /// Returns `std::nullopt` when nothing changed; the caller then uses
    /// `node`. The trigger is the layout count only (see
    /// `SamplingOptions::replenish_below`).
    ///
    /// The rescale is exact: with Kahan sums `E` and `E'` of `p` before and
    /// after, `kappa' = kappa * E / E'`. Skipped when nothing was added, so an
    /// unchanged node is exactly unchanged.
    ///
    /// A `delta` violation during the scan sets `error` and returns
    /// `std::nullopt`. **The caller must check `error` before using `node`.**
    auto replenish_node(
        BeliefNode const& node,
        SearchContext const& ctx,
        int depth,
        std::optional<EvaluationError>& error) -> std::optional<BeliefNode>
    {
        if (! ctx.options.sampling.replenish_below.has_value())
        {
            return std::nullopt;
        }
        if (! ctx.options.sampling.sample_size.has_value())
        {
            return std::nullopt;  // no target to top up to
        }
        if (node.layouts.size() >= *ctx.options.sampling.replenish_below)
        {
            return std::nullopt;  // trigger not met
        }
        if (node.no_more_available)
        {
            // An earlier scan on this path exhausted the source, so no scan below can
            // find anything. Checked after the cheap trigger, before the costly scan.
            return std::nullopt;
        }

        std::uint64_t const target = *ctx.options.sampling.sample_size;
        std::uint64_t const current = static_cast<std::uint64_t>(node.layouts.size());
        std::uint64_t const wanted = (target > current) ? (target - current) : 0;

        ScanResult const scan = scan_for_replenishment(
            node, ctx.root_layout, ctx.source, ctx.delta, wanted, ctx.options.sampling.scan_budget);
        record_replenishment_attempt(
            ctx.counters, depth, ! scan.candidates.empty(), scan.candidates.size(), scan.at_calls);
        if (scan.error != ValidationError::None)
        {
            error =
                EvaluationError{scan.error, EvaluationCallback::DefenderStrategy, scan.seat, scan.offending_layout};
            return std::nullopt;
        }
        bool const exhausted = scan.outcome == ScanOutcome::SourceExhausted;
        if (scan.candidates.empty() && ! exhausted)
        {
            return std::nullopt;  // nothing found yet; node unchanged
        }

        BeliefNode replenished = node;
        if (! scan.candidates.empty())
        {
            KahanAccumulator mass_before;
            for (Probability const p_i : replenished.p)
            {
                mass_before.add(p_i);
            }

            for (ScanCandidate const& candidate : scan.candidates)
            {
                replenished.layouts.push_back(candidate.layout);
                replenished.p.push_back(candidate.p_j);
                replenished.root_keys.push_back(candidate.root_key);
            }

            KahanAccumulator mass_after;
            for (Probability const p_i : replenished.p)
            {
                mass_after.add(p_i);
            }

            // The "nothing added" case returned above, so this never divides by zero.
            replenished.kappa *= mass_before.value() / mass_after.value();
        }

        // The scan reached the end of source, so this node holds every layout its
        // path admits. That does not change the parent. tier2_dead() still gates
        // on !is_sample alone.
        if (exhausted)
        {
            replenished.is_sample = false;
            replenished.no_more_available = true;
        }
        return replenished;
    }

    /// The recursion: terminal_value(node), the sum over children (defender
    /// node), or the one child's value (declarer node).
    ///
    /// Once `error` is set, every call returns 0 without recursing. Call sites
    /// already stop when `error` is set, so no test reaches this guard. Keep
    /// it for future call sites.
    ///
    /// `ctx.counters` is null unless collect_counters was set.
    ///
    /// `depth` is the distance from the root. evaluate() handles the root
    /// itself, so every call here passes `depth + 1`.
    auto p_make(
        BeliefNode const& node,
        SearchContext const& ctx,
        int depth,
        std::optional<EvaluationError>& error) -> double
    {
        if (error.has_value())
        {
            return 0.0;
        }
        count_node(ctx.counters);

        // Replenish at node entry: before every cut, and before any BeliefView is
        // built (only expand_declarer_node builds one). See replenish_node. Without
        // replenish_below this is one call that changes nothing.
        std::optional<BeliefNode> const replenished = replenish_node(node, ctx, depth, error);
        if (error.has_value())
        {
            return 0.0;
        }
        BeliefNode const& n = replenished.has_value() ? *replenished : node;

        record_sample_size(ctx.counters, n, depth);
        if (already_made(n.state))
        {
            count_tier1_made_cut(ctx.counters);
            return node_mass(n);
        }
        if (is_dead(n.state))
        {
            count_tier1_dead_cut(ctx.counters);
            return 0.0;  // mass not conserved: the contract fails in every layout here
        }
        if (tier2_dead(n, ctx.options))
        {
            count_tier2_cut(ctx.counters);
            return 0.0;  // mass not conserved, as for the tier 1 dead cut
        }
        // Never true here: at a terminal, already_made() and is_dead() are exact
        // complements, so a tier 1 cut has already returned. Kept as a fallback,
        // like the error check above.
        if (is_terminal(n))
        {
            return terminal_value(n);
        }

        int const seat = seat_on_play(n.state.known_holdings);

        if (is_declarer_side(n.state, seat))
        {
            ExpandResult const result = expand_declarer_node(n, ctx.pi);
            if (! result.child.has_value())
            {
                error = declarer_expand_error(result, seat, n.state.known_holdings);
                return 0.0;
            }
            return p_make(*result.child, ctx, depth + 1, error);
        }

        ExpandDefenderResult const result = expand_defender_node(n, ctx.delta);
        if (! result.children.has_value())
        {
            error = EvaluationError{
                result.error, EvaluationCallback::DefenderStrategy, seat, result.offending_layout};
            return 0.0;
        }

        KahanAccumulator total;
        for (BeliefNode const& child : *result.children)
        {
            total.add(p_make(child, ctx, depth + 1, error));
            if (error.has_value())
            {
                return 0.0;
            }
        }
        return total.value();
    }
}

auto already_made(ObservationState const& state) -> bool
{
    return state.tricks_won_by_declarer >= state.tricks_needed;
}

auto is_dead(ObservationState const& state) -> bool
{
    return state.tricks_won_by_declarer + tricks_remaining(state) < state.tricks_needed;
}

auto tier2_dead(BeliefNode const& node, EvaluateOptions const& options) -> bool
{
    // Gated on !node.is_sample, unlike the tier 1 cuts. See the header.
    if (node.is_sample)
    {
        return false;
    }
    if (! options.delta_is_double_dummy_optimal || ! options.bound)
    {
        return false;
    }
    int const still_needed = node.state.tricks_needed - node.state.tricks_won_by_declarer;
    for (Deal const& layout : node.layouts)
    {
        if (options.bound(layout) >= still_needed)
        {
            return false;  // one live layout suppresses the cut
        }
    }
    return true;
}

auto evaluate(
    Deal const& root_layout,
    int declarer,
    int tricks_needed,
    LayoutSource const& source,
    DeclarerStrategy const& pi,
    DefenderStrategy const& delta,
    EvaluateOptions const& options) -> EvaluationResult
{
    PlayRecord const* const record = options.play_record.has_value() ? &*options.play_record : nullptr;
    RootConstructionResult const root_result = make_root(
        root_layout,
        declarer,
        tricks_needed,
        source,
        RootOptions{.sample_size = options.sampling.sample_size, .scan_budget = options.sampling.scan_budget},
        record);
    if (! root_result.node.has_value())
    {
        EvaluationError const error{
            ValidationError::None,
            EvaluationCallback::RootConstruction,
            declarer,
            root_layout,
            root_result.failure};
        return EvaluationResult{{}, error};
    }
    BeliefNode const& root = *root_result.node;

    std::optional<EvaluationError> error;
    EvaluationValue value{};
    EvaluationCounters counters{};
    // Null unless collect_counters is set. The helpers check for null, so the
    // calls below are unconditional.
    EvaluationCounters* const counters_ptr = options.collect_counters ? &counters : nullptr;
    SearchContext const ctx{pi, delta, options, counters_ptr, root_layout, source};

    // The root is counted here, because it is dispatched here, not in p_make().
    count_node(counters_ptr);
    record_sample_size(counters_ptr, root, /*depth=*/0);

    // Set before the dispatch, so that the cut paths set it too.
    value.root_is_declaring_side =
        is_declarer_side(root.state, seat_on_play(root.state.known_holdings));

    if (already_made(root.state))
    {
        // Tier 1 made cut. pi is never called: the contract is already made, so
        // root_children stays empty.
        count_tier1_made_cut(counters_ptr);
        value.p_make = node_mass(root);
    }
    else if (is_dead(root.state))
    {
        // Tier 1 dead cut: p_make stays 0.0 and root_children stays empty.
        count_tier1_dead_cut(counters_ptr);
        value.p_make = 0.0;
    }
    else if (tier2_dead(root, options))
    {
        // Tier 2 cut: every root layout is dead by the bound.
        count_tier2_cut(counters_ptr);
        value.p_make = 0.0;
    }
    else if (is_terminal(root))
    {
        // Unreachable, as in p_make(). Kept as a fallback.
        value.p_make = terminal_value(root);  // no legal first card exists; root_children stays empty
    }
    else
    {
        int const seat = seat_on_play(root.state.known_holdings);

        if (is_declarer_side(root.state, seat))
        {
            // pi is called once at the root, to learn its choice. The other legal
            // cards' children come from make_declarer_children(), which does not call
            // pi. The chosen card's child is reused.
            ExpandResult const chosen = expand_declarer_node(root, pi);
            if (! chosen.child.has_value())
            {
                error = declarer_expand_error(chosen, seat, root.state.known_holdings);
                return EvaluationResult{{}, error};
            }

            std::vector<Card> const legal = enumerate_legal_cards(root.state.known_holdings, seat);
            std::size_t chosen_index = legal.size();
            for (std::size_t i = 0; i < legal.size(); ++i)
            {
                if (legal[i].suit == chosen.card.suit && legal[i].rank == chosen.card.rank)
                {
                    chosen_index = i;
                    break;
                }
            }
            // chosen.card passed validate_declarer_card, which applies the same
            // follow-suit rule to the same Deal. So it is in legal.
            assert(chosen_index < legal.size());

            std::vector<Card> other_cards;
            other_cards.reserve(legal.size());
            for (std::size_t i = 0; i < legal.size(); ++i)
            {
                if (i != chosen_index)
                {
                    other_cards.push_back(legal[i]);
                }
            }
            std::vector<BeliefNode> const other_children = make_declarer_children(root, other_cards);

            value.root_children.reserve(legal.size());
            std::size_t other_i = 0;
            for (std::size_t i = 0; i < legal.size(); ++i)
            {
                double const candidate_value = (i == chosen_index)
                    ? p_make(*chosen.child, ctx, /*depth=*/1, error)
                    : p_make(other_children[other_i++], ctx, /*depth=*/1, error);
                if (error.has_value())
                {
                    return EvaluationResult{{}, error};
                }
                value.root_children.push_back(RootChildValue{legal[i], candidate_value});
                if (i == chosen_index)
                {
                    value.p_make = candidate_value;
                }
            }
        }
        else
        {
            ExpandDefenderResult const expanded = expand_defender_node(root, delta);
            if (! expanded.children.has_value())
            {
                error = EvaluationError{
                    expanded.error,
                    EvaluationCallback::DefenderStrategy,
                    seat,
                    expanded.offending_layout};
                return EvaluationResult{{}, error};
            }

            KahanAccumulator total;
            value.root_children.reserve(expanded.children->size());
            for (BeliefNode const& child : *expanded.children)
            {
                double const child_value = p_make(child, ctx, /*depth=*/1, error);
                if (error.has_value())
                {
                    return EvaluationResult{{}, error};
                }
                total.add(child_value);
                value.root_children.push_back(RootChildValue{last_played_card(child), child_value});
            }
            value.p_make = total.value();
        }
    }

    if (options.retain_root)
    {
        value.retained_root = root;
        // retained_root outlives the call, but options may not. Clear the
        // pointer rather than leave it dangling. See EvaluationValue::retained_root.
        value.retained_root->state.play_record = nullptr;
    }
    if (options.collect_counters)
    {
        value.counters = counters;
    }

    EvaluationResult result{};
    result.by_strategy.emplace(pi.id, std::move(value));
    return result;
}

}  // namespace dds::belief_evaluation
