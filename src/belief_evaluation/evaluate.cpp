#include <belief_evaluation/evaluate.hpp>

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
        int const n = node.state.history.number - 1;
        return Card{node.state.history.suit[n], node.state.history.rank[n]};
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
    auto p_make(
        BeliefNode const& node,
        DeclarerStrategy const& pi,
        DefenderStrategy const& delta,
        std::optional<EvaluationError>& error) -> double
    {
        if (error.has_value())
        {
            return 0.0;
        }
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
            return p_make(*result.child, pi, delta, error);
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
            total.add(p_make(child, pi, delta, error));
            if (error.has_value())
            {
                return 0.0;
            }
        }
        return total.value();
    }
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

    if (is_terminal(root))
    {
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
            // evaluated by recursing pi/delta normally from there.
            ExpandResult const chosen = expand_declarer_node(root, pi);
            if (! chosen.child.has_value())
            {
                error = EvaluationError{
                    chosen.error, EvaluationCallback::DeclarerPlay, seat, root.state.known_holdings};
                return EvaluationResult{{}, error};
            }

            std::vector<Card> const legal = enumerate_legal_cards(root.state.known_holdings, seat);
            std::vector<BeliefNode> const candidates = make_declarer_children(root, legal);
            value.root_children.reserve(candidates.size());
            for (std::size_t i = 0; i < candidates.size(); ++i)
            {
                double const candidate_value = p_make(candidates[i], pi, delta, error);
                if (error.has_value())
                {
                    return EvaluationResult{{}, error};
                }
                value.root_children.push_back(RootChildValue{legal[i], candidate_value});
                if (legal[i].suit == chosen.card.suit && legal[i].rank == chosen.card.rank)
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
                double const child_value = p_make(child, pi, delta, error);
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

    EvaluationResult result{};
    result.by_strategy.emplace(pi.id, std::move(value));
    return result;
}
