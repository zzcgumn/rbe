// The C++ side of this capability's own Python-vs-C++ per-callback cost
// comparison: loops the same evaluate() call many times and times it, for
// two pi shapes (trivial, real Python-side work) crossed with two sampling
// configurations (replenishing, non-replenishing -- see this file's own
// module doxygen below for why both). timer.py is the identical
// comparison from the Python binding's own side; report.py runs both and
// tabulates the ratio.
//
// Not a cc_test: this measures wall time, which is exactly what
// benchmarks/belief_evaluation/instrument.cpp's own header comment already
// explains a cc_test must never assert on. A cc_binary instead, not tagged
// manual, so `bazel test //...` builds it everywhere and never runs it --
// running it is report.py's job.
//
// Solver-free: nothing here needs solve_board, only the same scripted
// strategies the rest of this plan's own benchmarks use.
//
// Deliberately duplicates its own fixture and strategies rather than
// reaching into library/tests/belief_evaluation/test_support.hpp: that
// header is private to the core C++ test suite (its own module comment
// says so), and benchmarks/belief_evaluation/strategies.hpp already
// established the precedent this file follows -- a small fixture or
// strategy belongs beside the benchmark that uses it, not folded into a
// private test-only header. safety_score_declarer_play's own correctness
// is proven once, in test_support.hpp's copy
// (safety_score_declarer_play_test.cpp) and its independent Python port
// (test_belief_space_local_evaluation_python_cost.py) -- this copy is
// exercised only for its timing, not re-proven here.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <bit>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/defender_strategy.hpp>
#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/exhaustive_layout_source.hpp>

namespace be = dds::belief_evaluation;

namespace
{
#if defined(__APPLE__)
    constexpr char const* kPlatform = "macos";
#elif defined(__linux__)
    constexpr char const* kPlatform = "linux";
#else
    constexpr char const* kPlatform = "unknown";
#endif

#ifdef NDEBUG
    constexpr char const* kMode = "opt";
#else
    constexpr char const* kMode = "default";
#endif

    constexpr int Spades = 0;
    constexpr int North = 0;
    constexpr int East = 1;
    constexpr int South = 2;
    constexpr int West = 3;

    // Identical to parity_reference.cpp's own make_two_card_finesse_root
    // and exhaustive_layout_source_integration_test.cpp's own -- the third
    // independent copy of this fixture in the tree, matching those two
    // files' own established precedent rather than reaching across a
    // package boundary for it.
    auto make_two_card_finesse_root() -> Deal
    {
        Deal root{};
        root.trump = DDS_NOTRUMP;
        root.first = East;
        root.remainCards[North][Spades] = (1u << 9);
        root.remainCards[South][Spades] = (1u << 8);
        root.remainCards[East][Spades] = (1u << 2) | (1u << 3);
        root.remainCards[West][Spades] = (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 10);
        return root;
    }

    auto seat_on_play(Deal const& deal) -> int
    {
        int played = 0;
        for (int i = 0; i < 3 && deal.currentTrickRank[i] != 0; ++i)
        {
            ++played;
        }
        return (deal.first + played) % DDS_HANDS;
    }

