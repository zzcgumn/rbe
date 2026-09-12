#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <set>
#include <utility>
#include <vector>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

#include <belief_evaluation/constrained_decomposition.hpp>
#include <belief_evaluation/history_verification.hpp>
#include <belief_evaluation/unconstrained_layout_source.hpp>
#include <belief_evaluation/node.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;

using be::ConstrainedSpaceStatus;
using be::HistoryVerdict;
using be::UnconstrainedLayoutSource;
using be::holding;
using be::is_consistent;
using be::layout_key;

namespace
{
    constexpr int Spades = 0;
    constexpr int Hearts = 1;
    constexpr int Diamonds = 2;
    constexpr int Clubs = 3;

    constexpr int North = 0;  // declarer in every fixture below
    constexpr int East = 1;
    constexpr int South = 2;  // dummy
    constexpr int West = 3;

    /// Ten pooled cards, five each -- C(10, 5) = 252, small enough to
    /// enumerate exhaustively, and large enough that sampling is
    /// meaningful. Not mid-trick; the mid-trick shape is covered by its
    /// own fixture below.
    auto make_ten_card_pool_root() -> Deal
    {
        Deal root{};
        root.trump = DDS_NOTRUMP;
        root.first = North;
        root.remainCards[North][Spades] = holding({14, 13});  // declarer
        root.remainCards[South][Hearts] = holding({14, 13});  // dummy
        root.remainCards[East][Diamonds] = holding({2, 3, 4});
        root.remainCards[East][Clubs] = holding({5, 6});
        root.remainCards[West][Diamonds] = holding({7, 8});
        root.remainCards[West][Clubs] = holding({9, 10, 11});
        return root;
    }

    auto make_mid_trick_root() -> Deal
    {
        Deal root{};
        root.trump = DDS_NOTRUMP;
        root.first = North;
        root.currentTrickSuit[0] = Diamonds;
        root.currentTrickRank[0] = 14;  // East already played the ace
        root.remainCards[North][Spades] = holding({13});
        root.remainCards[South][Hearts] = holding({13});
        root.remainCards[East][Diamonds] = holding({2, 3});
        root.remainCards[West][Diamonds] = holding({4, 5, 6});
        return root;
    }

    auto make_small_single_suit_root() -> Deal
    {
        Deal root{};
        root.trump = Spades;
        root.first = West;
        root.remainCards[North][Hearts] = holding({14});
        root.remainCards[South][Clubs] = holding({14});
        root.remainCards[East][Spades] = holding({2});
        root.remainCards[West][Spades] = holding({3});
        return root;
    }

    /// Neither defender holds anything -- an empty pool, C(0, 0) = 1: the
    /// degenerate space of exactly one member, the root itself. Every
    /// underlying piece (defender_pool_decomposition, unrank_combination,
    /// keyed_permutation) already has its own test for this shape in
    /// isolation; this is the composed-source-level version of the same
    /// claim.
    auto make_empty_pool_root() -> Deal
    {
        Deal root{};
        root.trump = DDS_NOTRUMP;
        root.first = North;
        root.remainCards[North][Spades] = holding({14, 13});
        root.remainCards[South][Hearts] = holding({14, 13});
        // East and West hold nothing: the pool is empty.
        return root;
    }

    using CardPair = std::pair<int, int>;  // (suit, rank)

