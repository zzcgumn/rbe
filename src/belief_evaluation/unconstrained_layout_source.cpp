#include <belief_evaluation/unconstrained_layout_source.hpp>

#include <cassert>
#include <cstddef>
#include <vector>

#include <belief_evaluation/keyed_permutation.hpp>
#include <belief_evaluation/void_derivation.hpp>

namespace dds::belief_evaluation
{

namespace
{
    /// Builds the `Deal` for one layout: `root` with the fixed and other
    /// seats' `remainCards` replaced by exactly `fixed_seat_cards` and
    /// `other_seat_cards`. The constrained-decomposition sibling of
    /// `apply_defender_split` (defender_split.hpp) -- that one takes
    /// indices into a `DefenderPool`'s own whole-pool order, which is
    /// exactly right for the unconstrained case's `unrank_combination`
    /// result but does not fit here: a constrained `at()` already knows the
    /// forced cards outright and only unranks a subset of the *free* cards,
    /// so this takes the resulting card lists directly rather than
    /// reintroducing a pool-relative index space for them. Copy-and-
    /// overwrite, exactly as `apply_defender_split` is built: trump,
    /// `first`, both `currentTrick*` arrays, and declarer's and dummy's
    /// holdings are therefore guaranteed byte-identical to `root`'s own.
    auto build_layout(
        Deal const& root,
        int fixed_seat,
        int other_seat,
        std::vector<Card> const& fixed_seat_cards,
        std::vector<Card> const& other_seat_cards) -> Deal
    {
        Deal result = root;
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            result.remainCards[fixed_seat][suit] = 0;
            result.remainCards[other_seat][suit] = 0;
        }
        for (Card const& card : fixed_seat_cards)
        {
            result.remainCards[fixed_seat][card.suit] |= (1u << card.rank);
        }
        for (Card const& card : other_seat_cards)
        {
            result.remainCards[other_seat][card.suit] |= (1u << card.rank);
        }
        return result;
    }
}

UnconstrainedLayoutSource::UnconstrainedLayoutSource(
    Deal const& root, int declarer, std::uint64_t seed, PlayTraceBin const& history, int opening_leader)
    : root_(root)
    , seed_(seed)
    , fixed_seat_((declarer + 1) % DDS_HANDS)
    , other_seat_((declarer + 3) % DDS_HANDS)
    , pool_(defender_pool_decomposition(root, declarer))
{
    // An empty history applies no constraint and is not checked against
    // root at all -- see this constructor's own doxygen for why: a caller
    // supplying no play record is not making a claim verify_history could
    // reject, and every call site written before this parameter existed
    // must keep compiling into exactly this branch.
    verdict_ =
        history.number == 0 ? HistoryVerdict::Consistent : verify_history(root, declarer, history, opening_leader);

    if (verdict_ == HistoryVerdict::Consistent)
    {
        // derive_voids on an empty history returns no voids for anybody, so
        // this branch also covers the no-history case: decompose_constrained
        // then forces nothing, free_cards is the whole pool in its own
        // order, and fixed_seat_needed is pool_.fixed_seat_count -- the same
        // values the original, pre-history implementation used directly,
        // reached by the same call rather than a parallel one.
        VoidsBySeat const voids = derive_voids(history, opening_leader, root.trump);
        decomposition_ = decompose_constrained(pool_, voids[fixed_seat_], voids[other_seat_]);
    }
    // else: decomposition_ stays default-constructed (status Ok, every list
    // empty). That status is not meaningful here -- see
    // constrained_space_status()'s own doxygen -- a caller must consult
    // verdict_ (via history_verdict()) first.
}

auto UnconstrainedLayoutSource::size() const -> std::optional<std::uint64_t>
{
    if (verdict_ != HistoryVerdict::Consistent)
    {
        // Not constrained_space_size(decomposition_): decomposition_ is the
        // meaningless default in this branch (status Ok, needed 0, free
        // empty), which would report C(0, 0) == 1 -- an empty history is
        // never this branch (see the constructor), so this can only be a
        // genuine rejection, and the space it names is empty, full stop.
        return std::uint64_t{0};
    }
    return constrained_space_size(decomposition_);
}

auto UnconstrainedLayoutSource::at(std::uint64_t index) const -> Deal
{
    std::uint64_t const total = *size();
    assert(index < total);

    // Permute first, on the index, before unranking: permuting after
    // unranking would mean permuting a subset (not a thing), and
    // permuting the output space rather than the index space would
    // require materialising it -- this ordering is what keeps the whole
    // construction O(1) memory. See UnconstrainedLayoutSource's own doxygen.
    std::uint64_t const permuted_index = keyed_permutation(index, total, seed_);
    std::vector<int> const free_subset = unrank_combination(
        permuted_index, static_cast<int>(decomposition_.free_cards.size()), decomposition_.fixed_seat_needed);

    std::vector<bool> is_chosen(decomposition_.free_cards.size(), false);
    for (int i : free_subset)
    {
        is_chosen[static_cast<std::size_t>(i)] = true;
    }

    std::vector<Card> fixed_seat_cards = decomposition_.forced_to_fixed_seat;
    std::vector<Card> other_seat_cards = decomposition_.forced_to_other_seat;
    for (std::size_t i = 0; i < decomposition_.free_cards.size(); ++i)
    {
        if (is_chosen[i])
        {
            fixed_seat_cards.push_back(decomposition_.free_cards[i]);
        }
        else
        {
            other_seat_cards.push_back(decomposition_.free_cards[i]);
        }
    }

    return build_layout(root_, fixed_seat_, other_seat_, fixed_seat_cards, other_seat_cards);
}

auto UnconstrainedLayoutSource::history_verdict() const -> HistoryVerdict
{
    return verdict_;
}

auto UnconstrainedLayoutSource::constrained_space_status() const -> ConstrainedSpaceStatus
{
    return decomposition_.status;
}

}  // namespace dds::belief_evaluation
