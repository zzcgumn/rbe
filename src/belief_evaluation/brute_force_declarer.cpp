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
    // The root construction convention this strategy's internal search
    // relies on throughout: kappa = 1.0, p = each entry's own posterior
    // (already summing to 1, per BeliefView's own contract), so
    // node_mass(root) == 1.0 exactly and every value the search computes
    // or combines -- leaf, declarer-max, defender-sum -- is already the
    // correctly normalised probability or expected trick count, with no
    // separate normalisation step anywhere in the recursion. root_keys is
    // filled with each layout's own index -- this search never reads it
    // back (its real meaning, "identity in root space", does not apply to
    // a private internal recursion with no outer root to compare
    // against), but expand_defender_node asserts it is present at the
    // right length before doing anything else.
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

    // A stable serialisation of BruteForceCacheKey's own belief-space
    // component *only* into a StateKey (std::string) -- deliberately
    // omitting the position fields. DeclarerStrategy::state_key's own three-state
    // contract (declarer_strategy.hpp) asks for what play() consults
    // *beyond* the position the evaluator already keys on, nothing more;
    // the evaluator would supply the position component of any future
    // cache key itself, so including it here would make this strategy's
    // own declaration needlessly coarser than necessary -- two states
    // identical in "the rest" but differing only in a position field the
    // evaluator already keys on separately would wrongly miss each other
    // once an evaluator-level cache exists. Inert today (evaluate() never
    // calls state_key at all), but worth getting right now rather than
    // when it first matters. Two equal keys must serialise to equal
    // strings, and nothing else is required -- there is no existing format
    // to match, only internal consistency to keep.
    auto serialize_key(BruteForceCacheKey const& key) -> StateKey
    {
        std::string bytes;
        bytes.reserve(key.deal_and_posterior.size() * 16);
        auto const append = [&bytes](auto const& value)
        {
            bytes.append(reinterpret_cast<char const*>(&value), sizeof(value));
        };
        for (auto const& [deal_hash, posterior] : key.deal_and_posterior)
        {
            append(deal_hash);
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

    // One defender's seat fixed for this node's whole lifetime (the same
    // declarer throughout), used below purely as a stable tie-break
    // identity -- not the position-identity role layout_key() otherwise
    // plays in make_brute_force_cache_key.
    int const defender_seat = (node.state.declarer + 1) % DDS_HANDS;

    std::vector<std::size_t> indices(node.layouts.size());
    for (std::size_t i = 0; i < indices.size(); ++i)
    {
        indices[i] = i;
    }
    // Descending by posterior, ties broken by each layout's own exact
    // identity (layout_key), not by input index -- a total order over the
    // *content*, so the kept set is deterministic regardless of the
    // input's own enumeration order. Breaking ties by index instead would
    // let two belief spaces that are pure permutations of each other (the
    // same set of (deal, posterior) pairs, differently ordered -- which
    // state_key and make_brute_force_cache_key's own sorted key already
    // treat as identical) retain a different survivor under a tie, and
    // therefore potentially choose a different card, solely from source
    // order.
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
    // is_terminal/terminal_leaf_value checked on the *original* node,
    // before any pruning: there is no solver call here to bound -- a
    // terminal leaf is already O(1), reading only common knowledge
    // (tricks_won_by_declarer) plus node_mass over whatever node.p it is
    // handed -- so pruning first would buy nothing and cost real
    // accuracy: drop_lowest_posterior_layouts() never redistributes the
    // dropped layouts' own mass onto the survivors, so a terminal node
    // reached with more surviving layouts than max_layouts (the common
    // case for the very first node search() is ever called on, since
    // play() builds its root and its declarer children directly from the
    // full, unpruned BeliefView the caller handed in) would otherwise
    // silently under-count the true probability/expected-tricks value for
    // no reason connected to what max_layouts actually exists to bound.
    if (is_terminal(node))
    {
        return LeafCheckResult{terminal_leaf_value(node, objective), node};
    }

    // A node whose own surviving layout count exceeds max_layouts is
    // pruned down before anything below this point reads node.layouts/p
    // -- the depth-cutoff check right after, and (back in search()) the
    // cache key further down. Pruning before the depth-cutoff check in
    // particular matters: cutoff_leaf_value calls DoubleDummyBound once
    // per surviving layout, a real solver call, so a cutoff reached before
    // any ancestor node had a chance to prune (max_depth of 0 or 1, say)
    // must still see the capped layout set, not the original, possibly
    // much larger one -- otherwise max_layouts silently stops bounding
    // exactly the cost it exists to bound. Unlike the terminal case above,
    // this one does have a real cost to bound, which is exactly why it is
    // pruned first and the terminal case is not. drop_lowest_posterior_layouts()
    // returns its argument unchanged when nothing needs dropping, so this
    // is safe to call unconditionally once max_layouts is set at all.
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
    // max_layouts = 0 means "keep the 0 highest-posterior layouts" --
    // every non-terminal node search() ever prunes would collapse to zero
    // layouts, and make_brute_force_cache_key's own layouts.front() would
    // then read past an empty vector. Rejected here for the same reason
    // make_root() rejects sample_size == 0 (node.cpp): there is no useful
    // non-UB meaning for "cap the search to nothing", so this is a caller
    // mistake to report, not a degenerate case to special-case through.
    if (options_.max_layouts.has_value() && *options_.max_layouts == 0)
    {
        throw std::invalid_argument("BruteForceOptions::max_layouts must not be 0");
    }
    // max_depth is a ply count counted from 1 (the first searched child,
    // per as_strategy()'s own play lambda) -- a negative value is not a
    // smaller-but-valid budget, it is check_leaf's own
    // `depth >= *max_depth` test firing on the very first node for every
    // negative value alike, silently behaving as an immediate cutoff
    // rather than reporting the caller's mistake.
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
    // A real runtime check, not assert(): this guards a caller mistake --
    // reusing one BruteForceDeclarer instance for a second declarer seat,
    // which this class's own doxygen already documents as unsupported --
    // and assert() compiles out entirely under NDEBUG (this repo's own
    // --config=opt), which would silently keep the first declarer's
    // DoubleDummyBound and compute wrong cutoff values for the second
    // rather than failing at all.
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
    // The leaf decision (terminal vs. depth-cutoff vs. neither, and in
    // which order max_layouts pruning applies) is factored into check_leaf
    // itself -- called here, not reimplemented -- so a test can exercise
    // this exact production ordering directly. See check_leaf's own
    // doxygen for the reasoning.
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
        // The cutoff a finite max_depth triggers inside search() depends
        // on *call-relative* recursion depth, not on anything inherent to
        // a node's own (state, layouts, p) -- the same logical node can be
        // reached at a different depth by a later call whose own root
        // happens to be closer to it (a real possibility: this is exactly
        // the "successive pi calls along one actually-played line revisit
        // the same node" reuse this strategy's cache otherwise exists
        // for), with a genuinely different correct cutoff-or-not answer
        // each time. Carrying cache_ across calls is therefore only sound
        // when max_depth is absent, where a node's value depends purely on
        // its own content and never on how this call's own root was
        // reached. With max_depth set, each call starts from a clean
        // cache instead -- correct at the cost of the cross-call reuse the
        // plan's own "Correctness" section otherwise relies on; see this
        // class's own doxygen.
        if (options_.max_depth.has_value())
        {
            cache_ = BruteForceCache{};
        }
        BeliefNode const root = make_internal_root(state, view);
        int const seat = seat_on_play(root.state.known_holdings);
        std::vector<Card> const cards = enumerate_legal_cards(root.state.known_holdings, seat);
        std::vector<BeliefNode> const children = make_declarer_children(root, cards);

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