    auto all_52_cards() -> std::vector<CardPair>
    {
        std::vector<CardPair> cards;
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            for (int rank = 2; rank <= 14; ++rank)
            {
                cards.emplace_back(suit, rank);
            }
        }
        return cards;
    }

    /// Builds a root + history pair that together account for exactly the
    /// 52-card deck -- see history_verification_test.cpp's own copy of this
    /// helper for the full rationale. `played` (in order) becomes the
    /// history; every other card goes to `hand_for(suit, rank)`.
    auto build_deal(
        std::vector<CardPair> const& played, std::function<int(int, int)> const& hand_for)
        -> std::pair<Deal, PlayTraceBin>
    {
        std::set<CardPair> const played_set(played.begin(), played.end());

        Deal root{};
        for (auto const& [suit, rank] : all_52_cards())
        {
            if (played_set.count({suit, rank}) != 0)
            {
                continue;
            }
            root.remainCards[hand_for(suit, rank)][suit] |= (1u << rank);
        }

        PlayTraceBin history{};
        history.number = static_cast<int>(played.size());
        for (std::size_t i = 0; i < played.size(); ++i)
        {
            history.suit[i] = played[i].first;
            history.rank[i] = played[i].second;
        }

        return {root, history};
    }

    /// A root and history where East (the fixed seat: `(declarer + 1) %
    /// DDS_HANDS`) has shown void in diamonds, and diamonds remain in the
    /// defender pool -- small enough to enumerate exhaustively (size 3), and
    /// with the voided suit actually live in the pool so the void
    /// constraint has something to bite on.
    ///
    /// Trick one (no-trump, diamonds led): North (declarer) leads the ace,
    /// East discards a spade -- void in diamonds -- South (dummy) follows
    /// the king, West follows the queen. North's ace is highest, so North
    /// wins and leads next; no trick is in progress at this root.
    ///
    /// After the trick: East holds one club (C4); West holds three
    /// diamonds (D5, D7, D9) and two more clubs (C6, C9). Every other card
    /// in the deck -- the bulk of it -- is dumped on North, which nothing
    /// here depends on the realism of (declarer's exact holding is not
    /// part of what varies across a node, so an unrealistically large one
    /// is harmless).
    ///
    /// Pool (suits ascending, ranks ascending): D5, D7, D9, C4, C6, C9 (6
    /// cards); East's own count is 1 (C4). East void in diamonds forces
    /// D5/D7/D9 to West; the three clubs are free; East still needs 1 of
    /// them: C(3, 1) = 3.
    auto make_void_ending() -> std::pair<Deal, PlayTraceBin>
    {
        auto const hand_for = [](int suit, int rank) -> int
        {
            if (suit == Diamonds && (rank == 5 || rank == 7 || rank == 9))
            {
                return West;
            }
            if (suit == Clubs && (rank == 4 || rank == 6 || rank == 9))
            {
                return rank == 4 ? East : West;
            }
            return North;
        };
        auto [root, history] =
            build_deal({{Diamonds, 14}, {Spades, 2}, {Diamonds, 13}, {Diamonds, 12}}, hand_for);
        root.trump = DDS_NOTRUMP;
        root.first = North;
        return {root, history};
    }
}  // namespace

// --- acceptance 1: size() matches hand-derived counts, several shapes -----

TEST(UnconstrainedLayoutSourceTest, SizeMatchesTheHandDerivedCountOnATenCardPool)
{
    UnconstrainedLayoutSource const source(make_ten_card_pool_root(), North, /*seed=*/1u);
    EXPECT_EQ(source.size(), 252u);  // C(10, 5)
}

TEST(UnconstrainedLayoutSourceTest, SizeMatchesTheHandDerivedCountOnAMidTrickEnding)
{
    // Pool is 5 diamonds (East 2, West 3, since East already played the
    // ace to the current trick) -- C(5, 2) = 10.
    UnconstrainedLayoutSource const source(make_mid_trick_root(), North, /*seed=*/1u);
    EXPECT_EQ(source.size(), 10u);
}

TEST(UnconstrainedLayoutSourceTest, SizeMatchesTheHandDerivedCountOnATwoCardEnding)
{
    // One spade each -- C(2, 1) = 2.
    UnconstrainedLayoutSource const source(make_small_single_suit_root(), North, /*seed=*/1u);
    EXPECT_EQ(source.size(), 2u);
}

