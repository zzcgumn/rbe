#include "fixtures.hpp"

#include <algorithm>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

namespace dds::belief_evaluation::benchmarks
{

namespace
{
    constexpr int Spades = 0;
    constexpr int Hearts = 1;
    constexpr int Diamonds = 2;
    constexpr int Clubs = 3;

    constexpr int North = 0;  // declarer throughout this file
    constexpr int East = 1;   // the fixed seat: (declarer + 1) % DDS_HANDS
    constexpr int South = 2;  // dummy
    constexpr int West = 3;

    using CardPair = std::pair<int, int>;  // (suit, rank)

    /// `C(n, k)`, a small local copy rather than a call into
    /// defender_split.hpp's own binomial_coefficient: that one is private
    /// to the core library (not exported -- see its own doxygen), and
    /// duplicating it at the small arguments this file ever calls it with
    /// is the established precedent elsewhere in this codebase (see
    /// evaluate.hpp's history on binomial_coefficient duplication) rather
    /// than exporting a function whose whole point was to stay private.
    auto choose(int n, int k) -> std::uint64_t
    {
        if (k < 0 || k > n)
        {
            return 0;
        }
        std::uint64_t result = 1;
        for (int i = 0; i < k; ++i)
        {
            result = result * static_cast<std::uint64_t>(n - i) / static_cast<std::uint64_t>(i + 1);
        }
        return result;
    }

    /// The first `count` cards of Clubs (ascending) then Spades (ascending)
    /// -- the free-suit pool a rung's fixed seat and the other defender
    /// split between them once the voided suit (Diamonds, below) is set
    /// aside. Two suits rather than one so `count` can exceed 13 (needed
    /// at the top of the ladder, k=8: 2k-2=14) without running out.
    auto free_pool_cards(int count) -> std::vector<CardPair>
    {
        std::vector<CardPair> cards;
        for (int const suit : {Clubs, Spades})
        {
            for (int rank = 2; rank <= 14 && static_cast<int>(cards.size()) < count; ++rank)
            {
                cards.emplace_back(suit, rank);
            }
        }
        return cards;
    }

    /// Builds one rung: a completed trick establishes East's void in
    /// Diamonds (mirroring exhaustive_layout_source_test.cpp's own
    /// make_void_ending, scaled up), leaving exactly two Diamonds
    /// outstanding -- both forced to West by the void -- and a free pool of
    /// `2k - 2` Clubs/Spades cards, of which the fixed seat (East) needs
    /// all `k` of its own cards and West needs `k - 2` more (having
    /// already got its two forced Diamonds). Without a history supplied,
    /// the same root's unconstrained space is `C(2k, k)`; with it,
    /// `C(2k - 2, k)`.
    ///
    /// `realistic`: false gives declarer (North) every leftover card as
    /// filler -- unrealistic, but harmless and exactly
    /// exhaustive_layout_source_test.cpp's own make_ten_card_pool_root
    /// precedent, since these fixtures are about the defender pool's
    /// combinatorics, not about declarer's hand. true (make_realistic_rung_a
    /// and _b, below) alternates leftover cards between North and South
    /// instead, so both end up with a believable multi-suit spread rather
    /// than one entire suit each -- what "a position a bridge player would
    /// recognise" needs and the plain pool ladder does not.
    auto make_rung(char const* name, int k, bool realistic) -> Rung
    {
        int const c = 2 * k - 2;  // the free pool's own size
        std::vector<CardPair> const free_cards = free_pool_cards(c);
        std::vector<CardPair> const east_cards(free_cards.begin(), free_cards.begin() + k);
        std::vector<CardPair> const west_free_cards(free_cards.begin() + k, free_cards.end());

        auto const contains = [](std::vector<CardPair> const& cards, int suit, int rank) -> bool
        {
            return std::find(cards.begin(), cards.end(), CardPair{suit, rank}) != cards.end();
        };

        // Trick 1: North leads the Diamond Ace and wins it; East discards
        // (showing void in Diamonds) rather than following; South and West
        // both follow suit. Four cards, seated from North -- North, East,
        // South, West -- exactly matching what `opening_leader = North`
        // replays. After the trick, root.first is North again (the
        // winner leads next) and no trick is in progress.
        std::vector<CardPair> const played = {{Diamonds, 14}, {Hearts, 10}, {Diamonds, 13}, {Diamonds, 12}};
        std::set<CardPair> const played_set(played.begin(), played.end());

        int leftover_to_north = 0;  // only consulted when realistic is true

        auto const hand_for = [&](int suit, int rank) -> int
        {
            if (suit == Diamonds && (rank == 2 || rank == 3))
            {
                return West;  // the two Diamonds the void forces
            }
            if (suit == Hearts && (rank == 14 || rank == 13))
            {
                return North;
            }
            if (suit == Hearts && (rank == 12 || rank == 11))
            {
                return South;
            }
            if (contains(east_cards, suit, rank))
            {
                return East;
            }
            if (contains(west_free_cards, suit, rank))
            {
                return West;
            }
            // Everything else: harmless filler on declarer and dummy,
            // neither of whom this fixture's combinatorics depend on.
            if (! realistic)
            {
                return North;
            }
            leftover_to_north = ! leftover_to_north;
            return leftover_to_north != 0 ? North : South;
        };

        Deal root{};
        root.trump = DDS_NOTRUMP;
        root.first = North;
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            for (int rank = 2; rank <= 14; ++rank)
            {
                if (played_set.count({suit, rank}) != 0)
                {
                    continue;
                }
                root.remainCards[hand_for(suit, rank)][suit] |= (1u << rank);
            }
        }