    auto lowest_legal_card(Deal const& deal, int seat) -> be::Card
    {
        int led = -1;
        if (deal.currentTrickRank[0] != 0)
        {
            led = deal.currentTrickSuit[0];
        }
        if (led != -1 && deal.remainCards[seat][led] != 0)
        {
            for (int rank = 2; rank <= 14; ++rank)
            {
                if ((deal.remainCards[seat][led] & (1u << rank)) != 0)
                {
                    return be::Card{led, rank};
                }
            }
        }
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            unsigned const holding = deal.remainCards[seat][suit];
            for (int rank = 2; rank <= 14; ++rank)
            {
                if ((holding & (1u << rank)) != 0)
                {
                    return be::Card{suit, rank};
                }
            }
        }
        return be::Card{};
    }

    auto trivial_declarer_play(be::ObservationState const& state, be::BeliefView const&) -> be::Card
    {
        return lowest_legal_card(state.known_holdings, seat_on_play(state.known_holdings));
    }

    auto trivial_defender_play(be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
    {
        return {be::WeightedCard{lowest_legal_card(query.layout, query.seat), 1.0}};
    }

    auto enumerate_legal_cards(Deal const& deal, int seat) -> std::vector<be::Card>
    {
        std::vector<be::Card> cards;
        auto const collect = [&](int suit)
        {
            unsigned const suit_holding = deal.remainCards[seat][suit];
            for (int rank = 2; rank <= 14; ++rank)
            {
                if ((suit_holding & (1u << rank)) != 0)
                {
                    cards.push_back(be::Card{suit, rank});
                }
            }
        };
        int led = -1;
        if (deal.currentTrickRank[0] != 0)
        {
            led = deal.currentTrickSuit[0];
        }
        if (led != -1 && deal.remainCards[seat][led] != 0)
        {
            collect(led);
            return cards;
        }
        for (int suit = 0; suit < DDS_SUITS; ++suit)
        {
            collect(suit);
        }
        return cards;
    }

    auto higher_defender_count(Deal const& layout, int declarer, int suit, int rank) -> int
    {
        int const east = (declarer + 1) % DDS_HANDS;
        int const west = (declarer + 3) % DDS_HANDS;
        unsigned const holding = layout.remainCards[east][suit] | layout.remainCards[west][suit];
        unsigned const above_rank = ~((1u << (rank + 1)) - 1u);
        return std::popcount(holding & above_rank);
    }

    // The "real work" pi -- identical rule to
    // test_support.hpp's safety_score_declarer_play (see that copy's own
    // doxygen for what it computes and why; proven correct there and in
    // its Python port, not re-proven here).
    auto real_work_declarer_play(be::ObservationState const& state, be::BeliefView const& view) -> be::Card
    {
        int const seat = seat_on_play(state.known_holdings);
        std::vector<be::Card> const candidates = enumerate_legal_cards(state.known_holdings, seat);

        be::Card best{};
        double best_score = 0.0;
        bool have_best = false;
        for (be::Card const& candidate : candidates)
        {
            double score = 0.0;
            for (be::BeliefEntry const& entry : view.entries)
            {
                score += entry.posterior
                    * static_cast<double>(higher_defender_count(
                          entry.layout, state.declarer, candidate.suit, candidate.rank));
            }
            if (! have_best || score < best_score || (score == best_score && candidate.rank < best.rank))
            {
                best = candidate;
                best_score = score;
                have_best = true;
            }
        }
        return best;
    }

    struct Config
    {
        char const* label;
        be::EvaluateOptions options;
    };

    auto run_config(
        char const* pi_label, be::DeclarerStrategy const& pi, Config const& config, int iterations) -> void
    {
        Deal const root = make_two_card_finesse_root();
        constexpr std::uint64_t seed = 7u;  // parity_reference.cpp's own SAMPLED_SEED

        // Accumulated and printed below (not a per-iteration volatile) so
        // nothing above is optimised away as dead code -- genuinely read,
        // not merely written, which is what makes this survive -Werror's
        // own unused-but-set-variable check as well as an optimiser's.
        // Not a correctness check either way; that is what the gtest/py_test
        // guards are for.
        double sink = 0.0;
        auto const start = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i)
        {
            be::ExhaustiveLayoutSource const source(root, North, seed);
            be::EvaluationResult const result =
                be::evaluate(root, North, /*tricks_needed=*/1, source, pi, trivial_defender_play, config.options);
            if (! result.error.has_value())
            {
                sink += result.by_strategy.at(pi.id).p_make;
            }
        }
        auto const end = std::chrono::steady_clock::now();
        double const total_ms = std::chrono::duration<double, std::milli>(end - start).count();
        std::printf(
            "cpp pi=%-9s config=%-16s mode=%s platform=%s iterations=%d ms_per_iteration=%.6f sink=%.6f\n",
            pi_label, config.label, kMode, kPlatform, iterations, total_ms / iterations, sink);
    }
}  // namespace

auto main(int argc, char** argv) -> int
{
    int iterations = 2000;  // the earlier order-of-magnitude check's own loop count, for a direct re-run
    if (argc > 1)
    {
        iterations = std::atoi(argv[1]);
    }

    // Same root, same declarer, same tricks_needed, same seed, same
    // sample_size/scan_budget throughout -- only replenish_below differs,
    // so "replenishing" vs "non-replenishing" isolates exactly the
    // node-local replenishment scan's own effect on how often delta is
    // called, per this task's own background.
    be::EvaluateOptions non_replenishing{};
    non_replenishing.sampling.sample_size = 10u;
    non_replenishing.sampling.scan_budget = 100u;

    be::EvaluateOptions replenishing = non_replenishing;
    replenishing.sampling.replenish_below = 5u;  // parity_reference.cpp's own SAMPLED_REPLENISH_BELOW

    Config const configs[] = {
        Config{"non_replenishing", non_replenishing},
        Config{"replenishing", replenishing},
    };

    be::DeclarerStrategy const trivial{.id = 1, .play = trivial_declarer_play, .state_key = nullptr};
    be::DeclarerStrategy const real_work{.id = 2, .play = real_work_declarer_play, .state_key = nullptr};

    for (Config const& config : configs)
    {
        run_config("trivial", trivial, config, iterations);
        run_config("real_work", real_work, config, iterations);
    }
    return 0;
}