TEST(UnconstrainedLayoutSourceTest, AnEmptyDefenderPoolGivesTheDegenerateSizeOneSpace)
{
    // C(0, 0) = 1: the composed source's own version of the size() == 1
    // degenerate case -- each underlying piece already covers it in
    // isolation (defender_pool_decomposition, unrank_combination,
    // keyed_permutation each have their own empty/k==n test), and this
    // closes the gap at the level a caller actually meets.
    Deal const root = make_empty_pool_root();
    UnconstrainedLayoutSource const source(root, North, /*seed=*/1u);
    ASSERT_EQ(source.size(), 1u);
    EXPECT_TRUE(is_consistent(source.at(0), root, North, South));
    // The one member of a size-one space is the root's own split -- East
    // and West both hold nothing either way.
    EXPECT_EQ(layout_key(source.at(0), East), layout_key(root, East));
}

// --- acceptance 3: at(i) is a bijection over the whole space --------------

TEST(UnconstrainedLayoutSourceTest, AtIsABijectionOverTheWholeSpaceOnTheTenCardPool)
{
    Deal const root = make_ten_card_pool_root();
    UnconstrainedLayoutSource const source(root, North, /*seed=*/1u);
    std::uint64_t const total = *source.size();
    ASSERT_EQ(total, 252u);

    std::set<std::uint64_t> keys;
    for (std::uint64_t index = 0; index < total; ++index)
    {
        Deal const layout = source.at(index);
        // Every layout is consistent with the root: the underlying pieces
        // (defender_pool_decomposition, unrank_combination,
        // keyed_permutation, apply_defender_split) are each separately
        // verified in their own tests, but wiring is what goes wrong, so
        // this re-checks the claim on the assembled, composed type too.
        EXPECT_TRUE(is_consistent(layout, root, North, South)) << "index=" << index;
        keys.insert(layout_key(layout, East));
    }
    EXPECT_EQ(keys.size(), total);  // every index produced a distinct layout
}

// --- acceptance 2: every layout is a legal position, whole space ----------

TEST(UnconstrainedLayoutSourceTest, EveryLayoutOverTheWholeSpaceIsALegalPosition)
{
    Deal const root = make_ten_card_pool_root();
    UnconstrainedLayoutSource const source(root, North, /*seed=*/1u);
    std::uint64_t const total = *source.size();

    int const east_root_count = be::card_count(root, East);
    int const west_root_count = be::card_count(root, West);

    for (std::uint64_t index = 0; index < total; ++index)
    {
        Deal const layout = source.at(index);
        EXPECT_EQ(be::card_count(layout, East), east_root_count) << "index=" << index;
        EXPECT_EQ(be::card_count(layout, West), west_root_count) << "index=" << index;
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            EXPECT_EQ(layout.remainCards[East][suit] & layout.remainCards[West][suit], 0u)
                << "index=" << index << " suit=" << suit;
        }
    }
}

// --- acceptance 4: deterministic ---------------------------------------------

TEST(UnconstrainedLayoutSourceTest, AtIsDeterministicAcrossRepeatedCallsOnOneSource)
{
    UnconstrainedLayoutSource const source(make_ten_card_pool_root(), North, /*seed=*/7u);
    for (std::uint64_t index = 0; index < 20; ++index)
    {
        Deal const first = source.at(index);
        Deal const second = source.at(index);
        EXPECT_EQ(layout_key(first, East), layout_key(second, East)) << "index=" << index;
    }
}

TEST(UnconstrainedLayoutSourceTest, AtIsDeterministicAcrossTwoSeparatelyConstructedSourcesWithTheSameSeed)
{
    Deal const root = make_ten_card_pool_root();
    UnconstrainedLayoutSource const a(root, North, /*seed=*/7u);
    UnconstrainedLayoutSource const b(root, North, /*seed=*/7u);
    for (std::uint64_t index = 0; index < 20; ++index)
    {
        EXPECT_EQ(layout_key(a.at(index), East), layout_key(b.at(index), East)) << "index=" << index;
    }
}

// --- acceptance 5: different seeds give different orders, same set -------