        PlayTraceBin history{};
        history.number = static_cast<int>(played.size());
        for (std::size_t i = 0; i < played.size(); ++i)
        {
            history.suit[i] = played[i].first;
            history.rank[i] = played[i].second;
        }

        Rung rung{};
        rung.name = name;
        rung.without_history = RungFixture{
            .root = root,
            .declarer = North,
            .tricks_needed = 1,
            .history = PlayTraceBin{},
            .opening_leader = North,
            .expected_size = choose(2 * k, k),
        };
        rung.with_history = RungFixture{
            .root = root,
            .declarer = North,
            .tricks_needed = 1,
            .history = history,
            .opening_leader = North,
            .expected_size = choose(c, k),
        };
        return rung;
    }
}  // namespace

auto make_pool4_rung() -> Rung
{
    return make_rung("pool4", 4, /*realistic=*/false);
}

auto make_pool5_rung() -> Rung
{
    return make_rung("pool5", 5, /*realistic=*/false);
}

auto make_pool6_rung() -> Rung
{
    return make_rung("pool6", 6, /*realistic=*/false);
}

auto make_pool7_rung() -> Rung
{
    return make_rung("pool7", 7, /*realistic=*/false);
}

auto make_pool8_rung() -> Rung
{
    return make_rung("pool8", 8, /*realistic=*/false);
}

auto make_realistic_rung_a() -> Rung
{
    return make_rung("realistic_a", 6, /*realistic=*/true);
}

auto make_realistic_rung_b() -> Rung
{
    return make_rung("realistic_b", 8, /*realistic=*/true);
}

auto all_rungs() -> std::vector<Rung>
{
    return {
        make_pool4_rung(),
        make_pool5_rung(),
        make_pool6_rung(),
        make_pool7_rung(),
        make_pool8_rung(),
        make_realistic_rung_a(),
        make_realistic_rung_b(),
    };
}

// --- the finesse ladder: genuine per-layout uncertainty ------------------

auto make_finesse_rung(int suits) -> RungFixture
{
    // Close to exhaustive_layout_source_integration_test.cpp's own
    // make_two_card_finesse_root, in each of `suits` suits: North
    // (declarer) the Nine, South (dummy) the Eight, a pool split between
    // East and West. That fixture's own pool is {2..7, 10} (7 cards);
    // this one drops the Seven -- {2..6, 10}, 6 cards -- so that four
    // replicated suits (the most DDS_SUITS allows) still fit
    // binomial_coefficient's own domain: 6 suits * 4 = 24 outstanding
    // cards, under the 26-card ceiling (7 * 4 = 28 would not be). Root's
    // own East/West split within each suit is only a template for the
    // counts ExhaustiveLayoutSource reads (fixed_seat_count via popcount)
    // -- the enumerated space varies which pool cards East actually
    // holds, same as every other rung in this file.
    Deal root{};
    root.trump = DDS_NOTRUMP;
    root.first = East;  // a defender leads, as the fixture this mirrors does
    for (int suit = 0; suit < suits; ++suit)
    {
        root.remainCards[North][suit] |= (1u << 9);
        root.remainCards[South][suit] |= (1u << 8);
        root.remainCards[East][suit] |= (1u << 2);
        for (int const rank : {3, 4, 5, 6, 10})
        {
            root.remainCards[West][suit] |= (1u << rank);
        }
    }

    RungFixture fixture{};
    fixture.root = root;
    fixture.declarer = North;
    fixture.tricks_needed = suits;  // one trick per suit, all of them needed
    fixture.history = PlayTraceBin{};
    fixture.opening_leader = East;
    fixture.expected_size = choose(6 * suits, suits);
    return fixture;
}

