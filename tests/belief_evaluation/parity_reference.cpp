// Not a gtest binary: a plain program, invoked as a subprocess from
// test_belief_space_local_evaluation_parity.py, that builds a handful of
// fixtures, runs the real C++ evaluator over them, and prints both the
// fixture (as a Python deal-dict literal) and the answer (also as a
// Python literal) to stdout.
//
// This is the "build both sides of every comparison from one description"
// mechanism the parity task calls for. The alternative -- writing the same
// fixture twice, once by hand in each language -- is exactly how a
// transcription slip hides: the Python side would still run, still
// produce *some* answer, and pass or fail for a reason that has nothing
// to do with the binding. Reading this binary's own output instead means
// the Python test's fixture and the C++ answer it compares against both
// come from the same construction, once.
//
// Solver-free, matching //library/src/belief_evaluation's own deps below:
// nothing here needs solve_board, only the scripted strategies every
// other C++ test in this suite already uses.
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

#include <api/dds_data_types.hpp>
#include <utility/constants.h>

#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/exhaustive_layout_source.hpp>

#include "test_support.hpp"

namespace be = dds::belief_evaluation;

namespace
{
    constexpr int Spades = 0;
    constexpr int Clubs = 3;

    constexpr int North = 0;  // declarer throughout
    constexpr int East = 1;
    constexpr int South = 2;  // dummy
    constexpr int West = 3;

    // --- three roots of different shapes, mirroring exhaustive_layout_source_test.cpp's
    // own fixtures of the same names exactly (same file this binary's own
    // BUILD target already depends on for its test coverage of the type) ---

    auto make_ten_card_pool_root() -> Deal
    {
        Deal root{};
        root.trump = DDS_NOTRUMP;
        root.first = North;
        root.remainCards[North][Spades] = be::holding({14, 13});
        root.remainCards[South][1] = be::holding({14, 13});  // Hearts, filler
        root.remainCards[East][2] = be::holding({2, 3, 4});  // Diamonds
        root.remainCards[East][Clubs] = be::holding({5, 6});
        root.remainCards[West][2] = be::holding({7, 8});
        root.remainCards[West][Clubs] = be::holding({9, 10, 11});
        return root;
    }

    auto make_mid_trick_root() -> Deal
    {
        Deal root{};
        root.trump = DDS_NOTRUMP;
        root.first = North;
        root.currentTrickSuit[0] = 2;  // Diamonds
        root.currentTrickRank[0] = 14;
        root.remainCards[North][Spades] = be::holding({13});
        root.remainCards[South][1] = be::holding({13});
        root.remainCards[East][2] = be::holding({2, 3});
        root.remainCards[West][2] = be::holding({4, 5, 6});
        return root;
    }

    auto make_small_single_suit_root() -> Deal
    {
        Deal root{};
        root.trump = Spades;
        root.first = West;
        root.remainCards[North][1] = be::holding({14});
        root.remainCards[South][Clubs] = be::holding({14});
        root.remainCards[East][Spades] = be::holding({2});
        root.remainCards[West][Spades] = be::holding({3});
        return root;
    }

    // --- the sampled, replenishing fixture -- East holds
    // two of a seven-card pool, matching
    // exhaustive_layout_source_integration_test.cpp's own
    // make_two_card_finesse_root, so depth-1 nodes have room to differ by
    // sample and a node-local replenishment scan has real work to do ---

    auto make_two_card_finesse_root() -> Deal
    {
        Deal root{};
        root.trump = DDS_NOTRUMP;
        root.first = East;
        root.remainCards[North][Spades] = be::holding({9});
        root.remainCards[South][Spades] = be::holding({8});
        root.remainCards[East][Spades] = be::holding({2, 3});
        root.remainCards[West][Spades] = be::holding({4, 5, 6, 7, 10});
        return root;
    }

    // --- oracle cases, mirroring oracle_test.cpp's own 9a,
    // 9b and 9e exactly ---

    auto make_certain_win_layout(int east_low, int west_low) -> Deal
    {
        Deal deal{};
        deal.trump = DDS_NOTRUMP;
        deal.first = North;
        deal.remainCards[North][Spades] = be::holding({14});
        deal.remainCards[East][Spades] = be::holding({east_low});
        deal.remainCards[South][Spades] = be::holding({3});
        deal.remainCards[West][Spades] = be::holding({west_low});
        return deal;
    }

    auto make_delta_matters_layout() -> Deal
    {
        Deal layout{};
        layout.trump = DDS_NOTRUMP;
        layout.first = East;
        layout.remainCards[North][Spades] = be::holding({12});  // Queen
        layout.remainCards[North][Clubs] = be::holding({2});
        layout.remainCards[East][Spades] = be::holding({13, 2});  // King, Two
        layout.remainCards[South][Spades] = be::holding({3});
        layout.remainCards[South][Clubs] = be::holding({3});
        layout.remainCards[West][Spades] = be::holding({4});
        layout.remainCards[West][Clubs] = be::holding({4});
        return layout;
    }

