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
// Solver-free: nothing here needs solve_board. The pi/delta pair timed
// below lives in strategies.hpp, beside this file -- see that header's own
// comment for why it is not library/tests/belief_evaluation/test_support.hpp
// (this plan's own scope rule: no executable line changed under
// `library/`).
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
