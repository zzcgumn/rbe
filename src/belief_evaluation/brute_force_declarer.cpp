#include <belief_evaluation/brute_force_declarer.hpp>

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

#include <belief_evaluation/kahan.hpp>
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

    // A stable serialisation of BruteForceCacheKey into a StateKey
    // (std::string): two equal keys must serialise to equal strings, and
    // nothing else is required -- evaluate() never calls state_key today
    // (unchanged by this strategy; see DeclarerStrategy::state_key's own
    // doxygen), so there is no existing format to match, only internal
    // consistency to keep.
    auto serialize_key(BruteForceCacheKey const& key) -> StateKey
    {
        std::string bytes;
        bytes.reserve(sizeof(key.position_hash) + key.deal_and_quantized_posterior.size() * 16);
        auto const append = [&bytes](auto const& value)
        {
            bytes.append(reinterpret_cast<char const*>(&value), sizeof(value));
        };
        append(key.position_hash);
        for (auto const& [deal_hash, quantized] : key.deal_and_quantized_posterior)
        {
            append(deal_hash);
            append(quantized);
        }
        return bytes;
    }
}

auto terminal_leaf_value(BeliefNode const& node, DeclarerObjective objective) -> double
{
    if (objective == DeclarerObjective::MaximiseExpectedTricks)
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
        double const per_layout_value = (objective == DeclarerObjective::MaximiseExpectedTricks)
            ? static_cast<double>(declarer_total_tricks)
            : (declarer_total_tricks >= node.state.tricks_needed ? 1.0 : 0.0);
        total.add(node.p[i] * per_layout_value);
    }
    return node.kappa * total.value();
}

BruteForceDeclarer::BruteForceDeclarer(
    SolverContext& ctx, DeclarerObjective objective, DefenderStrategy opponent_model,
    BruteForceOptions options)
: ctx_(ctx), objective_(objective), options_(options)
{
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
    assert(state.declarer == bound_declarer_);
    return *bound_;
}

auto BruteForceDeclarer::as_strategy() -> DeclarerStrategy
{
    DeclarerStrategy strategy;
    strategy.id = 0;
    strategy.play = [this](ObservationState const& state, BeliefView const& view) -> Card
    {
        BeliefNode const root = make_internal_root(state, view);
        // TODO: search the belief space and return the best card under
        // objective_, rather than the first legal one -- a later task.
        (void)objective_;
        (void)opponent_model_;
        return enumerate_legal_cards(root.state.known_holdings, seat_on_play(root.state.known_holdings))
            .front();
    };
    strategy.state_key = [](ObservationState const& state, BeliefView const& view) -> StateKey
    {
        BeliefNode const root = make_internal_root(state, view);
        return serialize_key(make_brute_force_cache_key(root.state, root.layouts, root.p));
    };
    return strategy;
}

}  // namespace dds::belief_evaluation