    auto strategy(be::StrategyId id) -> be::DeclarerStrategy
    {
        return be::DeclarerStrategy{.id = id, .play = be::single_card_declarer_play, .state_key = nullptr};
    }

    auto print_deal(char const* name, Deal const& deal) -> void
    {
        std::printf("%s = {\n", name);
        std::printf("    \"trump\": %d,\n", deal.trump);
        std::printf("    \"first\": %d,\n", deal.first);
        std::printf("    \"remain_cards\": [\n");
        for (int hand = 0; hand < DDS_HANDS; ++hand) {
            std::printf(
                "        [%u, %u, %u, %u],\n", deal.remainCards[hand][0], deal.remainCards[hand][1],
                deal.remainCards[hand][2], deal.remainCards[hand][3]);
        }
        std::printf("    ],\n");
        std::printf(
            "    \"current_trick_suit\": (%d, %d, %d),\n", deal.currentTrickSuit[0], deal.currentTrickSuit[1],
            deal.currentTrickSuit[2]);
        std::printf(
            "    \"current_trick_rank\": (%d, %d, %d),\n", deal.currentTrickRank[0], deal.currentTrickRank[1],
            deal.currentTrickRank[2]);
        std::printf("}\n");
    }

    auto print_size(char const* name, std::optional<std::uint64_t> const& size) -> void
    {
        if (size.has_value()) {
            std::printf("%s = %llu\n", name, static_cast<unsigned long long>(*size));
        } else {
            std::printf("%s = None\n", name);
        }
    }

    auto print_counters(char const* name, be::EvaluationCounters const& counters) -> void
    {
        std::printf("%s = {\n", name);
        std::printf("    \"nodes_visited\": %llu,\n", static_cast<unsigned long long>(counters.nodes_visited));
        std::printf(
            "    \"tier1_made_cuts\": %llu,\n", static_cast<unsigned long long>(counters.tier1_made_cuts));
        std::printf(
            "    \"tier1_dead_cuts\": %llu,\n", static_cast<unsigned long long>(counters.tier1_dead_cuts));
        std::printf("    \"tier2_cuts\": %llu,\n", static_cast<unsigned long long>(counters.tier2_cuts));
        std::printf("    \"sample_size_by_depth\": [\n");
        for (be::DepthSampleStats const& depth : counters.sample_size_by_depth) {
            std::printf(
                "        {\"nodes\": %llu, \"layout_sum\": %llu, \"layout_min\": %llu},\n",
                static_cast<unsigned long long>(depth.nodes), static_cast<unsigned long long>(depth.layout_sum),
                static_cast<unsigned long long>(depth.layout_min));
        }
        std::printf("    ],\n");
        std::printf("    \"replenishment_by_depth\": [\n");
        for (be::DepthReplenishmentStats const& depth : counters.replenishment_by_depth) {
            std::printf(
                "        {\"attempted\": %llu, \"succeeded\": %llu, \"layouts_added\": %llu, "
                "\"at_calls\": %llu},\n",
                static_cast<unsigned long long>(depth.attempted), static_cast<unsigned long long>(depth.succeeded),
                static_cast<unsigned long long>(depth.layouts_added),
                static_cast<unsigned long long>(depth.at_calls));
        }
        std::printf("    ],\n");
        std::printf("}\n");
    }
}  // namespace

