# Replenished Belief Evaluation: Benchmarks

This document reports measurements of the replenished-belief-evaluation
capability:

- how sampled answers relate to the exhaustive answer;
- what a search costs, and why;
- which early cuts pay for themselves;
- what a π or δ written in Python costs against C++.

**Who this is for.** You built a hand with this implementation and got a
number. Use this document to check whether the number is surprising. A wide
spread is normal at a given sample size. A narrow spread around the wrong
answer is not; "Does a sampled answer converge to the truth?" shows the
difference. Every measurement reports raw counts, not only ratios, and names
the build mode and platform that produced it.

## Method

**Build modes.** The two build modes differ only by `-DNDEBUG`. `-O3` is on
in both, on every platform (`CPPVARIABLES.bzl`), so the default build is not
an unoptimised build. `--config=opt` (`-c opt`) removes the module's 21
runtime asserts, including the mass-conservation invariants.

- Every counting measurement (all except the Python cost section) ran in
  the **default** build, so the invariants stayed live. `-c opt` would give
  the same counts, faster, with less checking.
- The one wall-clock comparison, Python against C++, reports both modes.
  Assert cost is exactly what a caller comparing the two modes needs to see.

**Machine.** Every measurement ran on Linux, on one machine, in one session.
Absolute wall-clock times do not carry to another machine. Ratios between
two numbers from the same run are more robust.

**Commands.** The sweep scripts run a C++ instrument and print tables. Build
the instrument first, then run a script from the repository root:

```
bazelisk build //benchmarks/belief_evaluation:instrument
python3 benchmarks/belief_evaluation/<script>.py
```

Each section names its script.

**Fixtures.** Every fixture belongs to one of three ladders of increasing
belief-space size (`benchmarks/belief_evaluation/fixtures.hpp`):

| ladder | rungs | `N` | what it is for |
|---|---|---|---|
| pool | pool4 to pool8 | 70 to 12,870 | defender-pool combinatorics; declarer holds a whole side suit as filler |
| realistic | realistic_a, realistic_b | 924, 12,870 | the pool combinatorics with a believable multi-suit declarer hand |
| finesse | finesse1 to finesse4 | 6 to 10,626 | replicated independent finesses: the only ladder with real per-layout uncertainty |
| solver | solver_a, solver_b | 6, 20 | small, evenly dealt positions `solve_board` accepts, for a real double-dummy defender |

Every pool and realistic rung makes with `p_make = 1` in every layout, by
construction.

## Does a sampled answer converge to the truth?

**Question.** Does the true answer sit inside the spread of sampled answers,
and how fast does the spread narrow? Where the truth sits comes first. A
wide spread is an inconvenience. A narrow spread around the *wrong* answer
is a bias, the most serious finding possible here, and nothing else in the
project can detect it.

**Command.** `python3 benchmarks/belief_evaluation/convergence.py`

**Setup.** The finesse ladder, the only one with real per-layout uncertainty.
The exhaustive answer is computed in the same run, never stored. Four
sample sizes `M` as fractions of `N`, with 15–30 seeds per cell (30 at 10%,
25 at 25%, 20 at 50%, 15 at 75%). Linux, default build.

| fixture | N | true p_make | M=10%N | M=25%N | M=50%N | M=75%N |
|---|---:|---:|---|---|---|---|
| finesse1 | 6 | 0.8333 | [0.667, 1.000] (M=3) | [0.667, 1.000] (M=3) | [0.667, 1.000] (M=3) | [0.750, 1.000] (M=4) |
| finesse2 | 66 | 0.8333 | [0.429, 1.000] | [0.688, 0.938] | [0.727, 0.909] | [0.780, 0.860] |
| finesse3 | 816 | 0.7169 | [0.634, 0.829] | [0.647, 0.784] | [0.706, 0.755] | [0.701, 0.734] |
| finesse4 | 10,626 | 0.6432 | [0.605, 0.668] | [0.623, 0.654] | [0.632, 0.648] | [0.638, 0.647] |

**Result.**