TEST(UnconstrainedLayoutSourceTest, TwoSeedsGiveDifferentOrdersOfTheSameSetOfLayouts)
{
    Deal const root = make_ten_card_pool_root();
    UnconstrainedLayoutSource const a(root, North, /*seed=*/1u);
    UnconstrainedLayoutSource const b(root, North, /*seed=*/2u);
    std::uint64_t const total = *a.size();
    ASSERT_EQ(total, *b.size());

    std::vector<std::uint64_t> keys_a;
    std::vector<std::uint64_t> keys_b;
    for (std::uint64_t index = 0; index < total; ++index)
    {
        keys_a.push_back(layout_key(a.at(index), East));
        keys_b.push_back(layout_key(b.at(index), East));
    }
    EXPECT_NE(keys_a, keys_b);  // different order

    std::set<std::uint64_t> const set_a(keys_a.begin(), keys_a.end());
    std::set<std::uint64_t> const set_b(keys_b.begin(), keys_b.end());
    EXPECT_EQ(set_a, set_b);  // same set of layouts
}

// --- acceptance 6: out-of-range behaviour, stated and pinned --------------

TEST(UnconstrainedLayoutSourceTest, OutOfRangeIndexIsAnAssertedCallerError)
{
    // EXPECT_DEBUG_DEATH, not EXPECT_DEATH: the assert this pins is compiled
    // away under NDEBUG, so a death expectation that does not know about
    // that build mode would fail there for the wrong reason -- the program
    // no longer dies, not because the check stopped working. EXPECT_DEBUG_DEATH
    // checks for death only in a build where assert is actually active, and
    // is a no-op verification (just runs the statement) otherwise.
    UnconstrainedLayoutSource const source(make_small_single_suit_root(), North, /*seed=*/1u);
    ASSERT_EQ(source.size(), 2u);
    EXPECT_DEBUG_DEATH({ source.at(2); }, "");
}

// --- an empty history is bit-identical to the pre-history constructor -----

TEST(UnconstrainedLayoutSourceTest, AnEmptyHistoryIsBitIdenticalOverTheWholeSpace)
{
    // Prove it, not assume it: construct the same root two ways -- the
    // original three-argument call every existing call site still makes,
    // and the new five-argument form with an explicit empty history -- and
    // compare size() and every single at(i), not a sample of them.
    Deal const root = make_ten_card_pool_root();
    UnconstrainedLayoutSource const original(root, North, /*seed=*/1u);
    UnconstrainedLayoutSource const explicit_empty_history(
        root, North, /*seed=*/1u, PlayTraceBin{}, /*opening_leader=*/North);

    ASSERT_EQ(original.size(), explicit_empty_history.size());
    ASSERT_EQ(original.size(), 252u);  // C(10, 5), same as SizeMatchesTheHandDerivedCountOnATenCardPool
    EXPECT_EQ(original.history_verdict(), HistoryVerdict::Consistent);
    EXPECT_EQ(original.constrained_space_status(), ConstrainedSpaceStatus::Ok);

    for (std::uint64_t index = 0; index < *original.size(); ++index)
    {
        EXPECT_EQ(layout_key(original.at(index), East), layout_key(explicit_empty_history.at(index), East))
            << "index=" << index;
    }
}

// --- a supplied history narrows the space, whole space verified -----------

TEST(UnconstrainedLayoutSourceTest, AHistoryShrinksSizeToTheConstrainedCountAndNoLayoutLeaksTheVoidedSuit)
{
    auto const [root, history] = make_void_ending();
    UnconstrainedLayoutSource const source(root, North, /*seed=*/1u, history, /*opening_leader=*/North);

    ASSERT_EQ(source.history_verdict(), HistoryVerdict::Consistent);
    ASSERT_EQ(source.constrained_space_status(), ConstrainedSpaceStatus::Ok);
    ASSERT_EQ(source.size(), 3u);  // C(3, 1) -- see make_void_ending()'s own doxygen

    std::set<std::uint64_t> keys;
    for (std::uint64_t index = 0; index < *source.size(); ++index)
    {
        Deal const layout = source.at(index);
        EXPECT_TRUE(is_consistent(layout, root, North, South)) << "index=" << index;
        // East showed void in diamonds -- no generated layout may give East
        // a diamond, over the whole space, not a sample of it.
        EXPECT_EQ(layout.remainCards[East][Diamonds], 0u) << "index=" << index;
        // And West must hold exactly the three diamonds the void forced --
        // nothing free ever moves them.
        EXPECT_EQ(layout.remainCards[West][Diamonds], holding({5, 7, 9})) << "index=" << index;
        keys.insert(layout_key(layout, East));
    }
    EXPECT_EQ(keys.size(), 3u);  // at() is a bijection over the constrained space
}

