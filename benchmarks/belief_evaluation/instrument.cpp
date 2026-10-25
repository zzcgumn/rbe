// A measuring instrument, not a test. It runs one fixture from fixtures.hpp
// through evaluate() with a chosen seed, sample size and replenishment
// threshold, and prints what happened. It is not tagged manual, so
// `bazel test //...` builds it on every platform but never runs it.
//
// One mode per run:
// - --mode=count (default) prints EvaluationCounters raw, never as a ratio.
// - --mode=time times several repetitions and prints no counters. Wall
//   clock includes the double-dummy calls a bound or a DoubleDummyDefender
//   makes, so it would confound scan-to-hit. A separate pass keeps a time
//   from being read as a count, or the reverse.
// - --mode=uncut prints the node count with tier 1 and tier 2 both removed
//   (see uncut_tree.hpp). It runs a different recursion, over a tree that
//   can be much larger, so it has its own pass.
// - --mode=divergence counts how often SpreadPolicy::TouchingSequence and
//   SpreadPolicy::AllOptimal choose differently at the same node. It needs
//   --strategy double_dummy (see counting_divergence_defender).
//
// Output is flat "key=value" lines, one record per process. The sweep
// scripts parse it; nobody reads it run by run. Each per-depth vector
// prints its length (`sample_size_by_depth.length=N`) before its entries.
// That keeps the distinction EvaluationCounters documents: a depth with no
// entry is not a depth with zero.
//
// `delta_calls`, printed in the count, time and divergence modes, counts
// calls to the defender strategy. EvaluationCounters has no such count, so
// the callable is wrapped. Counting costs nothing next to delta itself, so
// it does not confound a timed run. `bound_calls` (non-zero only with --tier2) counts
// calls to the injected LayoutBound the same way.
//
// --strategy scripted (default) is solver-free. --strategy double_dummy
// uses DoubleDummyDefender and links the solver. The instrument may do that,
// though the core library must not. With double_dummy only:
// - --tier2 also supplies a DoubleDummyBound and declares
//   delta_is_double_dummy_optimal. Tier 2 can fire only in this setup.
// - --policy picks the defender's SpreadPolicy in count and time modes. In
//   divergence mode it picks which policy's result drives the tree.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <api/dds_data_types.hpp>
#include <api/solve_board.hpp>