- **The true value was inside the observed range in every cell**, including
  the smallest-`M`, widest cells where a bias would show most. This checks
  in practice that the keyed-Feistel shuffle of `ExhaustiveLayoutSource` is
  unbiased with respect to layout consistency, not only by construction.
- A minimum sample size of 3 guards against a degenerate 1- or 2-layout
  draw. So finesse1's 10%, 25% and 50% columns are all `M=3`: one
  configuration measured with three seed lists, not three sample sizes.
  Only its `M=4` column is distinct. Of the 16 seed batches, 14 are
  distinct `(fixture, M)` configurations.
- **Width shrinks monotonically with `M` at every fixture**, roughly as
  `1/sqrt(M)` by eye (not fitted). finesse4 narrows from 0.063 at 10%N to
  0.009 at 75%N: sevenfold for 7.5 times the sampling. finesse1 looks flat
  across its first three columns only because they are the same `M=3`. Its
  real step, `M=3` to `M=4`, narrows from 0.333 to 0.250.
- `layout_min` (the smallest live layout count at any depth) read 1, the
  minimum possible, in every cell, sampled or exhaustive. **This is not a
  red flag.** A search narrows to one layout near the end of a resolved
  line, as expected at a leaf. One minimum across all depths cannot tell
  early collapse from that expected narrowing.

**Conclusion.** `M` around half of `N` is a reasonable choice when
exhaustive evaluation is too expensive and estimator noise should stay well
below single-digit percentage points. `SamplingOptions::sample_size` has no
default: absent means exhaustive.

The evidence: finesse3 and finesse4, the largest fixtures here, are under a
0.05 spread at `M=50%N`. `M=75%N` buys little more (0.049 to 0.033, and
0.016 to 0.009) for 50% more sampling.

**Limits.** Four fixtures of one shape (a replicated finesse), so not a
general result. It ignores sampling cost; see "What does replenishment
cost?", where a bigger `M` also costs more.

## Scan-to-hit by depth, against N

**Question.** How many `at()` calls does a node-local replenishment scan
spend per accepted layout (`at_calls / layouts_added`) as `N` grows? Does a
narrowing hint look necessary, beyond what a play history already gives?

**Command.** `python3 benchmarks/belief_evaluation/scan_to_hit.py`

**Setup.** The whole pool and realistic ladder, both history forms, 20
seeds. Linux, default build. Two sweeps.

**Result.**

| sweep | sample size | without history, depth 2 | with history |
|---|---|---|---|
| 1: fixed | `sample_size=5` | 7.2 at N=70 to 13.3 at N=12,870 | looks worse in 4 of 5 pool rungs (7.22 to 8.67 at the smallest): a confound |
| 2: scaled | 20% of `N` | 7.4 at N=70 to 17.7 at N=12,870 | better or equal in 4 of 5 pool rungs |

- **Sweep 1, fixed sample size,** is the realistic case: a caller picks one
  practical sample size whatever the position. Scan-to-hit is roughly flat
  to mildly rising. A 184-fold increase in `N` gives a 1.5–1.8-fold
  increase in scan-to-hit.
- **The with-history comparison in sweep 1 is a confound, not a finding.**
  At a fixed sample size, a small constrained space is over-sampled for its
  size. That raises collision pressure (candidates drawn and excluded),
  whatever the history does.
- **Sweep 2 removes the confound** by fixing the fraction. Scan-to-hit then
  rises clearly with `N`. With history it is better or equal in 4 of 5 pool
  rungs. Only the smallest is slightly worse, within small-integer noise.
- That smallest with-history rung (N=15) is not quite matched. A floor of 4
  layouts gives it 26.7% of `N`, not 20%. Even with that extra share it came
  out slightly behind (8.15 against 7.44), so the mismatch, if anything,
  understates the gap.
- `attempted` and `succeeded` stayed within a few percent, except at the
  smallest with-history fixture. There 35% of attempts added nothing: a
  15-layout space with a 5-layout sample empties its pool fast.

