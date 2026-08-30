#include <belief_evaluation/evaluate.hpp>

#include <cassert>

#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/trick.hpp>
#include <utility/constants.h>

namespace
{
    auto is_declarer_side(ObservationState const& state, int seat) -> bool
    {
        int const dummy = (state.declarer + 2) % DDS_HANDS;
        return seat == state.declarer || seat == dummy;
    }

    /// Every card `seat` may legally play first at `deal`, expanded from
    /// legal_cards()'s per-suit bitmasks into a flat list — the shape
    /// make_declarer_children() and the root-child-value loop both want.
    auto enumerate_legal_cards(Deal const& deal, int seat) -> std::vector<Card>
    {
        std::array<unsigned, DDS_SUITS> const legal = legal_cards(deal, seat);
        std::vector<Card> cards;
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            for (int rank = 2; rank <= 14; ++rank)
            {
                if ((legal[suit] & (1u << rank)) != 0)
                {
                    cards.push_back(Card{suit, rank});
                }
            }
        }
        return cards;
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

    /// nodes_visited's single write site: every other counter this module
    /// will ever add is a candidate for its own such helper, but this one
    /// is shared between p_make (every recursive call) and evaluate() (the
    /// root, which dispatches the same way but outside p_make) — see
    /// evaluate()'s own root-handling block for the paired call.
    auto count_node(EvaluationCounters* counters) -> void
    {
        if (counters != nullptr)
        {
            counters->nodes_visited += 1;
        }
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
    /// `counters` is null unless EvaluateOptions::collect_counters was set;
    /// every write to it goes through count_node() so collection stays a
    /// single well-known site as more counters arrive.
    auto p_make(
        BeliefNode const& node,
        DeclarerStrategy const& pi,
        DefenderStrategy const& delta,
        std::optional<EvaluationError>& error,
        EvaluationCounters* counters,
        EvaluateOptions const& options) -> double
    {
        if (error.has_value())
        {
            return 0.0;
        }
        count_node(counters);
        if (already_made(node.state))
        {
            return node_mass(node);
        }
        if (is_dead(node.state))
        {
            return 0.0;  // node_mass(node) discarded here, not conserved -- the contract fails in
                          // every layout this node holds, whatever happens next
        }
        if (tier2_dead(node, options))
        {
            return 0.0;  // same non-conservation as tier 1's dead cut above -- see its own comment
        }
        // is_terminal(node) is never true here, not just its "made" branch: a
        // terminal node has tricks_remaining(node.state) == 0, and at that
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
        if (is_terminal(node))
        {
            return terminal_value(node);
        }

        int const seat = seat_on_play(node.state.known_holdings);

        if (is_declarer_side(node.state, seat))
        {
            ExpandResult const result = expand_declarer_node(node, pi);
            if (! result.child.has_value())
            {
                error = EvaluationError{
                    result.error, EvaluationCallback::DeclarerPlay, seat, node.state.known_holdings};
                return 0.0;
            }
            return p_make(*result.child, pi, delta, error, counters, options);
        }

        ExpandDefenderResult const result = expand_defender_node(node, delta);
        if (! result.children.has_value())
        {
            error = EvaluationError{
                result.error, EvaluationCallback::DefenderStrategy, seat, result.offending_layout};
            return 0.0;
        }

        KahanAccumulator total;
        for (BeliefNode const& child : *result.children)
        {
            total.add(p_make(child, pi, delta, error, counters, options));
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
    // Gated on !node.is_sample, unlike either tier-1 cut: those read only
    // tricks_won_by_declarer, tricks_needed and the outstanding pool, all
    // common knowledge identical across every layout the node holds
    // regardless of whether the node is a full space or a sample of one --
    // sampling removes layouts, it does not change how many cards are left
    // or how many tricks have been won. This cut instead concludes "every
    // layout in this node is dead" from the layouts the node happens to
    // hold; on a sample that is only "every layout *drawn* is dead", which
    // says nothing about every layout in the true space, so a layout that
    // would have made could simply not have been drawn. is_sample is
    // always false today -- nothing in this evaluator samples yet -- so
    // this is a no-op today and load-bearing only once something sets it
    // true.
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
    std::optional<BeliefNode> const root_opt = make_root(root_layout, declarer, tricks_needed, source);
    if (! root_opt.has_value())
    {
        EvaluationError const error{
            ValidationError::None, EvaluationCallback::RootConstruction, declarer, root_layout};
        return EvaluationResult{{}, error};
    }
    BeliefNode const& root = *root_opt;

    std::optional<EvaluationError> error;
    EvaluationValue value{};
    EvaluationCounters counters{};
    // Null unless options.collect_counters is set, so the counting call
    // sites below are unconditional and cost nothing when off — count_node()
    // itself is the single place that checks the flag (via nullness).
    EvaluationCounters* const counters_ptr = options.collect_counters ? &counters : nullptr;

    // The root's own visit — same site p_make() counts a node at, but
    // outside p_make() because the root's dispatch happens here rather than
    // through a p_make() call on itself.
    count_node(counters_ptr);

    if (already_made(root.state))
    {
        // Tier 1's already-made cut, mirrored here for the same reason
        // count_node() is: pi is never even asked which seat is on play,
        // because there is nothing left to decide -- the contract is made
        // in every layout the root holds before a single card is played.
        // root_children stays empty for the same reason it does at a
        // terminal root: no first-card decision exists to report
        // alternatives for.
        value.p_make = node_mass(root);
    }
    else if (is_dead(root.state))
    {
        // Tier 1's dead cut, mirrored here for the same reason: the
        // contract cannot be made from the root even in principle, so
        // p_make stays 0.0 and root_children stays empty -- there is no
        // point reporting alternatives for a first card when every one of
        // them leads to the same impossible outcome.
        value.p_make = 0.0;
    }
    else if (tier2_dead(root, options))
    {
        // Tier 2's cut, mirrored here for the same reason: every layout
        // the root holds is dead by the injected bound, under the
        // caller's own double-dummy-optimal declaration.
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
                    ? p_make(*chosen.child, pi, delta, error, counters_ptr, options)
                    : p_make(other_children[other_i++], pi, delta, error, counters_ptr, options);
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
                double const child_value = p_make(child, pi, delta, error, counters_ptr, options);
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
