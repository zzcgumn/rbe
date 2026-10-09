#include <belief_evaluation/brute_force_cache.hpp>

#include <algorithm>
#include <cmath>

#include <api/dds_constants.hpp>

#include <belief_evaluation/layout_key.hpp>

namespace dds::belief_evaluation
{

namespace
{
    // splitmix64-style mixing step -- cheap, well-distributed, and this
    // module's own precedent for packing several small integer fields into
    // one 64-bit key is layout_key()'s own bit-packing; this does the same
    // job for fields too wide to just shift-and-or together.
    auto hash_combine(std::uint64_t seed, std::uint64_t value) -> std::uint64_t
    {
        seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        return seed;
    }
}

auto BruteForceCacheKey::operator==(BruteForceCacheKey const& other) const -> bool
{
    return position_hash == other.position_hash
        && deal_and_quantized_posterior == other.deal_and_quantized_posterior;
}

auto hash_value(BruteForceCacheKey const& key) -> std::size_t
{
    std::uint64_t h = key.position_hash;
    for (auto const& [deal_hash, quantized] : key.deal_and_quantized_posterior)
    {
        h = hash_combine(h, deal_hash);
        h = hash_combine(h, static_cast<std::uint64_t>(quantized));
    }
    return static_cast<std::size_t>(h);
}

auto quantize_posterior(Probability posterior) -> std::int64_t
{
    constexpr double Scale = 1e9;
    return static_cast<std::int64_t>(std::llround(posterior * Scale));
}

auto make_brute_force_cache_key(
    ObservationState const& state, std::vector<Deal> const& layouts,
    std::vector<Probability> const& p) -> BruteForceCacheKey
{
    Deal const& representative = layouts.front();  // every layout shares trump/first/current-trick
                                                     // shape and declarer's/dummy's own holdings --
                                                     // the module's own outstanding-pool invariant
    int const dummy = (state.declarer + 2) % DDS_HANDS;

    std::uint64_t position_hash = 0;
    position_hash = hash_combine(position_hash, static_cast<std::uint64_t>(state.trump));
    position_hash = hash_combine(position_hash, static_cast<std::uint64_t>(state.declarer));
    position_hash = hash_combine(position_hash, static_cast<std::uint64_t>(state.tricks_needed));
    position_hash =
        hash_combine(position_hash, static_cast<std::uint64_t>(state.tricks_won_by_declarer));
    position_hash = hash_combine(position_hash, static_cast<std::uint64_t>(representative.first));
    for (int slot = 0; slot < 3; ++slot)
    {
        position_hash =
            hash_combine(position_hash, static_cast<std::uint64_t>(representative.currentTrickSuit[slot]));
        position_hash =
            hash_combine(position_hash, static_cast<std::uint64_t>(representative.currentTrickRank[slot]));
    }
    // Declarer's and dummy's own exact holdings -- not redundant with the
    // per-layout entries below, which hash only a defender's split. See
    // BruteForceCacheKey's own doxygen for why this field is required.
    position_hash = hash_combine(position_hash, layout_key(representative, state.declarer));
    position_hash = hash_combine(position_hash, layout_key(representative, dummy));

    // One defender's seat fixed for this strategy's whole lifetime (the
    // same declarer, hence the same defender pair, throughout) -- its
    // holding fully determines the other defender's by complement, given
    // the outstanding pool position_hash above already captures via
    // declarer's/dummy's holdings. See layout_key()'s own doxygen.
    int const defender_seat = (state.declarer + 1) % DDS_HANDS;

    std::vector<std::pair<std::uint64_t, std::int64_t>> pairs;
    pairs.reserve(layouts.size());
    for (std::size_t i = 0; i < layouts.size(); ++i)
    {
        pairs.emplace_back(layout_key(layouts[i], defender_seat), quantize_posterior(p[i]));
    }
    std::sort(pairs.begin(), pairs.end());

    return BruteForceCacheKey{position_hash, std::move(pairs)};
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