auto all_finesse_rungs() -> std::vector<RungFixture>
{
    std::vector<RungFixture> rungs;
    for (int suits = 1; suits <= DDS_SUITS; ++suits)
    {
        rungs.push_back(make_finesse_rung(suits));
    }
    return rungs;
}

// --- the solver rung: a position solve_board() actually accepts --------

namespace
{
    /// Spades pool ranks for make_solver_rung(pool_size), chosen (not
    /// derived from a formula) so the pool always mixes ranks below and
    /// above North's own Nine -- a pool entirely below it would make
    /// North's Nine win every layout trivially, the same degenerate
    /// p_make == 1 problem the finesse ladder's own header explains.
    auto solver_rung_pool_ranks(int pool_size) -> std::vector<int>
    {
        if (pool_size == 4)
        {
            return {2, 3, 10, 11};
        }
        if (pool_size == 6)
        {
            return {2, 3, 4, 10, 11, 12};
        }
        return {};  // caller error; make_solver_rung's own two wrappers never pass anything else
    }
}  // namespace

auto make_solver_rung(int pool_size) -> RungFixture
{
    // solve_board rejects a deal whose four hands do not all hold the
    // same card count (table_deal_validate.hpp's own three rules, which
    // its own doxygen says solve_board enforces too). This function's
    // own first two drafts both tripped that, for two different reasons,
    // neither anticipated:
    //
    //   1. giving East/West `half` Spades on top of one card each in
    //      Hearts, Diamonds and Clubs, while North/South held only one
    //      Spade plus those same three suits -- unequal counts outright
    //      (solve_board's own RETURN_CARD_COUNT, confirmed directly);
    //   2. after equalising the *counts* by giving North/South extra
    //      Diamonds, ExhaustiveLayoutSource::size() came back as 252, not
    //      the intended C(pool_size, half) -- because defender_pool_decomposition
    //      flattens the pool *across every suit* East or West hold
    //      anything in, not just Spades, so "fixed" Hearts/Clubs cards on
    //      East and West were never actually fixed under enumeration; the
    //      real space mixed them in too.
    //
    // The fix for both: East and West hold *only* Spades. Every other
    // card in the ending -- what equalises the counts -- goes to North
    // and South alone, who are declarer's own side and never enter the
    // enumerated pool regardless of suit.
    std::vector<int> const pool_ranks = solver_rung_pool_ranks(pool_size);
    int const half = pool_size / 2;

    Deal root{};
    root.trump = DDS_NOTRUMP;
    root.first = East;
    root.remainCards[North][Spades] |= (1u << 9);
    root.remainCards[South][Spades] |= (1u << 8);
    for (int i = 0; i < half; ++i)
    {
        root.remainCards[East][Spades] |= (1u << pool_ranks[i]);
    }
    for (int i = half; i < pool_size; ++i)
    {
        root.remainCards[West][Spades] |= (1u << pool_ranks[i]);
    }

    // Diamonds, North and South only: `half - 1` cards each (the top
    // `2 * (half - 1)` ranks, split between them) -- the card-count
    // equaliser, entirely on declarer's own side so East/West's pool
    // stays Spades alone. North's own top holding is winners in every
    // layout (nothing else in the deck outranks it), so this suit is not
    // where the fixture's uncertainty lives -- only Spades is.
    int const extra = half - 1;
    for (int i = 0; i < extra; ++i)
    {
        root.remainCards[North][Diamonds] |= (1u << (14 - i));
    }
    for (int i = 0; i < extra; ++i)
    {
        root.remainCards[South][Diamonds] |= (1u << (14 - extra - i));
    }

    RungFixture fixture{};
    fixture.root = root;
    fixture.declarer = North;
    fixture.tricks_needed = half;  // every remaining trick
    fixture.history = PlayTraceBin{};
    fixture.opening_leader = East;
    fixture.expected_size = choose(pool_size, half);
    return fixture;
}

auto make_solver_rung_a() -> RungFixture
{
    return make_solver_rung(4);
}

auto make_solver_rung_b() -> RungFixture
{
    return make_solver_rung(6);
}

}  // namespace dds::belief_evaluation::benchmarks