**Conclusion.** A narrowing hint does not look necessary. At a matched
sampling fraction, a play history already buys most of what a hint would,
because a history-constrained space is denser in consistent layouts, not
only smaller.

**Limits.** Seven rungs, one family of shapes. pool6 and realistic_a give
identical numbers (the same defender pool, different declarer filler), so
they are a consistency check, not two data points.

## What a play history buys at realistic positions

**Question.** How much does a play history shrink the belief space, and how
wrong is an answer that ignores it?

**Command.**

```
bazelisk build //benchmarks/belief_evaluation:ladder_reference
python3 benchmarks/belief_evaluation/space_size.py
```

**Setup.** Direct `ExhaustiveLayoutSource::size()` calls on the same deal,
with and without history. No search, no sampling, no seed. Linux.

| rung | unconstrained | constrained | ratio |
|---|---:|---:|---:|
| pool4 | 70 | 15 | 4.67x |
| pool5 | 252 | 56 | 4.50x |
| pool6 | 924 | 210 | 4.40x |
| pool7 | 3,432 | 792 | 4.33x |
| pool8 | 12,870 | 3,003 | 4.29x |
| realistic_a | 924 | 210 | 4.40x |
| realistic_b | 12,870 | 3,003 | 4.29x |

The two realistic rungs are real positions after trick one, not small
endings with a made-up history. Declarer leads a top winner and wins it, one
defender discards (showing a real void), and the history replays exactly
that.

**How wrong an unconstrained answer is.** Take `n` pool cards and a
defender holding `k`. Without the void, that defender holds a given card
with probability `k/n`. If they have shown void in a suit with `h` cards
still in the pool, the right answer is `k/(n-h)`.

| rung | k | unconstrained P(defender holds a given pool card) | constrained P | swing |
|---|---:|---:|---:|---:|
| realistic_a | 6 | 6/12 = 0.500 | 6/10 = 0.600 | +10.0pp |
| realistic_b | 8 | 8/16 = 0.500 | 8/14 = 0.571 | +7.1pp |

- The swing applies to every one of the 10 (realistic_a) or 14
  (realistic_b) outstanding non-void cards, not to one decision. The error
  is not confined to the voided suit.
- The two forced cards of the voided suit are worse. The unconstrained
  answer gives the void defender a 50% chance of each; the true chance is
  0%, because that defender has shown out.
- The ratio falls slowly towards 4, not 1, as the pool grows. A void always
  removes the same two forced cards, so its relative effect shrinks with
  pool size but never vanishes.
- Scan-to-hit also improves at these sizes (sweep 2 above, depth 2): from
  13.44 (N=924, no history) to 11.54 (N=210, with history), and from 17.69
  to 17.62 at the larger rung. Flat to better, never worse.

**Conclusion.** A play history keeps mattering at every pool size this
ladder reaches. The size ratio understates what it buys a sampling run: the
layouts a void removes are mostly ones a scan would otherwise draw and
discard as inconsistent.

## What does replenishment cost?

**Question.** What does a replenishment scan cost, especially when it
fails? Should `SamplingOptions::replenish_below` stay off by default?

**Command.** `python3 benchmarks/belief_evaluation/replenishment_cost.py`

**Cost per attempt.** Two purpose-built cases, since no existing fixture
exercises both. Linux, default build.

| case | `at_calls` per attempt | layouts added | success |
|---|---:|---:|---:|
| fails outright: finesse1, a near-exhausted source | 6.0 | 0 | 0% |
| always succeeds: pool8, fresh candidates never scarce | 76–105 | | 100% |

A failed attempt is cheaper because the scan gives up fast against an
exhausted space. It buys nothing. A larger, nearly exhausted space could
cost far more per failed attempt.

**With `replenish_below` set against without.** Three rungs of increasing
scale, three thresholds each (10%, 25% and 50% of the sample size), 10
seeds, best of 3 repeats.