auto main() -> int
{
    std::printf("# Generated by parity_reference -- do not hand-edit.\n");
    std::printf("# See this file's own header comment for why it exists.\n\n");

    // --- size() on three roots of different shapes -----------------------

    Deal const ten_card_pool_root = make_ten_card_pool_root();
    print_deal("TEN_CARD_POOL_ROOT", ten_card_pool_root);
    print_size(
        "TEN_CARD_POOL_SIZE", be::ExhaustiveLayoutSource(ten_card_pool_root, North, /*seed=*/1u).size());

    Deal const mid_trick_root = make_mid_trick_root();
    print_deal("MID_TRICK_ROOT", mid_trick_root);
    print_size("MID_TRICK_SIZE", be::ExhaustiveLayoutSource(mid_trick_root, North, /*seed=*/1u).size());

    Deal const small_single_suit_root = make_small_single_suit_root();
    print_deal("SMALL_SINGLE_SUIT_ROOT", small_single_suit_root);
    print_size(
        "SMALL_SINGLE_SUIT_SIZE",
        be::ExhaustiveLayoutSource(small_single_suit_root, North, /*seed=*/1u).size());

    // --- a LayoutSource's at() over the whole (small) space,
    // for direct layout-by-layout comparison against a Python subclass ---

    {
        be::ExhaustiveLayoutSource const source(small_single_suit_root, North, /*seed=*/1u);
        std::uint64_t const total = *source.size();
        std::printf("SMALL_SINGLE_SUIT_LAYOUTS = [\n");
        for (std::uint64_t index = 0; index < total; ++index) {
            std::printf("    {\n");
            Deal const layout = source.at(index);
            std::printf("        \"trump\": %d, \"first\": %d,\n", layout.trump, layout.first);
            std::printf("        \"remain_cards\": [\n");
            for (int hand = 0; hand < DDS_HANDS; ++hand) {
                std::printf(
                    "            [%u, %u, %u, %u],\n", layout.remainCards[hand][0], layout.remainCards[hand][1],
                    layout.remainCards[hand][2], layout.remainCards[hand][3]);
            }
            std::printf("        ],\n");
            std::printf(
                "        \"current_trick_suit\": (%d, %d, %d),\n", layout.currentTrickSuit[0],
                layout.currentTrickSuit[1], layout.currentTrickSuit[2]);
            std::printf(
                "        \"current_trick_rank\": (%d, %d, %d),\n", layout.currentTrickRank[0],
                layout.currentTrickRank[1], layout.currentTrickRank[2]);
            std::printf("    },\n");
        }
        std::printf("]\n");
    }

    // --- a sampled, replenishing run, p_make bitwise and
    // full counters ---

    {
        Deal const root = make_two_card_finesse_root();
        print_deal("SAMPLED_ROOT", root);
        be::ExhaustiveLayoutSource const source(root, North, /*seed=*/7u);
        be::EvaluationResult const result = be::evaluate(
            root, North, /*tricks_needed=*/1, source, strategy(1), be::single_card_defender,
            be::EvaluateOptions{
                .collect_counters = true,
                .sampling = {.sample_size = 10u, .scan_budget = 100u, .replenish_below = 5u}});
        std::printf("SAMPLED_SEED = 7\n");
        std::printf("SAMPLED_SAMPLE_SIZE = 10\n");
        std::printf("SAMPLED_SCAN_BUDGET = 100\n");
        std::printf("SAMPLED_REPLENISH_BELOW = 5\n");
        if (result.error.has_value()) {
            std::printf("SAMPLED_ERROR = True\n");
        } else {
            std::printf("SAMPLED_ERROR = False\n");
            std::printf("SAMPLED_P_MAKE = %.17g\n", result.by_strategy.at(1u).p_make);
            print_counters("SAMPLED_COUNTERS", *result.by_strategy.at(1u).counters);
        }
    }

    // --- oracle cases, mirroring oracle_test.cpp's own 9a
    // (certainty), 9b (impossibility) and 9e (delta actually matters) ---

    {
        be::VectorLayoutSource const source(
            {make_certain_win_layout(2, 4), make_certain_win_layout(4, 2), make_certain_win_layout(2, 2)});
        print_deal("ORACLE_CERTAINTY_LAYOUT", source.at(0));
        be::EvaluationResult const result =
            be::evaluate(source.at(0), North, /*tricks_needed=*/1, source, strategy(1), be::single_card_defender);
        std::printf("ORACLE_CERTAINTY_P_MAKE = %.17g\n", result.by_strategy.at(1u).p_make);
    }

    {
        Deal const layout = make_certain_win_layout(2, 4);
        be::VectorLayoutSource const source({layout});
        print_deal("ORACLE_IMPOSSIBILITY_LAYOUT", layout);
        be::EvaluationResult const result =
            be::evaluate(layout, North, /*tricks_needed=*/2, source, strategy(1), be::single_card_defender);
        std::printf("ORACLE_IMPOSSIBILITY_P_MAKE = %.17g\n", result.by_strategy.at(1u).p_make);
    }

    {
        Deal const layout = make_delta_matters_layout();
        be::VectorLayoutSource const source({layout});
        print_deal("ORACLE_DELTA_MATTERS_LAYOUT", layout);

        auto const east_still_has_the_choice = [](Deal const& deal)
        { return deal.remainCards[East][Spades] == be::holding({13, 2}); };
        auto const delta_plays_king = [=](be::DefenderQuery const& query) -> std::vector<be::WeightedCard> {
            if (query.seat == East && east_still_has_the_choice(query.layout)) {
                return {be::WeightedCard{be::Card{Spades, 13}, 1.0}};
            }
            return be::single_card_defender(query);
        };
        auto const delta_plays_two = [=](be::DefenderQuery const& query) -> std::vector<be::WeightedCard> {
            if (query.seat == East && east_still_has_the_choice(query.layout)) {
                return {be::WeightedCard{be::Card{Spades, 2}, 1.0}};
            }
            return be::single_card_defender(query);
        };

        be::EvaluationResult const with_king =
            be::evaluate(layout, North, /*tricks_needed=*/1, source, strategy(1), delta_plays_king);
        be::EvaluationResult const with_two =
            be::evaluate(layout, North, /*tricks_needed=*/1, source, strategy(1), delta_plays_two);
        std::printf("ORACLE_DELTA_MATTERS_WITH_KING_P_MAKE = %.17g\n", with_king.by_strategy.at(1u).p_make);
        std::printf("ORACLE_DELTA_MATTERS_WITH_TWO_P_MAKE = %.17g\n", with_two.by_strategy.at(1u).p_make);
    }

    return 0;
}
