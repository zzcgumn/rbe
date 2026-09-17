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
//
// `delta_calls` is printed in both modes: how many times this run's own
// defender strategy was called, not part of EvaluationCounters (nothing
// there counts it) so counted here by wrapping the callable itself --
// counting it alongside a timed run does not reintroduce the confound the
// count/time split above exists to avoid, since incrementing a counter
// costs nothing next to what delta itself does; only an *expensive*
// count (at_calls, which the source itself must actually perform) needs
// its own separate pass. `bound_calls` (only nonzero with --tier2) is the
// same mechanism applied to the injected LayoutBound tier2_dead() calls,
// a different and generally much smaller count on the same run.
//
// A third mode, --mode=uncut, answers a question neither of the other two
// can: how many nodes the search tree would have with tier 1 and tier 2
// both removed (see uncut_tree.hpp's own doxygen for why evaluate() itself
// cannot report this). Its own pass, not folded into --mode=count,
// because it runs a *different* recursion (uncut_tree.cpp's own walker,
// not evaluate()) over what can be a much larger tree than the cut one.
//
// --strategy scripted is solver-free and the default; --strategy
// double_dummy links the solver (DoubleDummyDefender) -- explicitly
// permitted for this instrument even though the core library must stay
// solver-free. --tier2 (only meaningful with --strategy double_dummy)
// additionally supplies a DoubleDummyBound and declares
// delta_is_double_dummy_optimal, the one configuration that can make
// tier 2 fire at all.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <api/dds_data_types.hpp>

#include <belief_evaluation/double_dummy_bound.hpp>
#include <belief_evaluation/double_dummy_defender.hpp>
#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/exhaustive_layout_source.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/validation.hpp>
#include <solver_context/solver_context.hpp>

