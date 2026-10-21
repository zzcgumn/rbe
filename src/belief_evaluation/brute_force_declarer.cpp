#include <belief_evaluation/brute_force_declarer.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <api/dds_constants.hpp>

#include <belief_evaluation/expand.hpp>
#include <belief_evaluation/kahan.hpp>
#include <belief_evaluation/layout_key.hpp>
#include <belief_evaluation/trick.hpp>

namespace dds::belief_evaluation
{

namespace
{
    // The internal root: kappa = 1.0 and p = the view's posteriors, which sum
    // to 1. So node_mass(root) == 1.0, and every value in the search is
    // already normalised. root_keys holds each layout's index. The search
    // never reads it, but expand_defender_node asserts its length.
    auto make_internal_root(ObservationState const& state, BeliefView const& view) -> BeliefNode
    {
        BeliefNode root{};
        root.state = state;
        root.kappa = 1.0;
        root.layouts.reserve(view.entries.size());
        root.p.reserve(view.entries.size());
        root.root_keys.reserve(view.entries.size());
        for (std::size_t i = 0; i < view.entries.size(); ++i)
        {
            root.layouts.push_back(view.entries[i].layout);
            root.p.push_back(view.entries[i].posterior);
            root.root_keys.push_back(static_cast<std::uint64_t>(i));
        }
        return root;
    }

