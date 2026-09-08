#include <belief_evaluation/defender_split.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>

#include <utility/constants.h>

namespace dds::belief_evaluation
{

auto defender_pool_decomposition(Deal const& root, int declarer) -> DefenderPool
{
    // declarer and dummy are never read: the pool is exactly the two
    // defenders' union, and neither holding contributes to it whatever it
    // is.
    int const fixed_seat = (declarer + 1) % DDS_HANDS;
    int const other_seat = (declarer + 3) % DDS_HANDS;

    DefenderPool pool;
    // std::popcount, not half of the eventual cards.size(): mid-trick the
    // two defenders' counts legitimately differ by one, and this must read
    // the fixed seat's own remaining holding rather than assume symmetry.
    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        pool.fixed_seat_count += std::popcount(root.remainCards[fixed_seat][suit]);
    }

    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        unsigned const suit_pool = root.remainCards[fixed_seat][suit] | root.remainCards[other_seat][suit];
        // Ranks ascending, bit r for absolute rank r -- Deal::remainCards'
        // own convention, no >> 2 shift (that compacted convention belongs
        // to aggr and the lookup tables, not here; see rank_map.cpp's own
        // comment on the distinction).
        for (int rank = 2; rank <= 14; ++rank)
        {
            if ((suit_pool & (1u << rank)) != 0)
            {
                pool.cards.push_back(Card{suit, rank});
            }
        }
    }
    return pool;
}

namespace
{
    // Thirteen tricks, so at most 26 cards are ever outstanding between two
    // defenders -- see binomial_coefficient's own doxygen.
    constexpr int MaxOutstandingCards = 26;

    constexpr auto make_pascals_triangle()
    {
        std::array<std::array<std::uint64_t, MaxOutstandingCards + 1>, MaxOutstandingCards + 1> table{};
        for (int n = 0; n <= MaxOutstandingCards; ++n)
        {
            table[n][0] = 1;
            for (int k = 1; k <= n; ++k)
            {
                // table[n - 1][k] is 0 by the zero-initialisation above
                // when k == n (row n - 1 only ever had entries up to
                // column n - 1 written to it) -- exactly C(n - 1, n) == 0,
                // so no separate bounds check is needed here.
                table[n][k] = table[n - 1][k - 1] + table[n - 1][k];
            }
        }
        return table;
    }

    constexpr auto PascalsTriangle = make_pascals_triangle();
}  // namespace

auto binomial_coefficient(int n, int k) -> std::uint64_t
{
    // n outside [0, MaxOutstandingCards] is a caller error -- the domain
    // this function is documented for -- and, unlike k outside [0, n]
    // (a legitimate, well-defined "zero" case), indexing PascalsTriangle
    // with it would read straight past the end of the table. Asserted for
    // a build where that is caught loudly; the range check below is what
    // stops it becoming an out-of-bounds read in a build where it is not.
    assert(n >= 0 && n <= MaxOutstandingCards);
    if (n < 0 || n > MaxOutstandingCards || k < 0 || k > n)
    {
        return 0;
    }
    return PascalsTriangle[static_cast<std::size_t>(n)][static_cast<std::size_t>(k)];
}

auto unrank_combination(std::uint64_t index, int n, int k) -> std::vector<int>
{
    // Combinatorial-number-system unranking in colex order: for each
    // position from k down to 1, find the largest remaining candidate v
    // with C(v, i) <= index, take it, and subtract that many combinations
    // before moving on. Builds descending, reversed to ascending below.
    std::vector<int> result;
    result.reserve(static_cast<std::size_t>(k));
    int v = n - 1;
    for (int i = k; i >= 1; --i)
    {
        while (binomial_coefficient(v, i) > index)
        {
            --v;
        }
        result.push_back(v);
        index -= binomial_coefficient(v, i);
        --v;
    }
    std::reverse(result.begin(), result.end());
    return result;
}

auto apply_defender_split(
    Deal const& root, int declarer, DefenderPool const& pool, std::vector<int> const& fixed_seat_cards) -> Deal
{
    int const fixed_seat = (declarer + 1) % DDS_HANDS;
    int const other_seat = (declarer + 3) % DDS_HANDS;

    Deal result = root;  // copy-and-overwrite: everything but the two defenders' holdings is root's own
    for (int suit = 0; suit < DDS_SUITS; ++suit)
    {
        result.remainCards[fixed_seat][suit] = 0;
        result.remainCards[other_seat][suit] = 0;
    }

    // The complement is derived from pool itself (every card not named
    // goes to other_seat), never from root's own other-defender holding --
    // see this function's own doxygen for why. A fixed-size array, not
    // std::vector<bool>: this is on UnconstrainedLayoutSource::at()'s hot
    // path, the pool is bounded by MaxOutstandingCards regardless of the
    // root (thirteen tricks, so at most 26 cards are ever outstanding
    // between two defenders -- see binomial_coefficient's own doxygen),
    // and a per-call heap allocation for a bound this small is waste a
    // large enumeration pays for on every single at() call.
    assert(pool.cards.size() <= MaxOutstandingCards);
    std::array<bool, MaxOutstandingCards> is_fixed_seat_card{};
    for (int index : fixed_seat_cards)
    {
        // A bad index here is a caller error (this function trusts
        // fixed_seat_cards for legality -- see its own doxygen), asserted
        // for a build where that catches it loudly; the range check below
        // is what stops it becoming an out-of-bounds write into
        // is_fixed_seat_card in a build where the assert has been compiled
        // away, rather than silently corrupting memory the size check
        // above alone does not guard against.
        assert(index >= 0 && static_cast<std::size_t>(index) < pool.cards.size());
        if (index < 0 || static_cast<std::size_t>(index) >= pool.cards.size())
        {
            continue;
        }
        is_fixed_seat_card[static_cast<std::size_t>(index)] = true;
    }
    for (std::size_t i = 0; i < pool.cards.size(); ++i)
    {
        Card const& card = pool.cards[i];
        int const seat = is_fixed_seat_card[i] ? fixed_seat : other_seat;
        result.remainCards[seat][card.suit] |= (1u << card.rank);
    }
    return result;
}

}  // namespace dds::belief_evaluation
