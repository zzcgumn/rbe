// The C++ side of the Python-against-C++ callback cost comparison. It
// loops one evaluate() call many times and times it, for two pi (trivial,
// and one with real work) crossed with two sampling configurations
// (replenishing and non-replenishing). timer.py does the same through the
// Python binding; report.py runs both and tabulates the ratio.
//
// Not a cc_test: it measures wall time, and a test must never assert on
// wall time (see benchmarks/belief_evaluation/instrument.cpp). It is a
// cc_binary, not tagged manual, so `bazel test //...` builds it everywhere
// and never runs it. report.py runs it.
//
// Solver-free. The pi and delta it times are in strategies.hpp, beside
// this file.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <api/dds_constants.hpp>
#include <api/dds_data_types.hpp>

#include <belief_evaluation/declarer_strategy.hpp>
#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/exhaustive_layout_source.hpp>

#include "strategies.hpp"

namespace be = dds::belief_evaluation;
namespace strategies = dds::belief_evaluation::benchmarks::python_cost;

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

    // The same fixture as make_two_card_finesse_root in parity_reference.cpp
    // and exhaustive_layout_source_integration_test.cpp. A fixture this small
    // is copied, not shared across a package boundary.
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

    struct Config
    {
        char const* label;
        be::EvaluateOptions options;
    };

    auto run_config(
        char const* pi_label, be::DeclarerStrategy const& pi, Config const& config, int iterations) -> void
    {
        Deal const root = make_two_card_finesse_root();
        constexpr std::uint64_t seed = 7u;  // SAMPLED_SEED in parity_reference.cpp

        // Accumulated and printed below so the optimiser cannot remove the calls
        // as dead code. Reading it also satisfies -Werror's
        // unused-but-set-variable check. It checks nothing; the gtest and py_test
        // guards do that.
        double sink = 0.0;
        auto const start = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i)
        {
            be::ExhaustiveLayoutSource const source(root, North, seed);
            be::EvaluationResult const result = be::evaluate(
                root, North, /*tricks_needed=*/1, source, pi, strategies::trivial_defender_play,
                config.options);
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
    int iterations = 2000;  // the loop count of an earlier check, so results compare directly
    if (argc > 1)
    {
        iterations = std::atoi(argv[1]);
    }
    if (iterations < 1)
    {
        // Zero would divide total_ms by zero below, and a negative count is
        // meaningless. Reject both rather than print an invalid
        // ms_per_iteration.
        std::fprintf(stderr, "iterations must be a positive integer, got %d\n", iterations);
        return 1;
    }

    // Root, declarer, tricks_needed, seed, sample_size and scan_budget are
    // the same throughout; only replenish_below differs. So the comparison
    // isolates the effect of node-local replenishment on how often delta is
    // called.
    be::EvaluateOptions non_replenishing{};
    non_replenishing.sampling.sample_size = 10u;
    non_replenishing.sampling.scan_budget = 100u;

    be::EvaluateOptions replenishing = non_replenishing;
    replenishing.sampling.replenish_below = 5u;  // SAMPLED_REPLENISH_BELOW in parity_reference.cpp

    Config const configs[] = {
        Config{"non_replenishing", non_replenishing},
        Config{"replenishing", replenishing},
    };

    be::DeclarerStrategy const trivial{.id = 1, .play = strategies::trivial_declarer_play, .state_key = nullptr};
    be::DeclarerStrategy const real_work{
        .id = 2, .play = strategies::real_work_declarer_play, .state_key = nullptr};

    for (Config const& config : configs)
    {
        run_config("trivial", trivial, config, iterations);
        run_config("real_work", real_work, config, iterations);
    }
    return 0;
}
