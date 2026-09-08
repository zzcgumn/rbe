#include <belief_evaluation/consistent_layout_source.hpp>

#include <cassert>

#include <belief_evaluation/keyed_permutation.hpp>

namespace dds::belief_evaluation
{

ConsistentLayoutSource::ConsistentLayoutSource(Deal const& root, int declarer, std::uint64_t seed)
    : root_(root), declarer_(declarer), seed_(seed), pool_(defender_pool_decomposition(root, declarer))
{
}

auto ConsistentLayoutSource::size() const -> std::optional<std::uint64_t>
{
    return binomial_coefficient(static_cast<int>(pool_.cards.size()), pool_.fixed_seat_count);
}

auto ConsistentLayoutSource::at(std::uint64_t index) const -> Deal
{
    std::uint64_t const total = *size();
    assert(index < total);

    // Permute first, on the index, before unranking: permuting after
    // unranking would mean permuting a subset (not a thing), and
    // permuting the output space rather than the index space would
    // require materialising it -- this ordering is what keeps the whole
    // construction O(1) memory. See ConsistentLayoutSource's own doxygen.
    std::uint64_t const permuted_index = keyed_permutation(index, total, seed_);
    std::vector<int> const subset =
        unrank_combination(permuted_index, static_cast<int>(pool_.cards.size()), pool_.fixed_seat_count);
    return apply_defender_split(root_, declarer_, pool_, subset);
}

}  // namespace dds::belief_evaluation