| rung | sample_size | threshold | delta_calls | wall time | share of total |
|---|---:|---:|---:|---:|---:|
| finesse2 | 30 | 10% | +381% | 6.48x | 85% |
| finesse2 | 30 | 25% | +157% | 3.84x | 74% |
| finesse2 | 30 | 50% | +157% | 3.78x | 74% |
| finesse3 | 204 | 10% | +1369% | 28.22x | 96% |
| finesse3 | 204 | 25% | +401% | 8.16x | 88% |
| finesse3 | 204 | 50% | +401% | 8.35x | 88% |
| pool7 | 343 | 10% | +2194% | 124.69x | 99% |
| pool7 | 343 | 25% | +2754% | 162.15x | 99% |
| pool7 | 343 | 50% | +2878% | 161.46x | 99% |

- Each figure is the median of each seed's paired (with − without) value,
  not the difference of two medians. The two are not the same in general,
  since the median is not linear. Here they agree within a few percent on
  every row.
- "Share of total" is the with/without difference as a fraction of the
  with-run's total wall time. It is not isolated scan time: the two runs
  also visit different nodes and make different numbers of callbacks.

**Conclusion.** Keep `SamplingOptions::replenish_below` absent by default.
In every configuration replenishment dominated wall time (74–99% of the run),
and the multiplier grows sharply with scale, to 124–162x at pool7. Leaving
it absent costs nothing. Setting it without allowing for scale risks a
slowdown of two orders of magnitude.

**Limits.** Three rungs across two fixture shapes. A sweep of sample size
at a fixed `N` would separate scale from shape more cleanly.

## Cut rates: how much tier 1 removes, and whether tier 2 pays

**Question.** How much of the tree do the early cuts remove? Does tier 2 pay
for its bound calls?

**Command.** `python3 benchmarks/belief_evaluation/cut_rates.py`

**Tier 1 cut rates**, as a percentage of nodes visited. Every fixture,
Linux, default build, 10 seeds, median.

| fixture | N | made_cuts | dead_cuts |
|---|---:|---:|---:|
| pool4..8, without history | 70-12,870 | 42.0-45.7% | 0.0% |
| pool4..8, with history | 15-3,003 | 31.8-33.6% | 0.0% |
| realistic_a/b, without history | 924, 12,870 | 44.4%, 45.6% | 0.0% |
| realistic_a/b, with history | 210, 3,003 | 32.3%, 31.7% | 0.0% |
| finesse1..4 | 6-10,626 | 20.0-22.8% | 2.5-4.7% |

Dead cuts are zero across the pool and realistic ladder by construction.
Those fixtures make in every layout, so `is_dead()` has no losing line to
prune. Only the finesse ladder has lines declarer can lose.

**Tree fraction removed.** A rate per visited node understates a cut high
in the tree, which removes a whole subtree that is never visited. So this
compares against a walker with both tiers removed (`uncut_tree.hpp`).

| fixture | N | tree fraction removed |
|---|---:|---:|
| finesse1 | 6 | 0.0% |
| finesse2 | 66 | 9.1% |
| finesse3 | 816 | 16.2% |
| finesse4 | 10,626 | 23.4% |

This is valid only on the finesse and solver ladders, where it was checked.
The real evaluator revisits sibling subtrees to renormalise belief views,
and the single-pass walker cannot. On every pool and realistic rung the
walker's count came back smaller than the real tree, which is impossible
where the comparison holds. So no fraction is reported there.

**Tier 2**, on the two solver rungs, exhaustive, 10 seeds:

| fixture | N | nodes: without/with tier 2 | delta_calls: without/with | bound_calls (with) |
|---|---:|---|---|---:|
| solver_a | 6 | 25 / 5 | 14 / 6 | 10 |
| solver_b | 20 | 49 / 7 | 50 / 20 | 40 |

- Tier 2 visits 80% and 86% fewer nodes, and makes 57% and 60% fewer delta
  calls.
- Cost: roughly 18–25 µs per bound call, against roughly 80–109 µs per
  delta call. **Neither is an isolated per-call cost**; each is whole-run
  wall time divided by a call count.
  - The bound-call figure is the net wall-time difference between the two
    runs (the median of paired per-seed differences), divided by bound
    calls. It already includes tier 2's savings (fewer nodes, fewer delta
    calls), which work the other way.
  - The delta-call figure is the whole without-tier-2 run (source scanning,
    node expansion, every callback) divided by delta calls.