// --- determinism holds with a history too ----------------------------------

TEST(UnconstrainedLayoutSourceTest, AtIsDeterministicWithAHistoryAcrossTwoSeparatelyConstructedSources)
{
    auto const [root, history] = make_void_ending();
    UnconstrainedLayoutSource const a(root, North, /*seed=*/7u, history, /*opening_leader=*/North);
    UnconstrainedLayoutSource const b(root, North, /*seed=*/7u, history, /*opening_leader=*/North);
    ASSERT_EQ(a.size(), b.size());
    for (std::uint64_t index = 0; index < *a.size(); ++index)
    {
        EXPECT_EQ(layout_key(a.at(index), East), layout_key(b.at(index), East)) << "index=" << index;
    }
}

// --- a rejected history and an empty constrained space are distinguishable,
// and neither reads as "the source had nothing consistent in it" -----------

TEST(UnconstrainedLayoutSourceTest, ARejectedHistoryIsReportedAndTheSpaceIsEmptyButDistinguishable)
{
    auto [root, history] = make_void_ending();
    // Duplicate a played card -- the same shape history_verification_test.cpp
    // uses to pin DuplicatedCard -- so verify_history rejects this history
    // outright.
    history.suit[3] = history.suit[0];
    history.rank[3] = history.rank[0];

    UnconstrainedLayoutSource const source(root, North, /*seed=*/1u, history, /*opening_leader=*/North);

    EXPECT_EQ(source.history_verdict(), HistoryVerdict::DuplicatedCard);
    // Not meaningful -- the decomposition was never attempted -- but still
    // the harmless default, not some other value a caller might mistake for
    // a real diagnosis.
    EXPECT_EQ(source.constrained_space_status(), ConstrainedSpaceStatus::Ok);
    EXPECT_EQ(source.size(), 0u);
}

TEST(UnconstrainedLayoutSourceTest, AContradictoryHistoryIsAcceptedButLeavesAnEmptySpaceWithItsOwnCause)
{
    // North leads a diamond, East discards a spade (void), South follows
    // suit, West discards a club (void too) -- both defenders void in
    // diamonds, and a diamond (D11) is left outstanding in West's own
    // post-trick holding below, so the contradiction is not vacuous.
    // verify_history has nothing to reject here -- the 52-card partition,
    // the trailing trick and the declarer/dummy cross-check all pass -- so
    // this is the constrained decomposition's own empty cause, not a
    // rejected history.
    auto const hand_for = [](int suit, int rank) -> int
    {
        if (suit == Diamonds && rank == 11)
        {
            return West;  // still in the pool, in the suit both defenders denied
        }
        return North;
    };
    auto [root, history] = build_deal(
        {{Diamonds, 14}, {Spades, 2}, {Diamonds, 13}, {Clubs, 3}}, hand_for);
    root.trump = DDS_NOTRUMP;
    root.first = North;  // North's ace was highest; neither discard could win

    UnconstrainedLayoutSource const source(root, North, /*seed=*/1u, history, /*opening_leader=*/North);

    ASSERT_EQ(source.history_verdict(), HistoryVerdict::Consistent);
    EXPECT_EQ(source.constrained_space_status(), ConstrainedSpaceStatus::ContradictoryVoid);
    EXPECT_EQ(source.size(), 0u);
}