#include "fixtures.hpp"
#include "strategies.hpp"
#include "uncut_tree.hpp"

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
    // here. -c dbg and the default fastbuild both leave this "default",
    // which is correct: they agree on whether asserts run, the only thing
    // this label means, even though they disagree on optimisation level
    // (a caller is trusted not to benchmark in dbg).
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
            "prints raw counters, timing, or an uncut node count -- never more than one\n"
            "of the three per run, never a ratio.\n\n"
            "Usage: instrument --fixture NAME --history with|without --seed N [options]\n\n"
            "  --fixture NAME        pool4 pool5 pool6 pool7 pool8 realistic_a realistic_b,\n"
            "                        or finesse1 finesse2 finesse3 finesse4 (no history form),\n"
            "                        or solver_a solver_b (solve_board-valid, for --strategy\n"
            "                        double_dummy; no history form)\n"
            "  --history FORM        with | without\n"
            "  --seed N              layout source seed (required)\n"
            "  --sample-size N       cap the root's own sample; absent = exhaustive\n"
            "  --scan-budget N       cap each scan's own at() calls; absent = unbounded\n"
            "  --replenish-below N   node-local replenishment threshold; absent = off\n"
            "  --mode count|time|uncut   count (default): raw counters. time: wall clock\n"
            "                        only. uncut: the tree size with tier 1 and tier 2\n"
            "                        both removed (see uncut_tree.hpp)\n"
            "  --repeat N            --mode=time only: repetitions (default 1)\n"
            "  --strategy scripted|double_dummy   scripted (default): solver-free,\n"
            "                        lowest-legal-card. double_dummy: DoubleDummyDefender,\n"
            "                        links the solver\n"
            "  --tier2               only with --strategy double_dummy: also supply a\n"
            "                        DoubleDummyBound and declare\n"
            "                        delta_is_double_dummy_optimal, enabling tier 2\n");
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
        bool tier2 = false;
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
            else if (arg == "--tier2")
            {
                options.tier2 = true;
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
        if (options.mode != "count" && options.mode != "time" && options.mode != "uncut")
        {
            std::fprintf(stderr, "--mode must be \"count\", \"time\" or \"uncut\"\n");
            return false;
        }
        if (options.strategy != "scripted" && options.strategy != "double_dummy")
        {
            std::fprintf(stderr, "--strategy must be \"scripted\" or \"double_dummy\"\n");
            return false;
        }
        if (options.tier2 && options.strategy != "double_dummy")
        {
            std::fprintf(stderr, "--tier2 needs --strategy double_dummy\n");
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
        if (name == "solver_a")
        {
            return bench::make_solver_rung_a();
        }
        if (name == "solver_b")
        {
            return bench::make_solver_rung_b();
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

    auto validation_error_name(be::ValidationError error) -> char const*
    {
        switch (error)
        {
        case be::ValidationError::None:
            return "None";
        case be::ValidationError::CardNotHeld:
            return "CardNotHeld";
        case be::ValidationError::CardIllegalForTrick:
            return "CardIllegalForTrick";
        case be::ValidationError::ProbabilityNonPositive:
            return "ProbabilityNonPositive";
        case be::ValidationError::ProbabilitiesDoNotSumToOne:
            return "ProbabilitiesDoNotSumToOne";
        case be::ValidationError::DistributionEmpty:
            return "DistributionEmpty";
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
        std::printf("strategy=%s\n", options.strategy.c_str());
        std::printf("tier2=%s\n", options.tier2 ? "true" : "false");
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

    // delta's own call count is not part of EvaluationCounters -- nothing
    // there distinguishes "how many times delta ran" from anything else
    // -- so this wraps the callable directly, the same way
    // test_support.hpp's CountingLayoutSource counts LayoutSource::at()
    // calls. A reference-capturing lambda, not a class with a member
    // counter: evaluate() takes `DefenderStrategy const&`
    // (`std::function`), and passing a class instance there constructs a
    // *copy* into the std::function, silently counting into that copy
    // instead of the caller's own object. Capturing `&calls` by reference
    // survives the copy (the closure is copied, the referenced counter is
    // not) and is the simpler fix besides.
    auto counting_delta(be::DefenderStrategy wrapped, std::uint64_t& calls) -> be::DefenderStrategy
    {
        // `wrapped` captured *by value* (its own std::function, not a
        // reference to this function's own parameter): the closure below
        // outlives this call, and a captured reference to a parameter
        // would dangle the moment this function returns. `calls` is
        // captured by reference deliberately -- that one does need to
        // outlive this call, to reach the caller's own counter.
        return [wrapped, &calls](be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
        {
            ++calls;
            return wrapped(query);
        };
    }

    // Same mechanism, for LayoutBound -- what tier2_dead() calls, once
    // per layout at a node until one is found live, so its own call count
    // is a different (and generally much smaller) number from delta_calls
    // even on the same run.
    auto counting_bound(be::LayoutBound wrapped, std::uint64_t& calls) -> be::LayoutBound
    {
        return [wrapped, &calls](Deal const& layout) -> int
        {
            ++calls;
            return wrapped(layout);
        };
    }

    // The solver-backed pieces a --strategy double_dummy / --tier2 run
    // needs, all owned here (not returned by value): DoubleDummyDefender's
    // and DoubleDummyBound's own DefenderStrategy/LayoutBound each capture
    // a reference to the object that made them, so everything must
    // outlive the evaluate() call it is used in.
    struct SolverBacked
    {
        // `ctx{}` here, not left for `SolverBacked backed{};` at the call
        // site to default: aggregate-init of a member with no
        // corresponding initializer copy-initializes it from `{}`, which
        // rejects SolverContext's own explicit constructor. A default
        // member initializer, direct-list-init by contrast, does not.
        SolverContext ctx{};
        std::optional<be::DoubleDummyDefender> defender;
        std::optional<be::DoubleDummyBound> bound;
    };

    // Not returned by value on purpose: SolverContext's own copy/move
    // status is not documented and not worth relying on -- every caller
    // constructs its own SolverBacked as a local and populates it via
    // this, in place.
    auto populate_solver_backed(Options const& options, bench::RungFixture const& fixture,
                                 SolverBacked& backed) -> void
    {
        if (options.strategy == "double_dummy")
        {
            backed.defender.emplace(backed.ctx);
        }
        if (options.tier2)
        {
            backed.bound.emplace(backed.ctx, fixture.declarer);
        }
    }

    auto raw_delta(Options const& options, SolverBacked& backed) -> be::DefenderStrategy
    {
        if (options.strategy == "double_dummy")
        {
            return backed.defender->as_strategy();
        }
        return bench::scripted_defender_play;
    }

    auto apply_tier2(Options const& options, SolverBacked& backed, be::EvaluateOptions& eval_options,
                      std::uint64_t& bound_calls) -> void
    {
        if (options.tier2)
        {
            eval_options.bound = counting_bound(backed.bound->as_bound(), bound_calls);
            eval_options.delta_is_double_dummy_optimal = true;
        }
    }

    auto run_count_mode(Options const& options, bench::RungFixture const& fixture) -> int
    {
        be::ExhaustiveLayoutSource const source(
            fixture.root, fixture.declarer, *options.seed, fixture.history, fixture.opening_leader);

        SolverBacked backed{};
        populate_solver_backed(options, fixture, backed);
        be::EvaluateOptions eval_options = build_options(options);
        eval_options.collect_counters = true;
        std::uint64_t bound_calls = 0;
        apply_tier2(options, backed, eval_options, bound_calls);

        std::uint64_t delta_calls = 0;
        be::EvaluationResult const result = be::evaluate(
            fixture.root, fixture.declarer, fixture.tricks_needed, source, bench::scripted_strategy(),
            counting_delta(raw_delta(options, backed), delta_calls), eval_options);

        print_header(options, fixture);
        std::printf("delta_calls=%llu\n", static_cast<unsigned long long>(delta_calls));
        std::printf("bound_calls=%llu\n", static_cast<unsigned long long>(bound_calls));

        if (result.error.has_value())
        {
            be::EvaluationError const& error = *result.error;
            std::printf("error_callback=%s\n", callback_name(error.callback));
            std::printf("error_root_failure=%s\n", root_failure_name(error.root_failure));
            std::printf("error_validation=%s\n", validation_error_name(error.validation));
            std::printf("error_seat=%d\n", error.seat);
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

        be::EvaluateOptions eval_options = build_options(options);
        double best_ms = -1.0;
        for (int trial = 0; trial < options.repeat; ++trial)
        {
            SolverBacked backed{};
            populate_solver_backed(options, fixture, backed);
            std::uint64_t bound_calls = 0;
            apply_tier2(options, backed, eval_options, bound_calls);

            be::ExhaustiveLayoutSource const source(
                fixture.root, fixture.declarer, *options.seed, fixture.history, fixture.opening_leader);
            std::uint64_t delta_calls = 0;
            auto const start = std::chrono::steady_clock::now();
            be::EvaluationResult const result = be::evaluate(
                fixture.root, fixture.declarer, fixture.tricks_needed, source, bench::scripted_strategy(),
                counting_delta(raw_delta(options, backed), delta_calls), eval_options);
            auto const end = std::chrono::steady_clock::now();
            double const elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
            std::printf("elapsed_ms[%d]=%.6f\n", trial, elapsed_ms);
            std::printf("delta_calls[%d]=%llu\n", trial, static_cast<unsigned long long>(delta_calls));
            std::printf("bound_calls[%d]=%llu\n", trial, static_cast<unsigned long long>(bound_calls));
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

    auto run_uncut_mode(Options const& options, bench::RungFixture const& fixture) -> int
    {
        print_header(options, fixture);

        SolverBacked backed{};
        populate_solver_backed(options, fixture, backed);
        be::ExhaustiveLayoutSource const source(
            fixture.root, fixture.declarer, *options.seed, fixture.history, fixture.opening_leader);
        std::optional<std::uint64_t> const uncut_nodes = bench::count_uncut_nodes(
            fixture.root, fixture.declarer, fixture.tricks_needed, source, bench::scripted_strategy(),
            raw_delta(options, backed));
        if (! uncut_nodes.has_value())
        {
            std::printf("uncut_nodes=failed\n");
            return 1;
        }
        std::printf("uncut_nodes=%llu\n", static_cast<unsigned long long>(*uncut_nodes));
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
    if (options.mode == "uncut")
    {
        return run_uncut_mode(options, fixture);
    }
    return run_count_mode(options, fixture);
}
