// A measuring instrument, not a test: runs one fixture from fixtures.hpp
// through evaluate() with a caller-chosen seed, sample size, and
// replenishment threshold, and prints what happened. Not tagged manual, so
// `bazel test //...` builds it on every platform and never runs it --
// running it is the whole point of a benchmark, not a fast check.
//
// Two separate modes rather than one run producing both: --mode=count
// (the default) collects EvaluationCounters and prints them raw, never as
// a ratio (see this file's own header for why); --mode=time instead times
// several repetitions of the same call and prints nothing about counters
// at all. Scan-to-hit must be measured separately from wall clock, which
// would confound it with the DD calls a bound provider or a
// DoubleDummyDefender makes under early cuts -- counting and timing stay
// two passes so nobody is tempted to read one run's number as the other's.
//
// Output is flat "key=value" lines, one instrument invocation per process
// (never several records interleaved) -- chosen over a human table because
// the tasks after this one are sweeps: several seeds x several sample
// sizes x several fixtures, tabulated by *something else*, not read by a
// person run by run. A per-depth vector's own length is printed explicitly
// (`sample_size_by_depth.length=N`) before its entries, rather than only
// the entries themselves -- the distinction EvaluationCounters' own
// doxygen calls out (a depth with no entry is not a depth with zero) has
// to survive the trip through this format or it is lost for good.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/exhaustive_layout_source.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/validation.hpp>

#include "fixtures.hpp"
#include "strategies.hpp"

namespace be = dds::belief_evaluation;
namespace bench = dds::belief_evaluation::benchmarks;

namespace
{
#if defined(__APPLE__)
    constexpr char const* kPlatform = "macos";
#elif defined(__linux__)
    constexpr char const* kPlatform = "linux";
#else
    constexpr char const* kPlatform = "unknown";
#endif

    // NDEBUG, not a guess at compilation_mode: -c opt is the only mode that
    // defines it, and that is the only distinction that actually matters
    // here -- see plans/08_benchmarks.md decision 4 (not cited in code
    // beyond this comment; the reasoning is what is being followed, not
    // the document). -c dbg and the default fastbuild both leave this
    // "default", which is correct: they agree on whether asserts run, the
    // only thing this label means, even though they disagree on
    // optimisation level (a caller is trusted not to benchmark in dbg).
#ifdef NDEBUG
    constexpr char const* kMode = "opt";
#else
    constexpr char const* kMode = "default";
#endif

    auto usage() -> int
    {
        std::fprintf(
            stderr,
            "Runs one benchmarks/belief_evaluation fixture through evaluate() and\n"
            "prints raw counters or timing -- never both, never a ratio.\n\n"
            "Usage: instrument --fixture NAME --history with|without --seed N [options]\n\n"
            "  --fixture NAME        pool4 pool5 pool6 pool7 pool8 realistic_a realistic_b,\n"
            "                        or finesse1 finesse2 finesse3 finesse4 (no history form)\n"
            "  --history FORM        with | without\n"
            "  --seed N              layout source seed (required)\n"
            "  --sample-size N       cap the root's own sample; absent = exhaustive\n"
            "  --scan-budget N       cap each scan's own at() calls; absent = unbounded\n"
            "  --replenish-below N   node-local replenishment threshold; absent = off\n"
            "  --mode count|time     count (default): raw counters. time: wall clock only\n"
            "  --repeat N            --mode=time only: repetitions (default 1)\n"
            "  --strategy scripted   the only strategy this instrument runs (default,\n"
            "                        and currently the only legal value)\n");
        return 2;
    }

    struct Options
    {
        std::string fixture;
        std::string history;
        std::optional<std::uint64_t> seed;
        std::optional<std::uint64_t> sample_size;
        std::optional<std::uint64_t> scan_budget;
        std::optional<std::uint64_t> replenish_below;
        std::string mode = "count";
        int repeat = 1;
        std::string strategy = "scripted";
    };

    auto parse_u64(char const* text) -> std::uint64_t
    {
        return static_cast<std::uint64_t>(std::strtoull(text, nullptr, 10));
    }

    auto parse_args(int argc, char** argv, Options& options) -> bool
    {
        for (int i = 1; i < argc; ++i)
        {
            std::string const arg = argv[i];
            auto const next = [&]() -> char const*
            {
                if (i + 1 >= argc)
                {
                    std::fprintf(stderr, "%s needs a value\n", arg.c_str());
                    std::exit(usage());
                }
                return argv[++i];
            };

            if (arg == "--fixture")
            {
                options.fixture = next();
            }
            else if (arg == "--history")
            {
                options.history = next();
            }
            else if (arg == "--seed")
            {
                options.seed = parse_u64(next());
            }
            else if (arg == "--sample-size")
            {
                options.sample_size = parse_u64(next());
            }
            else if (arg == "--scan-budget")
            {
                options.scan_budget = parse_u64(next());
            }
            else if (arg == "--replenish-below")
            {
                options.replenish_below = parse_u64(next());
            }
            else if (arg == "--mode")
            {
                options.mode = next();
            }
            else if (arg == "--repeat")
            {
                options.repeat = std::atoi(next());
            }
            else if (arg == "--strategy")
            {
                options.strategy = next();
            }
            else
            {
                std::fprintf(stderr, "unrecognised option: %s\n", arg.c_str());
                return false;
            }
        }
        if (options.fixture.empty() || options.history.empty() || ! options.seed.has_value())
        {
            std::fprintf(stderr, "--fixture, --history and --seed are all required\n");
            return false;
        }
        if (options.history != "with" && options.history != "without")
        {
            std::fprintf(stderr, "--history must be \"with\" or \"without\"\n");
            return false;
        }
        if (options.mode != "count" && options.mode != "time")
        {
            std::fprintf(stderr, "--mode must be \"count\" or \"time\"\n");
            return false;
        }
        if (options.strategy != "scripted")
        {
            std::fprintf(
                stderr, "--strategy: only \"scripted\" is implemented by this instrument today\n");
            return false;
        }
        return true;
    }

