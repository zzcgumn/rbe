#include <belief_evaluation/brute_force_cache.hpp>

#include <algorithm>
#include <bit>

#include <api/dds_constants.hpp>

#include <belief_evaluation/layout_key.hpp>

namespace dds::belief_evaluation
{

namespace
{
    // A splitmix64-style mixing step, for fields too wide to pack with shifts.
    auto hash_combine(std::uint64_t seed, std::uint64_t value) -> std::uint64_t
    {
        seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        return seed;
    }

    // The exact bits of posterior, which matches operator==. -0.0 becomes
    // +0.0 first: `==` treats them as equal, so their hashes must match.
    // Validated posteriors are positive, so this should never apply.
    auto hash_posterior(Probability posterior) -> std::uint64_t
    {
        if (posterior == 0.0)
        {
            posterior = 0.0;
        }
        return std::bit_cast<std::uint64_t>(posterior);
    }
}

auto BruteForceCacheKey::operator==(BruteForceCacheKey const& other) const -> bool
{
    return trump == other.trump && declarer == other.declarer
        && tricks_needed == other.tricks_needed
        && tricks_won_by_declarer == other.tricks_won_by_declarer && first == other.first
        && root_first == other.root_first
        && std::equal(
               std::begin(current_trick_suit), std::end(current_trick_suit),
               std::begin(other.current_trick_suit))
        && std::equal(
               std::begin(current_trick_rank), std::end(current_trick_rank),
               std::begin(other.current_trick_rank))
        && declarer_holding_key == other.declarer_holding_key
        && dummy_holding_key == other.dummy_holding_key && deal_and_posterior == other.deal_and_posterior;
}

auto hash_value(BruteForceCacheKey const& key) -> std::size_t
{
    std::uint64_t h = 0;
    h = hash_combine(h, static_cast<std::uint64_t>(key.trump));
    h = hash_combine(h, static_cast<std::uint64_t>(key.declarer));
    h = hash_combine(h, static_cast<std::uint64_t>(key.tricks_needed));
    h = hash_combine(h, static_cast<std::uint64_t>(key.tricks_won_by_declarer));
    h = hash_combine(h, static_cast<std::uint64_t>(key.first));
    h = hash_combine(h, static_cast<std::uint64_t>(key.root_first));
    for (int slot = 0; slot < 3; ++slot)
    {
        h = hash_combine(h, static_cast<std::uint64_t>(key.current_trick_suit[slot]));
        h = hash_combine(h, static_cast<std::uint64_t>(key.current_trick_rank[slot]));
    }
    h = hash_combine(h, key.declarer_holding_key);
    h = hash_combine(h, key.dummy_holding_key);
    for (auto const& [defender_key, other_defender_key, posterior] : key.deal_and_posterior)
    {
        h = hash_combine(h, defender_key);
        h = hash_combine(h, other_defender_key);
        h = hash_combine(h, hash_posterior(posterior));
    }
    return static_cast<std::size_t>(h);
}

auto make_brute_force_cache_key(
    ObservationState const& state, std::vector<Deal> const& layouts,
    std::vector<Probability> const& p) -> BruteForceCacheKey
{
    Deal const& representative = layouts.front();  // all layouts share everything but the defenders' split
    int const dummy = (state.declarer + 2) % DDS_HANDS;

    BruteForceCacheKey key;
    key.trump = state.trump;
    key.declarer = state.declarer;
    key.tricks_needed = state.tricks_needed;
    key.tricks_won_by_declarer = state.tricks_won_by_declarer;
    key.first = representative.first;
    key.root_first = state.first;
    for (int slot = 0; slot < 3; ++slot)
    {
        key.current_trick_suit[slot] = representative.currentTrickSuit[slot];
        key.current_trick_rank[slot] = representative.currentTrickRank[slot];
    }
    // Declarer's and dummy's exact holdings. Required: see
    // BruteForceCacheKey.
    key.declarer_holding_key = layout_key(representative, state.declarer);
    key.dummy_holding_key = layout_key(representative, dummy);

    // A fixed defender seat. Both defenders' holdings are packed per layout:
    // one holding fixes the other only within one pool (see
    // BruteForceCacheKey::deal_and_posterior).
    int const defender_seat = (state.declarer + 1) % DDS_HANDS;
    int const other_defender_seat = (state.declarer + 3) % DDS_HANDS;

    std::vector<std::tuple<std::uint64_t, std::uint64_t, Probability>> entries;
    entries.reserve(layouts.size());
    for (std::size_t i = 0; i < layouts.size(); ++i)
    {
        entries.emplace_back(
            layout_key(layouts[i], defender_seat), layout_key(layouts[i], other_defender_seat), p[i]);
    }
    std::sort(entries.begin(), entries.end());
    key.deal_and_posterior = std::move(entries);

    return key;
}

auto BruteForceCache::find(BruteForceCacheKey const& key) const -> std::optional<double>
{
    auto const it = table_.find(key);
    if (it == table_.end())
    {
        return std::nullopt;
    }
    return it->second;
}

auto BruteForceCache::insert(BruteForceCacheKey const& key, double value) -> void
{
    table_[key] = value;
}

auto BruteForceCache::size() const -> std::size_t
{
    return table_.size();
}

}  // namespace dds::belief_evaluation