**Conclusion.**

- Tier 1's tree fraction removed grows with `N`, over three orders of
  magnitude on one fixture shape: independent cut opportunities compound.
- Tier 2 pays clearly on both fixtures tested. It is opt-in, through
  `EvaluateOptions::bound` and `EvaluateOptions::delta_is_double_dummy_optimal`.
- The claim in `algorithm.md` that early cuts "speed up the search
  significantly" is not contradicted. Made cuts are 20–46% of visited nodes
  everywhere measured, tier 1's tree fraction grows with `N`, and tier 2
  shows a clear net benefit on both fixtures.

**Limits.**

- Tier 1: one fixture shape (a replicated finesse), not a sweep of shape
  independent of scale. Compounding on the pool and realistic ladder cannot
  be measured with a walker built from the evaluator's public pieces, for
  the reason above.
- Tier 2: exactly one qualifying δ, a double-dummy defender with a
  double-dummy bound. That is the only pairing that meets the soundness
  obligation of the injected bound. Both fixtures are small (`N` = 6 and
  20). This is a real finding at that scale, not a claim about tier 2 at
  production scale.

## How often `TouchingSequence` and `AllOptimal` diverge

**Question.** How often do the two spread policies choose different cards?
Does the choice change the final `p_make`?

**Command.** `python3 benchmarks/belief_evaluation/divergence.py`

**Setup.** Both questions need a real double-dummy solve, so this uses the
two solver rungs. Linux, default build, three seeds. The results were
bitwise identical across seeds, as they must be on exhaustive fixtures.

**Choice divergence:** do the two policies pick different card sets at the
same node? Each policy drives the tree once. The trees differ, since a
defender node's children depend on which policy is live.

| fixture | driven by | delta_calls | divergences | rate |
|---|---|---:|---:|---:|
| solver_a | TouchingSequence | 14 | 4 | 28.6% |
| solver_a | AllOptimal | 18 | 8 | 44.4% |
| solver_b | TouchingSequence | 50 | 23 | 46.0% |
| solver_b | AllOptimal | 80 | 51 | 63.7% |

**This is not rare.** Between a quarter and two-thirds of defender calls
diverge, on two small, ordinary fixtures not built to force a tie.

**Answer divergence:** `p_make` is 0.0 on both rungs under either policy.
**That is not evidence that the choice never matters.** Both solver rungs
are positions declarer cannot make against any defence: by construction the
pool holds exactly as many defender winners as tricks needed. The zero is a
limit of these fixtures, not a finding about the policies.

**Conclusion.** The two policies build materially different search trees,
so choosing between them is a real choice, not a corner case. Whether it
ever changes which hands are makeable is open. Answering that needs a
fixture where declarer's success depends on the defence, with the equal
hand counts `solve_board` requires. The ladder has none.

## What a Python π and δ cost against C++

**Question.** How much does a π or δ written in Python cost, against the
same callback in C++?

**Command.** One run per build mode:

```
bazelisk run //benchmarks/python_cost:report -- --label default
bazelisk run --config=opt //benchmarks/python_cost:report -- --label opt
```

**Setup.** The same fixture and seed on both sides, 2000 iterations per
cell, one run per build mode, Linux. Two π crossed with two sampling
configurations:

- trivial π: the "lowest legal card" rule of `single_card_declarer_play`,
  the π of the parity test, already shown bitwise identical in both
  languages;
- real-work π: scores each legal card by the posterior-weighted count of
  defender cards that would beat it (entries × candidates work). It is
  checked in both languages against a hand-derived case;
- replenishing and non-replenishing configurations, differing only in
  `replenish_below`.