    // "finesseN" (N = 1..DDS_SUITS) names bench::make_finesse_rung(N) --
    // the genuine-uncertainty ladder, which has no with/without-history
    // pairing, so --history is accepted but has no effect for these.
    // Kept out of bench::all_rungs() itself (that list's own callers,
    // fixtures_test.cpp's HistoryFormIsStrictlySmallerThanUnconstrained
    // chief among them, assume the pairing these fixtures do not have).
    auto find_finesse_rung(std::string const& name) -> std::optional<bench::RungFixture>
    {
        if (name.rfind("finesse", 0) != 0 || name.size() != 8)
        {
            return std::nullopt;
        }
        char const digit = name[7];
        if (digit < '1' || digit > '0' + DDS_SUITS)
        {
            return std::nullopt;
        }
        return bench::make_finesse_rung(digit - '0');
    }

    auto find_fixture(std::string const& name, std::string const& history)
        -> std::optional<bench::RungFixture>
    {
        std::optional<bench::RungFixture> const finesse = find_finesse_rung(name);
        if (finesse.has_value())
        {
            return finesse;
        }
        for (bench::Rung const& rung : bench::all_rungs())
        {
            if (name == rung.name)
            {
                return history == "with" ? rung.with_history : rung.without_history;
            }
        }
        return std::nullopt;
    }

    auto root_failure_name(be::RootFailure failure) -> char const*
    {
        switch (failure)
        {
        case be::RootFailure::None:
            return "None";
        case be::RootFailure::SourceNotEnumerable:
            return "SourceNotEnumerable";
        case be::RootFailure::NoLayoutSurvived:
            return "NoLayoutSurvived";
        case be::RootFailure::ScanBudgetExhausted:
            return "ScanBudgetExhausted";
        case be::RootFailure::SampleSizeZero:
            return "SampleSizeZero";
        }
        return "Unknown";
    }

    auto callback_name(be::EvaluationCallback callback) -> char const*
    {
        switch (callback)
        {
        case be::EvaluationCallback::RootConstruction:
            return "RootConstruction";
        case be::EvaluationCallback::DeclarerPlay:
            return "DeclarerPlay";
        case be::EvaluationCallback::DefenderStrategy:
            return "DefenderStrategy";
        }
        return "Unknown";
    }

    auto print_header(Options const& options, bench::RungFixture const& fixture) -> void
    {
        std::printf("mode=%s\n", kMode);
        std::printf("platform=%s\n", kPlatform);
        std::printf("fixture=%s\n", options.fixture.c_str());
        std::printf("history=%s\n", options.history.c_str());
        std::printf("seed=%llu\n", static_cast<unsigned long long>(*options.seed));
        std::printf(
            "sample_size=%s\n",
            options.sample_size.has_value() ? std::to_string(*options.sample_size).c_str() : "none");
        std::printf(
            "scan_budget=%s\n",
            options.scan_budget.has_value() ? std::to_string(*options.scan_budget).c_str() : "none");
        std::printf(
            "replenish_below=%s\n",
            options.replenish_below.has_value() ? std::to_string(*options.replenish_below).c_str()
                                                 : "none");
        std::printf("declarer=%d\n", fixture.declarer);
        std::printf("tricks_needed=%d\n", fixture.tricks_needed);
        // The fixture's own size() for exactly this history form -- what
        // "plotted against N" (a table with N ascending) needs, and what
        // this process would otherwise have no way to report: the
        // instrument is handed a RungFixture, not the ExhaustiveLayoutSource
        // built from it, and expected_size is fixtures.hpp's own claim
        // about that source, pinned by fixtures_test.cpp -- not
        // re-derived here.
        std::printf(
            "expected_size=%llu\n", static_cast<unsigned long long>(fixture.expected_size));
    }

    auto build_options(Options const& options) -> be::EvaluateOptions
    {
        be::EvaluateOptions eval_options{};
        eval_options.sampling.sample_size = options.sample_size;
        eval_options.sampling.scan_budget = options.scan_budget;
        eval_options.sampling.replenish_below = options.replenish_below;
        return eval_options;
    }