    // Serialises only the belief-space part of BruteForceCacheKey. state_key
    // declares what play() reads *beyond* the position, and the evaluator
    // adds the position itself. Including it would make the key needlessly
    // coarse. Not used yet: evaluate() never calls state_key. Equal keys must
    // give equal strings; no other format rule applies.
    auto serialize_key(BruteForceCacheKey const& key) -> StateKey
    {
        std::string bytes;
        bytes.reserve(key.deal_and_posterior.size() * 24);
        auto const append = [&bytes](auto const& value)
        {
            bytes.append(reinterpret_cast<char const*>(&value), sizeof(value));
        };
        for (auto const& [defender_key, other_defender_key, posterior] : key.deal_and_posterior)
        {
            append(defender_key);
            append(other_defender_key);
            append(posterior);
        }
        return bytes;
    }
}

auto terminal_leaf_value(BeliefNode const& node, DeclarerObjective objective) -> double
{
    if (objective == DeclarerObjective::MaximizeExpectedTricks)
    {
        return node_mass(node) * static_cast<double>(node.state.tricks_won_by_declarer);
    }
    return terminal_value(node);
}

auto cutoff_leaf_value(BeliefNode const& node, LayoutBound const& bound, DeclarerObjective objective)
    -> double
{
    KahanAccumulator total;
    for (std::size_t i = 0; i < node.layouts.size(); ++i)
    {
        int const declarer_total_tricks = node.state.tricks_won_by_declarer + bound(node.layouts[i]);
        double const per_layout_value = (objective == DeclarerObjective::MaximizeExpectedTricks)
            ? static_cast<double>(declarer_total_tricks)
            : (declarer_total_tricks >= node.state.tricks_needed ? 1.0 : 0.0);
        total.add(node.p[i] * per_layout_value);
    }
    return node.kappa * total.value();
}

auto drop_lowest_posterior_layouts(BeliefNode const& node, std::uint64_t keep) -> BeliefNode
{
    if (static_cast<std::uint64_t>(node.layouts.size()) <= keep)
    {
        return node;
    }

    // A fixed defender seat, used only for the tie-break key.
    int const defender_seat = (node.state.declarer + 1) % DDS_HANDS;

    std::vector<std::size_t> indices(node.layouts.size());
    for (std::size_t i = 0; i < indices.size(); ++i)
    {
        indices[i] = i;
    }
    // Descending by posterior. Ties go by layout_key, not by index, so the
    // survivors do not depend on enumeration order. A permuted belief space
    // keeps the same layouts, as state_key and the cache key assume.
    std::sort(indices.begin(), indices.end(), [&node, defender_seat](std::size_t a, std::size_t b)
    {
        if (node.p[a] != node.p[b])
        {
            return node.p[a] > node.p[b];
        }
        return layout_key(node.layouts[a], defender_seat) < layout_key(node.layouts[b], defender_seat);
    });
    indices.resize(static_cast<std::size_t>(keep));

    BeliefNode pruned{};
    pruned.state = node.state;
    pruned.kappa = node.kappa;
    pruned.is_sample = node.is_sample;
    pruned.no_more_available = node.no_more_available;
    pruned.layouts.reserve(indices.size());
    pruned.p.reserve(indices.size());
    pruned.root_keys.reserve(indices.size());
    for (std::size_t idx : indices)
    {
        pruned.layouts.push_back(node.layouts[idx]);
        pruned.p.push_back(node.p[idx]);
        pruned.root_keys.push_back(node.root_keys[idx]);
    }
    return pruned;
}

auto check_leaf(
    BeliefNode const& node, int depth, BruteForceOptions const& options, LayoutBound const& bound,
    DeclarerObjective objective) -> LeafCheckResult
{
    // Check for a terminal before pruning. A terminal leaf calls no solver,
    // so pruning would only lose mass: dropped mass is not redistributed. The
    // first node search() sees often has more layouts than max_layouts.
    if (is_terminal(node))
    {
        return LeafCheckResult{terminal_leaf_value(node, objective), node};
    }

    // Prune before the depth cutoff, and before the cache key reads layouts.
    // cutoff_leaf_value calls DoubleDummyBound once per layout. With a small
    // max_depth, the cutoff can come before any ancestor pruned. Pruning a
    // node already within the cap returns it unchanged.
    BeliefNode const pruned =
        options.max_layouts.has_value() ? drop_lowest_posterior_layouts(node, *options.max_layouts) : node;

    if (options.max_depth.has_value() && depth >= *options.max_depth)
    {
        return LeafCheckResult{cutoff_leaf_value(pruned, bound, objective), pruned};
    }

    return LeafCheckResult{std::nullopt, pruned};
}

BruteForceDeclarer::BruteForceDeclarer(
    SolverContext& ctx, DeclarerObjective objective, DefenderStrategy opponent_model,
    BruteForceOptions options)
: ctx_(ctx), objective_(objective), options_(options)
{
    // max_layouts = 0 would leave every pruned node empty, and
    // make_brute_force_cache_key would read layouts.front() of an empty
    // vector. Rejected, as make_root() rejects sample_size == 0.
    if (options_.max_layouts.has_value() && *options_.max_layouts == 0)
    {
        throw std::invalid_argument("BruteForceOptions::max_layouts must not be 0");
    }
    // A negative max_depth would make every node an immediate cutoff. Report
    // it instead.
    if (options_.max_depth.has_value() && *options_.max_depth < 0)
    {
        throw std::invalid_argument("BruteForceOptions::max_depth must not be negative");
    }
    if (opponent_model)
    {
        opponent_model_ = std::move(opponent_model);
    }
    else
    {
        default_opponent_.emplace(ctx_);
        opponent_model_ = default_opponent_->as_strategy();
    }
}

auto BruteForceDeclarer::bound_for(ObservationState const& state) -> DoubleDummyBound&
{
    if (! bound_.has_value())
    {
        bound_.emplace(ctx_, state.declarer);
        bound_declarer_ = state.declarer;
    }
    // A runtime check, not assert(): --config=opt compiles asserts out. A
    // second declarer seat would otherwise silently use the first one's
    // DoubleDummyBound.
    if (state.declarer != bound_declarer_)
    {
        throw std::logic_error(
            "BruteForceDeclarer reused for a second declarer seat ("
            + std::to_string(state.declarer) + ") after being bound to the first ("
            + std::to_string(bound_declarer_)
            + ") -- one instance is for a single declarer seat for its whole lifetime");
    }
    return *bound_;
}

auto BruteForceDeclarer::is_declarer_side(ObservationState const& state, int seat) -> bool
{
    int const dummy = (state.declarer + 2) % DDS_HANDS;
    return seat == state.declarer || seat == dummy;
}

auto BruteForceDeclarer::search(BeliefNode const& node, int depth) -> double
{
    // check_leaf holds the leaf decision and its order, so tests can call it.
    LeafCheckResult const leaf =
        check_leaf(node, depth, options_, bound_for(node.state).as_bound(), objective_);
    if (leaf.value.has_value())
    {
        return *leaf.value;
    }
    BeliefNode const& n = leaf.continuation;

    BruteForceCacheKey const key = make_brute_force_cache_key(n.state, n.layouts, n.p);
    if (std::optional<double> const cached = cache_.find(key); cached.has_value())
    {
        return *cached;
    }

    int const seat = seat_on_play(n.state.known_holdings);
    double value = 0.0;

    if (is_declarer_side(n.state, seat))
    {
        std::vector<Card> const cards = enumerate_legal_cards(n.state.known_holdings, seat);
        std::vector<BeliefNode> const children = make_declarer_children(n, cards);
        double best = -std::numeric_limits<double>::infinity();
        for (BeliefNode const& child : children)
        {
            best = std::max(best, search(child, depth + 1));
        }
        value = best;
    }
    else
    {
        ExpandDefenderResult const result = expand_defender_node(n, opponent_model_);
        if (! result.children.has_value())
        {
            throw BruteForceOpponentModelError{result.error, seat, result.offending_layout};
        }
        KahanAccumulator total;
        for (BeliefNode const& child : *result.children)
        {
            total.add(search(child, depth + 1));
        }
        value = total.value();
    }

    cache_.insert(key, value);
    return value;
}

auto BruteForceDeclarer::as_strategy() -> DeclarerStrategy
{
    DeclarerStrategy strategy;
    strategy.id = 0;
    strategy.play = [this](ObservationState const& state, BeliefView const& view) -> Card
    {
        // With max_depth set, the cutoff depends on the depth where this call
        // reached a node, not on the node. A later call can reach the same node
        // at another depth and need a different answer. So each call starts
        // with an empty cache. Without max_depth, the cache is kept across
        // calls. See the BruteForceDeclarer doxygen.
        if (options_.max_depth.has_value())
        {
            cache_ = BruteForceCache{};
        }
        BeliefNode const root = make_internal_root(state, view);
        // Prune the root here. search() never sees the root, and
        // make_declarer_children would otherwise copy every layout once per legal
        // card. The root cannot be a terminal leaf, because play() always
        // branches. Declarer's card changes no posterior and no tie-break key, so
        // pruning once here keeps the same survivors as pruning each child.
        BeliefNode const pruned_root =
            options_.max_layouts.has_value() ? drop_lowest_posterior_layouts(root, *options_.max_layouts) : root;
        int const seat = seat_on_play(pruned_root.state.known_holdings);
        std::vector<Card> const cards = enumerate_legal_cards(pruned_root.state.known_holdings, seat);
        std::vector<BeliefNode> const children = make_declarer_children(pruned_root, cards);

        double best_value = -std::numeric_limits<double>::infinity();
        Card best_card = cards.front();
        for (std::size_t i = 0; i < children.size(); ++i)
        {
            double const value = search(children[i], /*depth=*/1);
            if (value > best_value)
            {
                best_value = value;
                best_card = cards[i];
            }
        }
        return best_card;
    };
    strategy.state_key = [](ObservationState const& state, BeliefView const& view) -> StateKey
    {
        BeliefNode const root = make_internal_root(state, view);
        return serialize_key(make_brute_force_cache_key(root.state, root.layouts, root.p));
    };
    return strategy;
}

}  // namespace dds::belief_evaluation