| π | config | build mode | C++ ms/iter | Python ms/iter | ratio |
|---|---|---|---:|---:|---:|
| trivial | non-replenishing | default | 0.020412 | 0.345747 | 16.94x |
| trivial | non-replenishing | opt | 0.021938 | 0.261476 | 11.92x |
| trivial | replenishing | default | 0.056528 | 0.582060 | 10.30x |
| trivial | replenishing | opt | 0.053599 | 0.479204 | 8.94x |
| real-work | non-replenishing | default | 0.019605 | 0.472462 | 24.10x |
| real-work | non-replenishing | opt | 0.017293 | 0.402853 | 23.30x |
| real-work | replenishing | default | 0.055005 | 0.866256 | 15.75x |
| real-work | replenishing | opt | 0.053723 | 0.702686 | 13.08x |

**Result.**

- **The ratio reproduces an earlier order-of-magnitude check.** Trivial π,
  replenishing: 10.30x in the default build against an earlier ~9.0x, and
  8.94x in opt against ~7.8x. Opt is lower than default in both. Absolute
  times here are 4.5–5 times slower for both languages and both modes. That
  is what different hardware does; a change of method would distort the
  ratio, not scale both sides by the same constant.
- **A Python π dominates the profile, more than the trivial ratio
  suggests.** A real-work π was expected to narrow the ratio, as a fixed
  crossing cost spreads over more work. It widens it instead, in both
  configurations and both build modes.
  - The fixture gives declarer exactly one legal card at every decision.
    So the `entries × candidates` work is `entries × 1`: a few
    multiply-adds that cost C++ nothing at this scale (real-work is at or
    below trivial on the C++ side in every cell).
  - In Python the same work is not free. Real-work costs 0.126–0.284
    ms/iteration more than trivial at a fixed configuration, which is the
    whole widening. A Python callback costs the boundary crossing plus the
    interpreter cost of what it does, and the second cost shows where C++
    shows none.
  - This holds whenever the belief view has more than a handful of entries.
    It says nothing about the candidates side, which this fixture never
    takes above 1.
- **Replenishment costs much more on the Python side, in raw
  milliseconds.** As percentages it looks the other way round: +176.9% in
  C++ (default) against +68.4% in Python. The percentages use very
  different baselines, a near-free C++ call against an expensive Python
  one. In raw terms replenishment adds 0.036 ms/iteration in C++ against
  0.236 in Python (trivial π, default): about 6.5 times as much, and about
  11 times at the real-work π.

**Conclusion.** A Python π or δ costs 9–24 times its C++ equivalent here,
and work inside a Python callback costs where the same work in C++ is free.
Replenishment puts δ on a hot inner loop, so a Python δ is expensive once
`replenish_below` is set.

## Limits: what this document could not measure

- **The ceiling of exhaustive evaluation.** Every pool and realistic rung,
  both history forms, completes an exhaustive evaluation in under 50ms on
  this platform; the largest (12,870 layouts) took 49ms (fixtures_test.cpp,
  `bazelisk test //benchmarks/belief_evaluation:fixtures_test`). That is not
  the real ceiling. Every rung here needs one trick, so cost grows roughly
  linearly with belief-space size, not with a search tree. Finding the limit
  needs a richer fixture: more tricks needed, and a strategy that varies by
  layout.
- **No defect found.** No fixture here surfaced a defect that existing tests
  do not cover. This was checked, not assumed: running new positions was the
  likeliest way to find one by accident.
- **Convergence on the pool and realistic ladder** is not measured. Those
  fixtures make with `p_make = 1` in every layout, so there is nothing to
  converge on. Extending it needs a non-degenerate strategy over that
  ladder.
- **Whether spread-policy divergence changes the final answer** is open
  (see the divergence section). It needs a fixture winnable in some layouts
  and not others, with the equal hand counts `solve_board` requires.
- **No Python δ was measured with real work.** Only π had two shapes; δ was
  the trivial rule in both languages. The replenishment finding still bears
  on a Python δ, since the replenishing contrast isolates how often δ is
  called, not what each call does.
- **Whether a bound provider and a defender on the same node defeat the
  solver's transposition-table warmth** (`similarDeal`) was not attempted.
  It needs solver-internal instrumentation beyond this capability's public
  interface.