#include <belief_evaluation/double_dummy_bound.hpp>
#include <belief_evaluation/double_dummy_defender.hpp>
#include <belief_evaluation/evaluate.hpp>
#include <belief_evaluation/exhaustive_layout_source.hpp>
#include <belief_evaluation/node.hpp>
#include <belief_evaluation/play_record.hpp>
#include <belief_evaluation/spread.hpp>
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

    // NDEBUG, not a guess at compilation_mode. Only -c opt defines it, and it
    // decides whether asserts run, which is all this label means. -c dbg and
    // fastbuild both report "default", though their optimisation differs.
    // Nobody should benchmark in dbg.
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
            "prints raw counters, timing, an uncut node count or a divergence count.\n"
            "One mode per run, never a ratio.\n\n"
            "Usage: instrument --fixture NAME --history with|without --seed N [options]\n\n"
            "  --fixture NAME        pool4 pool5 pool6 pool7 pool8 realistic_a realistic_b,\n"
            "                        or finesse1 finesse2 finesse3 finesse4 (no history form),\n"
            "                        or solver_a solver_b (solve_board-valid, for --strategy\n"
            "                        double_dummy; no history form)\n"
            "  --history FORM        with | without\n"
            "  --seed N              layout source seed (required)\n"
            "  --sample-size N       cap the root sample; absent = exhaustive\n"
            "  --scan-budget N       cap at() calls per scan; absent = unbounded\n"
            "  --replenish-below N   node-local replenishment threshold; absent = off\n"
            "  --mode count|time|uncut|divergence   count (default): raw counters. time:\n"
            "                        wall clock only. uncut: the tree size with tier 1 and\n"
            "                        tier 2 both removed (see uncut_tree.hpp). divergence:\n"
            "                        how often SpreadPolicy::TouchingSequence and\n"
            "                        ::AllOptimal choose differently (needs --strategy\n"
            "                        double_dummy; --policy picks which one drives the\n"
            "                        tree)\n"
            "  --repeat N            --mode=time only: repetitions (default 1)\n"
            "  --strategy scripted|double_dummy   scripted (default): solver-free,\n"
            "                        lowest-legal-card. double_dummy: DoubleDummyDefender,\n"
            "                        links the solver\n"
            "  --tier2               only with --strategy double_dummy: also supply a\n"
            "                        DoubleDummyBound and declare\n"
            "                        delta_is_double_dummy_optimal, enabling tier 2\n"
            "  --policy touching|all_optimal   only with --strategy double_dummy: which\n"
            "                        SpreadPolicy (spread.hpp) the defender uses; default\n"
            "                        touching\n");
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
        std::string policy = "touching";
    };

    // `flag` names the option, for the error message. Rejects what a bare
    // strtoull() would accept:
    // - empty text;
    // - a leading '-', which strtoull negates and wraps into a huge value;
    // - a trailing non-digit. "nope" would otherwise parse as 0 and run a
    //   different measurement.
    auto parse_u64(char const* flag, char const* text) -> std::uint64_t
    {
        if (text[0] == '\0' || text[0] == '-')
        {
            std::fprintf(stderr, "%s must be a non-negative integer, got \"%s\"\n", flag, text);
            std::exit(usage());
        }
        char* end = nullptr;
        unsigned long long const value = std::strtoull(text, &end, 10);
        if (end == text || *end != '\0')
        {
            std::fprintf(stderr, "%s must be a non-negative integer, got \"%s\"\n", flag, text);
            std::exit(usage());
        }
        return static_cast<std::uint64_t>(value);
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
                options.seed = parse_u64("--seed", next());
            }
            else if (arg == "--sample-size")
            {
                options.sample_size = parse_u64("--sample-size", next());
            }
            else if (arg == "--scan-budget")
            {
                options.scan_budget = parse_u64("--scan-budget", next());
            }
            else if (arg == "--replenish-below")
            {
                options.replenish_below = parse_u64("--replenish-below", next());
            }
            else if (arg == "--mode")
            {
                options.mode = next();
            }
            else if (arg == "--repeat")
            {
                // Not std::atoi(), which stops at the first non-digit ("5junk" -> 5)
                // with no diagnostic. parse_u64 checks the whole string. repeat is an
                // int, so a value past INT_MAX is rejected here, not truncated by the
                // cast.
                std::uint64_t const value = parse_u64("--repeat", next());
                if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
                {
                    std::fprintf(stderr, "--repeat is too large\n");
                    std::exit(usage());
                }
                options.repeat = static_cast<int>(value);
            }
            else if (arg == "--strategy")
            {
                options.strategy = next();
            }
            else if (arg == "--tier2")
            {
                options.tier2 = true;
            }
            else if (arg == "--policy")
            {
                options.policy = next();
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
        if (options.mode != "count" && options.mode != "time" && options.mode != "uncut"
            && options.mode != "divergence")
        {
            std::fprintf(stderr, "--mode must be \"count\", \"time\", \"uncut\" or \"divergence\"\n");
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
        if (options.policy != "touching" && options.policy != "all_optimal")
        {
            std::fprintf(stderr, "--policy must be \"touching\" or \"all_optimal\"\n");
            return false;
        }
        if (options.mode == "divergence" && options.strategy != "double_dummy")
        {
            std::fprintf(stderr, "--mode divergence needs --strategy double_dummy\n");
            return false;
        }
        if (options.repeat < 1)
        {
            // Zero or negative would skip run_time_mode's trial loop and print the
            // -1.0 sentinel as elapsed_ms.best. A typo such as --repeat 0 must be
            // rejected, not produce an invalid record.
            std::fprintf(stderr, "--repeat must be a positive integer\n");
            return false;
        }
        return true;
    }

    // "finesseN" (N = 1..DDS_SUITS) names bench::make_finesse_rung(N). That
    // ladder has no history pairing, so --history has no effect on it. It is
    // kept out of bench::all_rungs(), whose callers (such as
    // fixtures_test.cpp's HistoryFormIsStrictlySmallerThanUnconstrained)
    // assume the pairing.
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

    // "touching" and "all_optimal" name the two SpreadPolicy values.
    // parse_args rejects anything else.
    auto to_spread_policy(std::string const& policy) -> be::SpreadPolicy
    {
        return policy == "all_optimal" ? be::SpreadPolicy::AllOptimal : be::SpreadPolicy::TouchingSequence;
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
        std::printf("policy=%s\n", options.policy.c_str());
        // The fixture's size() for this history form, which a table against N
        // needs. The instrument holds a RungFixture, not the source built from
        // it, so it prints fixtures.hpp's expected_size. fixtures_test.cpp pins
        // that value.
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

    // EvaluationCounters does not count delta calls, so this wraps the
    // callable, as test_support.hpp's CountingLayoutSource does for
    // LayoutSource::at(). It is a lambda that captures the counter by
    // reference, not a class with a member counter. evaluate() takes a
    // `std::function`, which would copy a class and count into the copy. A
    // copied closure still refers to the caller's counter.
    auto counting_delta(be::DefenderStrategy wrapped, std::uint64_t& calls) -> be::DefenderStrategy
    {
        // `wrapped` is captured by value: the closure outlives this call, and a
        // reference to the parameter would dangle. `calls` is captured by
        // reference so it reaches the caller's counter.
        return [wrapped, &calls](be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
        {
            ++calls;
            return wrapped(query);
        };
    }

    // The same wrapper for LayoutBound. tier2_dead() calls it once per layout
    // until one is live, so bound_calls is usually much smaller than
    // delta_calls on the same run.
    auto counting_bound(be::LayoutBound wrapped, std::uint64_t& calls) -> be::LayoutBound
    {
        return [wrapped, &calls](Deal const& layout) -> int
        {
            ++calls;
            return wrapped(layout);
        };
    }

    // A stable per-card key (suit * 100 + rank) for comparing WeightedCard
    // vectors as sets. WeightedCard has no operator==. Probability is left out
    // on purpose: the question is which cards each policy would play. Even
    // when the card sets match, the weights differ (one group against a union
    // of several).
    auto card_key(be::Card const& card) -> int
    {
        return card.suit * 100 + card.rank;
    }

    auto card_set(std::vector<be::WeightedCard> const& cards) -> std::set<int>
    {
        std::set<int> keys;
        for (be::WeightedCard const& wc : cards)
        {
            keys.insert(card_key(wc.card));
        }
        return keys;
    }

    // A DefenderStrategy that solves query.layout once, with the solve_board
    // call double_dummy_defender.cpp makes. From that one FutureTricks it
    // computes spread() under both SpreadPolicy values. One solve is enough:
    // TouchingSequence returns the touching group of one of the entries
    // AllOptimal unions over, so its card set is always a subset of
    // AllOptimal's (see spread.hpp). The comparison is exact.
    //
    // Counts every call into `calls`, and every call where the sets differ
    // into `divergences`. Returns `primary`'s result, so the tree is the one
    // DoubleDummyDefender(ctx, primary) would build.
    auto counting_divergence_defender(
        SolverContext& ctx, be::SpreadPolicy primary, std::uint64_t& calls, std::uint64_t& divergences)
        -> be::DefenderStrategy
    {
        return [&ctx, primary, &calls,
                &divergences](be::DefenderQuery const& query) -> std::vector<be::WeightedCard>
        {
            ++calls;
            FutureTricks fut{};
            // solutions=2, mode=0, target=-1: the call double_dummy_defender.cpp
            // makes, for the same reason. solutions=2 reports everything both
            // spread() calls need.
            int const status =
                solve_board(ctx, query.layout, /*target=*/-1, /*solutions=*/2, /*mode=*/0, &fut);
            if (status != RETURN_NO_FAULT)
            {
                return {};
            }
            std::vector<be::WeightedCard> const touching =
                be::spread(fut, be::SpreadPolicy::TouchingSequence);
            std::vector<be::WeightedCard> const all_optimal = be::spread(fut, be::SpreadPolicy::AllOptimal);
            if (card_set(touching) != card_set(all_optimal))
            {
                ++divergences;
            }
            return primary == be::SpreadPolicy::TouchingSequence ? touching : all_optimal;
        };
    }

    // The solver-backed pieces a double_dummy or --tier2 run needs. The
    // DefenderStrategy and LayoutBound these objects return refer back to the
    // object, so all of them must outlive the evaluate() call.
    struct SolverBacked
    {
        // `ctx{}` here, not left to `SolverBacked backed{};` at the call site.
        // Aggregate init copy-initializes a member that has no initializer from
        // `{}`, which SolverContext's explicit constructor rejects. A default
        // member initializer uses direct-list-init, which is allowed.
        SolverContext ctx{};
        std::optional<be::DoubleDummyDefender> defender;
        std::optional<be::DoubleDummyBound> bound;
    };

    // Fills `backed` in place rather than returning it. SolverContext's copy
    // and move behaviour is not documented, so nothing relies on it.
    auto populate_solver_backed(Options const& options, bench::RungFixture const& fixture,
                                 SolverBacked& backed) -> void
    {
        if (options.strategy == "double_dummy")
        {
            backed.defender.emplace(backed.ctx, to_spread_policy(options.policy));
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

    auto record_of(bench::RungFixture const& fixture) -> be::PlayRecord
    {
        return *be::PlayRecord::create(fixture.history, fixture.opening_leader).first;
    }

    auto run_count_mode(Options const& options, bench::RungFixture const& fixture) -> int
    {
        be::ExhaustiveLayoutSource const source(
            fixture.root, fixture.declarer, *options.seed, record_of(fixture));

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
                fixture.root, fixture.declarer, *options.seed, record_of(fixture));
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
            fixture.root, fixture.declarer, *options.seed, record_of(fixture));
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

    auto run_divergence_mode(Options const& options, bench::RungFixture const& fixture) -> int
    {
        print_header(options, fixture);

        // Only backed.ctx is used. This mode makes its own solve_board calls (see
        // counting_divergence_defender), not DoubleDummyDefender's.
        SolverBacked backed{};
        be::ExhaustiveLayoutSource const source(
            fixture.root, fixture.declarer, *options.seed, record_of(fixture));
        be::EvaluateOptions const eval_options = build_options(options);

        std::uint64_t calls = 0;
        std::uint64_t divergences = 0;
        be::EvaluationResult const result = be::evaluate(
            fixture.root, fixture.declarer, fixture.tricks_needed, source, bench::scripted_strategy(),
            counting_divergence_defender(backed.ctx, to_spread_policy(options.policy), calls, divergences),
            eval_options);

        std::printf("delta_calls=%llu\n", static_cast<unsigned long long>(calls));
        std::printf("policy_divergences=%llu\n", static_cast<unsigned long long>(divergences));
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
    if (options.mode == "divergence")
    {
        return run_divergence_mode(options, fixture);
    }
    return run_count_mode(options, fixture);
}
