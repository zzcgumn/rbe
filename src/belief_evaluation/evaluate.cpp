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

    /// The card whose history entry a defender child just recorded — the
    /// trailing entry advance_state() (expand.cpp) appended when building
    /// it. BeliefNode itself does not track "which card led here" any more
    /// directly than that.
    auto last_played_card(BeliefNode const& node) -> Card
    {
        assert(node.state.history.number > 0);
        int const n = node.state.history.number - 1;
        return Card{node.state.history.suit[n], node.state.history.rank[n]};
    }

    // --- counters -----------------------------------------------------
    //
    // One small helper per counter, each its counter's single write site
    // and each checking the null `counters` pointer the same way. All but
    // the last are called from two places: p_make(), and evaluate()'s own
    // root-handling block, which dispatches the same way from outside
    // p_make.

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

    /// Called with the same node and depth count_node() uses, so every
    /// node reached is recorded here, not only ones a cut later touches.
    /// Grows the vector on demand — see
    /// EvaluationCounters::sample_size_by_depth for why a trailing
    /// zero-entry must never mean "reached but empty".
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

    /// The exception to the two-call-site pattern above: this one is
    /// called only from replenish_node(). Replenishment never happens at
    /// the root, so a root-block call could only ever record zeros.
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

    /// Everything the recursion carries unchanged from the root down.
    /// Held and passed by const reference; widening it is how the
    /// recursion gains read-only context without touching every call
    /// site's parameter list.
    ///
    /// Three things are deliberately *not* members. `error` is a mutable
    /// out-parameter, and burying one in a struct named "context" would
    /// stop it looking like one. `depth` genuinely differs per call.
    /// `declarer` and `tricks_needed` are already reachable through
    /// `node.state`, and a second path to the same fact could drift.
    ///
    /// `root_layout` and `source` reference objects owned by evaluate()'s
    /// caller, outliving the whole recursion.
    struct SearchContext
    {
        DeclarerStrategy const& pi;
        DefenderStrategy const& delta;
        EvaluateOptions const& options;
        EvaluationCounters* counters;  // null unless collecting
        Deal const& root_layout;
        LayoutSource const& source;
    };

    /// Tops `node` back up towards `sample_size` when its layout count is
    /// below `replenish_below`, rescaling `kappa` so the node's mass is
    /// unchanged, and clearing `is_sample` when the scan reaches
    /// `ScanOutcome::SourceExhausted`.
    ///
    /// Returns `std::nullopt` whenever nothing changed — no threshold, the
    /// trigger unmet, no `sample_size` to top up to, or a scan that found
    /// nothing and did not exhaust the source. The caller then uses `node`
    /// itself, not a copy.
    ///
    /// The trigger is the layout count and **nothing else**: see
    /// `SamplingOptions::replenish_below`.
    ///
    /// The rescale is algebraically exact — with `E` and `E'` the Kahan
    /// sums of `p` before and after, `kappa' = kappa * E / E'` gives
    /// `kappa' * E' = kappa * E`. Skipped when the scan added nothing, so
    /// that "untouched" is a guarantee rather than "usually 1.0".
    ///
    /// A `delta` contract violation during the scan sets `error` (the same
    /// `EvaluationError` shape `expand_defender_node` uses — same callback,
    /// same contract) and returns `std::nullopt`, so the caller must check
    /// `error` before falling back to `node`.
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
            return std::nullopt;  // no target to top up to -- see this field's own doxygen
        }
        if (node.layouts.size() >= *ctx.options.sampling.replenish_below)
        {
            return std::nullopt;  // trigger not met
        }
        if (node.no_more_available)
        {
            // A scan somewhere on this path already reached the end of
            // source and found this node's own narrower history was not
            // among what survived -- see that field's own doxygen for why
            // scanning again below here cannot find anything new either.
            // Checked after the trigger (cheap) but before the scan
            // (expensive), which is the whole point of carrying it.
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
            return std::nullopt;  // the ordinary "nothing more available (yet)" outcome; node is unchanged
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

            // See this function's own doxygen for why the "nothing added"
            // case above returns before ever reaching this division.
            replenished.kappa *= mass_before.value() / mass_after.value();
        }

        // The node's own scan reached the end of source: it now holds
        // every layout its path admits, exactly as make_root's root-level
        // scan does when it runs to completion. Not a refinement of
        // tier2_dead()'s gate -- that stays exactly !node.is_sample, no
        // floor -- this only lets a node report the flag honestly once it
        // genuinely holds the whole of its own remaining space. Does not
        // propagate upward: a child's own exhaustion says nothing about
        // its parent, whose own layout set is still whatever prefix was
        // drawn for it.
        //
        // no_more_available is set from this exact same signal -- see its
        // own doxygen for why it is nonetheless a separate field from
        // is_sample, not a second name for the same fact.
        if (exhausted)
        {
            replenished.is_sample = false;
            replenished.no_more_available = true;
        }
        return replenished;
    }

    /// The recursion: P_make(node) = terminal_value(node), or the sum (for
    /// a defender node) / the single value (for a declarer node) over its
    /// children. Once `error` is set, every further call is a no-op
    /// returning 0 rather than continuing to recurse on an invariant a
    /// callback has already violated. Every call site also stops issuing
    /// further calls itself as soon as `error` is set (there is currently
    /// no path that would reach this function again without checking
    /// first), so this guard is a structural invariant for callers to rely
    /// on rather than a behaviour any current test can isolate — keep it
    /// so that stays true as call sites are added, not because it is
    /// reachable today.
    ///
    /// `ctx.counters` is null unless EvaluateOptions::collect_counters was
    /// set; every write to it goes through count_node() so collection
    /// stays a single well-known site as more counters arrive.
    ///
    /// `depth` is the node's distance from the root, which is depth 0.
    /// evaluate() dispatches the root itself, outside this function (see
    /// its own root-handling block, which mirrors every cut here for that
    /// reason), so the root never reaches this function and every call
    /// site below passes `depth + 1` -- there is no call site that passes
    /// depth 0.
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

        // Replenishment fires here: at node entry, before every cut below
        // and before is_terminal()/expansion -- in particular before any
        // BeliefView could be built (expand_declarer_node is the only place
        // one is, and it has not been reached yet). See replenish_node's
        // own doxygen for why this ordering is a stated constraint the rest
        // of the module leans on, not an incidental choice. Absent
        // options.sampling.replenish_below, replenish_node returns nullopt
        // having touched nothing, so this costs one function call and
        // nothing else on the path every existing test still takes.
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
            return 0.0;  // node_mass(n) discarded here, not conserved -- the contract fails in
                          // every layout this node holds, whatever happens next
        }
        if (tier2_dead(n, ctx.options))
        {
            count_tier2_cut(ctx.counters);
            return 0.0;  // same non-conservation as tier 1's dead cut above -- see its own comment
        }
        // is_terminal(n) is never true here, not just its "made" branch: a
        // terminal node has tricks_remaining(n.state) == 0, and at that
        // point already_made() and is_dead() are exact logical complements
        // (tricks_won >= needed vs. tricks_won + 0 < needed), so one of the
        // two tier-1 checks above has already returned before this line could
        // run. Kept anyway, the same reasoning this function's own error
        // check above is kept for: a structural invariant for today's call
        // sites to rely on, not a behaviour any current test can isolate --
        // safe to keep as tier-1 coverage evolves, not because it is
        // reachable today. is_terminal()/terminal_value() remain genuinely
        // used elsewhere (direct construction in terminal_test.cpp, per their
        // own doxygen); this is only about the two call sites inside the
        // recursion.
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
                error = EvaluationError{
                    result.error, EvaluationCallback::DeclarerPlay, seat, n.state.known_holdings};
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
    // Gated on !node.is_sample, unlike either tier-1 cut -- see this
    // function's own doxygen for the soundness argument.
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
    RootConstructionResult const root_result = make_root(
        root_layout,
        declarer,
        tricks_needed,
        source,
        RootOptions{.sample_size = options.sampling.sample_size, .scan_budget = options.sampling.scan_budget});
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
    // Null unless options.collect_counters is set, so the counting call
    // sites below are unconditional and cost nothing when off — count_node()
    // itself is the single place that checks the flag (via nullness).
    EvaluationCounters* const counters_ptr = options.collect_counters ? &counters : nullptr;
    SearchContext const ctx{pi, delta, options, counters_ptr, root_layout, source};

    // The root's own visit — same site p_make() counts a node at, but
    // outside p_make() because the root's dispatch happens here rather than
    // through a p_make() call on itself.
    count_node(counters_ptr);
    record_sample_size(counters_ptr, root, /*depth=*/0);

    if (already_made(root.state))
    {
        // Tier 1's already-made cut. pi is never even asked which seat is
        // on play: the contract is made in every layout the root holds
        // before a card is played, so there is no first-card decision to
        // report alternatives for and root_children stays empty.
        count_tier1_made_cut(counters_ptr);
        value.p_make = node_mass(root);
    }
    else if (is_dead(root.state))
    {
        // Tier 1's dead cut: the contract cannot be made from the root
        // even in principle, so p_make stays 0.0 and root_children stays
        // empty -- every first card leads to the same outcome.
        count_tier1_dead_cut(counters_ptr);
        value.p_make = 0.0;
    }
    else if (tier2_dead(root, options))
    {
        // Tier 2's cut: every layout the root holds is dead by the
        // injected bound, under the caller's own declaration.
        count_tier2_cut(counters_ptr);
        value.p_make = 0.0;
    }
    else if (is_terminal(root))
    {
        // Unreachable today for the same reason p_make()'s own is_terminal()
        // check is -- see that call site's comment. Kept as the same
        // structural fallback, not because a root can reach here now.
        value.p_make = terminal_value(root);  // no legal first card exists; root_children stays empty
    }
    else
    {
        int const seat = seat_on_play(root.state.known_holdings);

        if (is_declarer_side(root.state, seat))
        {
            // pi is called exactly once at the root — here — to learn its
            // actual choice (validated the same way any other node is).
            // The other legal cards' subtrees are then built directly via
            // make_declarer_children(), which does not call pi, and
            // evaluated by recursing pi/delta normally from there. The
            // chosen card's own child (chosen.child) is reused rather than
            // rebuilt a second time through make_declarer_children().
            ExpandResult const chosen = expand_declarer_node(root, pi);
            if (! chosen.child.has_value())
            {
                error = EvaluationError{
                    chosen.error, EvaluationCallback::DeclarerPlay, seat, root.state.known_holdings};
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
            // chosen.card already passed validate_declarer_card, and
            // enumerate_legal_cards()/legal_cards() computes the same
            // follow-suit rule over the same Deal (root.state.known_holdings),
            // so chosen.card is provably present in legal. Asserted, not just
            // commented, per this file's own convention for preconditions
            // that hold today but aren't otherwise enforced (see
            // last_played_card and trick_complete_winner).
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