    auto run_count_mode(Options const& options, bench::RungFixture const& fixture) -> int
    {
        be::ExhaustiveLayoutSource const source(
            fixture.root, fixture.declarer, *options.seed, fixture.history, fixture.opening_leader);

        be::EvaluateOptions eval_options = build_options(options);
        eval_options.collect_counters = true;

        be::EvaluationResult const result = be::evaluate(
            fixture.root, fixture.declarer, fixture.tricks_needed, source, bench::scripted_strategy(),
            bench::scripted_defender_play, eval_options);

        print_header(options, fixture);

        if (result.error.has_value())
        {
            be::EvaluationError const& error = *result.error;
            std::printf("error_callback=%s\n", callback_name(error.callback));
            std::printf("error_root_failure=%s\n", root_failure_name(error.root_failure));
            return 0;
        }
        std::printf("error_callback=none\n");

        be::EvaluationValue const& value = result.by_strategy.at(bench::scripted_strategy().id);
        std::printf("p_make=%.17g\n", value.p_make);

        if (! value.counters.has_value())
        {
            std::fprintf(stderr, "collect_counters was set but no counters came back\n");
            return 1;
        }
        be::EvaluationCounters const& counters = *value.counters;
        std::printf("nodes_visited=%llu\n", static_cast<unsigned long long>(counters.nodes_visited));
        std::printf(
            "tier1_made_cuts=%llu\n", static_cast<unsigned long long>(counters.tier1_made_cuts));
        std::printf(
            "tier1_dead_cuts=%llu\n", static_cast<unsigned long long>(counters.tier1_dead_cuts));
        std::printf("tier2_cuts=%llu\n", static_cast<unsigned long long>(counters.tier2_cuts));

        std::printf(
            "sample_size_by_depth.length=%zu\n", counters.sample_size_by_depth.size());
        for (std::size_t depth = 0; depth < counters.sample_size_by_depth.size(); ++depth)
        {
            be::DepthSampleStats const& stats = counters.sample_size_by_depth[depth];
            std::printf(
                "sample_size_by_depth[%zu].nodes=%llu\n", depth, static_cast<unsigned long long>(stats.nodes));
            std::printf(
                "sample_size_by_depth[%zu].layout_sum=%llu\n", depth,
                static_cast<unsigned long long>(stats.layout_sum));
            std::printf(
                "sample_size_by_depth[%zu].layout_min=%llu\n", depth,
                static_cast<unsigned long long>(stats.layout_min));
        }

        std::printf(
            "replenishment_by_depth.length=%zu\n", counters.replenishment_by_depth.size());
        for (std::size_t depth = 0; depth < counters.replenishment_by_depth.size(); ++depth)
        {
            be::DepthReplenishmentStats const& stats = counters.replenishment_by_depth[depth];
            std::printf(
                "replenishment_by_depth[%zu].attempted=%llu\n", depth,
                static_cast<unsigned long long>(stats.attempted));
            std::printf(
                "replenishment_by_depth[%zu].succeeded=%llu\n", depth,
                static_cast<unsigned long long>(stats.succeeded));
            std::printf(
                "replenishment_by_depth[%zu].layouts_added=%llu\n", depth,
                static_cast<unsigned long long>(stats.layouts_added));
            std::printf(
                "replenishment_by_depth[%zu].at_calls=%llu\n", depth,
                static_cast<unsigned long long>(stats.at_calls));
        }
        return 0;
    }

    auto run_time_mode(Options const& options, bench::RungFixture const& fixture) -> int
    {
        print_header(options, fixture);
        std::printf("repeat=%d\n", options.repeat);

        be::EvaluateOptions const eval_options = build_options(options);
        double best_ms = -1.0;
        for (int trial = 0; trial < options.repeat; ++trial)
        {
            be::ExhaustiveLayoutSource const source(
                fixture.root, fixture.declarer, *options.seed, fixture.history, fixture.opening_leader);
            auto const start = std::chrono::steady_clock::now();
            be::EvaluationResult const result = be::evaluate(
                fixture.root, fixture.declarer, fixture.tricks_needed, source, bench::scripted_strategy(),
                bench::scripted_defender_play, eval_options);
            auto const end = std::chrono::steady_clock::now();
            double const elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
            std::printf("elapsed_ms[%d]=%.6f\n", trial, elapsed_ms);
            if (result.error.has_value())
            {
                std::printf("error_callback[%d]=%s\n", trial, callback_name(result.error->callback));
            }
            if (best_ms < 0.0 || elapsed_ms < best_ms)
            {
                best_ms = elapsed_ms;
            }
        }
        std::printf("elapsed_ms.best=%.6f\n", best_ms);
        return 0;
    }
}  // namespace

auto main(int argc, char** argv) -> int
{
    Options options;
    if (! parse_args(argc, argv, options))
    {
        return usage();
    }

    std::optional<bench::RungFixture> const found = find_fixture(options.fixture, options.history);
    if (! found.has_value())
    {
        std::fprintf(stderr, "unknown --fixture: %s\n", options.fixture.c_str());
        return usage();
    }
    bench::RungFixture const& fixture = *found;

    if (options.mode == "time")
    {
        return run_time_mode(options, fixture);
    }
    return run_count_mode(options, fixture);
}
