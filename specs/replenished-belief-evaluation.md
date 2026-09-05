---
capability: replenished-belief-evaluation
owners: [belief_evaluation]
last-updated: 2026-09-04
---

# Replenished Belief Evaluation

> **Specs vs. doxygen.** Per-symbol signatures, parameters and return
> encodings live in the header doxygen under `library/src/belief_evaluation/`.
> This spec records the capability-wide contracts and invariants that span
> more than one symbol — not a function reference.

## Purpose

This capability evaluates, for a fixed declarer strategy played against a
fixed defender strategy, the probability that a bridge contract is made, over
a *belief space* of layouts consistent with everything observed so far. It
turns declarer play — a Partially Observable Markov Decision Process over
hidden layouts — into a search over the belief space instead, following
`docs/replenished_belief_evaluation/algorithm.md`, which this spec cites
throughout for the theory and does not restate.

This document describes the capability as it stands today: the vocabulary,
the injection contracts, the renumbering scheme, an exhaustive evaluator that
computes `P_make` exactly over a belief space small enough to enumerate in
full, and a sampling mode that draws a bounded prefix of a larger one instead.
No replenishment yet, so a sampled run is knowingly degraded — see "Behaviour
& invariants" for exactly what that means and what has and has not been done
about it. Early cuts exist — unconditional arithmetic ones that skip
subtrees contributing exactly zero, and one gated, injected-bound one — but
change no answer; see "Behaviour & invariants" for what makes each sound and
"Known gaps / non-goals" for what is still absent (a third, injected-bound-
free tier; per-layout pruning; replenishment).

## Behaviour & invariants

> Per-symbol detail lives in doxygen; these are the facts that span the whole
> capability.

- **A layout is a `Deal`** (`api/dds_data_types.hpp`). A belief space is a set of `Deal`
  values that share declarer's holding, dummy's holding, and the cards played
  so far, differing only in how the outstanding cards are split between the
  two defenders. Nothing in the type system enforces this; it is a contract
  the evaluator's callers and the evaluator itself must uphold — `make_root`
  validates it on entry rather than trusting the caller.
- **Declarer strategy (`DeclarerStrategy::play`) must be a pure function of
  its arguments.** The evaluator walks the belief-space tree in its own
  order and may revisit sibling subtrees; a strategy that accumulates
  state across calls returns different cards for the same node and corrupts
  the result. The common accidental violation is a strategy seeded from one
  PRNG stream drawn across the whole search: the card returned at a node then
  depends on how many decisions preceded it in traversal order, not on the
  node itself, which silently breaks under any future early-cut or caching
  optimisation that changes traversal order. The fix is to derive any
  randomised choice from the state directly (a hash of a seed and the state),
  never from a stream position.
- **`DeclarerStrategy::play` returns an absolute rank, never a relative
  one.** Relative ranks are only meaningful with respect to the current
  outstanding-card pool, which changes with every card played; absolute ranks
  are stable. A strategy reasoning in relative terms converts with one
  `RankMap::to_absolute` call before returning.
- **`DeclarerStrategy::state_key` has three states, not two.** Unset disables
  any future reuse of this strategy's results entirely. Set but returning an
  empty key is the strongest declaration available: "this strategy consults
  nothing beyond the position a cache would already key on", enabling maximal
  reuse. A non-empty key must not be coarser than what `play` actually
  consults — two states mapped to the same key on which `play` would diverge
  produce a wrong probability, not merely a slow one.
- **Defender strategy (`DefenderStrategy`) returns a distribution, not a
  single card**, so that randomisation between double-dummy-equivalent cards
  (restricted choice, in bridge terms) is expressible. Every returned card
  must be held by the queried seat in the queried layout and legal there;
  every probability must be strictly positive; the probabilities for one
  query must sum to one within tolerance. A card the strategy will never play
  is omitted, never given zero probability — the evaluator uses "probability
  greater than zero" as the survival test for a layout.
- **The defender strategy is independent of the declarer strategy and of the
  belief space.** It receives one layout (perfect information) and which
  seat is asking, nothing more. This independence is what licenses evaluating
  each belief-space node without reference to any other part of the search
  tree — the same property `docs/replenished_belief_evaluation/algorithm.md`
  identifies as necessary for treating each node as an independent
  sub-problem.
- **The outstanding-card pool (`aggr`) is invariant across every layout of one
  belief-space node.** Every layout at a node shares declarer's holding,
  dummy's holding and the cards played, so they share the same outstanding
  cards per suit and differ only in the two-way defender split. This is what
  lets one renumbering apply validly to a whole node rather than to a single
  layout.
- **Renumbering is exact.** Compressing a suit holding to consecutive
  positions with gaps removed, keeping only cards present in the outstanding
  pool, is a strictly order-preserving bijection within each suit. Every rule
  of trick-taking depends only on suit membership and within-suit order, so
  the game played from a renumbered position is structurally identical to the
  game played from the original: legal moves, trick winners, and trick counts
  all correspond under the bijection. Renumbering never crosses suits — suits
  are not interchangeable while a trump suit exists.
- **Rank conventions: `1` = highest, publicly; the low-packed bitmask
  internal only.** `RankMap` exposes both absolute-to-relative and
  relative-to-absolute conversion with `1` meaning the highest outstanding
  card, matching how a bridge player reads a suit. The compressed low-packed
  bitmask `renumber()` produces is an internal key-construction detail and
  never appears in a callback signature.
- **A layout identity is node-local, not global.** `layout_key()` packs one
  defender's holding exactly (52 significant bits, four 13-bit suits) and is
  unique only among layouts sharing one belief-space node's outstanding pool —
  it is not a cross-node or whole-game layout identity.
- **Declarer nodes pass weight through undivided; defender nodes partition
  it.** Declarer's and dummy's cards are public, so a declarer node neither
  filters nor reweights the belief space — its single child inherits the
  parent's layouts and per-layout probability `p` unchanged, and every
  candidate first card is evaluated against the whole belief space, not a
  share of it. Only a defender's play carries information: a defender node
  calls the defender strategy once per layout and groups the results by
  card, so `p` for a surviving layout is scaled by the probability the
  defender strategy assigned that card in that layout, and a layout the
  defender strategy gives no probability to is absent from that card's
  child rather than present with zero weight. This is the asymmetry a
  reader will not guess from the type signatures alone.
- **δ may return a multi-entry distribution, and one layout may consequently
  appear in several belief-space children with different `p`.** This is the
  mechanism by which declarer learns anything from the play at all —
  restricted choice is the canonical instance: a defender that could have
  played either of two cards reveals, by playing one, that the split is not
  what it would be had the defender held only that one. Whenever δ assigns
  more than one card positive probability for a layout, that layout survives
  into each of those cards' children, `p` multiplied by δ's respective
  probability each time; mass conservation (below) holds across the full set
  of children exactly as it does in the single-entry case.
  `docs/replenished_belief_evaluation/algorithm.md` notes this happens
  whenever the defenders have had more than one real choice, which is most
  of the time once δ is genuinely stochastic.
- **A double-dummy defender's two spread policies differ in what licenses
  them, not only in behaviour.** `SpreadPolicy::TouchingSequence` spreads
  uniformly over the canonical best card's touching-card group: playing
  either of two touching cards (a held KQ, say) leaves positions that are
  isomorphic under the renumbering bijection above, so the uniform
  distribution over that group is not a modelling choice but the canonical
  one, and the elementary `W_{π,δ}` derivation covers it unchanged.
  `SpreadPolicy::AllOptimal` spreads uniformly over every candidate achieving
  the maximum double-dummy score, across suits; those cards are equally
  *good* but not otherwise equivalent, the resulting positions are not
  isomorphic, and this is the case `algorithm.md` warns about when it says a
  heuristic defender is "not guaranteed to stay within the requirements for
  the `W_{π,δ}` based formulation" — its more general `Ω = S × B`
  construction is what covers `AllOptimal`, not the elementary derivation. A
  caller selecting `AllOptimal` has moved to that more general
  justification, which the type alone does not convey.
- **The double-dummy defender maximises tricks and is therefore not best
  defence against a specific contract.** It solves with `target = -1`
  (maximum tricks), not the contract's threshold: a threshold-aware
  double-dummy defender is indifferent across every line whenever the
  contract's fate is already double-dummy decided, so its equivalence
  classes degenerate toward uniform noise, while trick-maximising keeps a
  usable gradient at every node. The consequence is not academic — **a
  trick-maximising defender will sometimes concede the contract to hold the
  trick count down** — and is a specific instance of the gap between
  maximising tricks and minimising `P_make` that this whole capability
  exists to quantify, not a defect to apologise for.
- **The double-dummy defender is a separate build target; the core evaluator
  does not depend on the solver.** A caller supplying their own defender
  links only the core library. This also keeps the callback boundary
  (single-threaded, calling-thread-only — see below) free of the solver's
  own threading model, a constraint future work must preserve rather than
  merely a fact about today's build graph.
- **A defender node evaluates its layouts in a stable, deliberate order, and
  nothing reorders, batches or parallelises that.** Consecutive `solve_board`
  calls at one node keep a warm transposition table (`similarDeal`,
  `solver_if.cpp`) across layouts close together in a `LayoutSource`'s own
  iteration order; a future optimisation that changes that order would lose
  the benefit.
- **No double-dummy result cache yet.** The same solver results are also
  wanted by early cuts' injected bound (below), so a cache belongs somewhere
  every such caller can reach it — adding one inside this reference
  implementation now would sit inside the very thing a cut-introducing
  evaluator's correctness is measured against.
- **Mass conservation at a defender node**: summing a node's mass — `kappa`
  times the Kahan-compensated sum of `p` — over every child a defender node
  produces reproduces the parent's mass, to a stated tolerance. This is
  `docs/replenished_belief_evaluation/algorithm.md`'s
  `Σ_c wᵢ^(…,b,c) = wᵢ^(…,b)`. An invariance test of this kind does not by
  itself pin down that layouts are grouped by the right card or reweighted
  by the right factor — it holds under any reweighting that happens to
  preserve totals — so it is necessary evidence, not sufficient; the
  evaluator's own test suite pairs it with hand-derived per-child values.
- **Tricks won is common knowledge.** Every card played is observed, so
  `tricks_won_by_declarer` is identical across every layout in a node, and
  the terminal indicator `tricks_won ≥ tricks_needed` is therefore constant
  on the whole node rather than a per-layout fact. A terminal node's value
  is `node_mass` or zero, never a per-layout indicator summed over layouts —
  the two are numerically identical but only one is structurally correct,
  since a per-layout form would need a per-layout trick count that does not
  exist anywhere in the node's state.
- **`kappa` and `p` are separate quantities; `w = kappa · p` is never
  stored.** `kappa` is the node's own sample weight, `p` is per-layout, and
  collapsing them into one stored number is tempting wherever nothing yet
  rescales `kappa` mid-search — but rescaling `kappa` (replenishment) has to
  move every layout's weight in the node at once, which a single stored `w`
  per layout cannot do without being rewritten entrywise anyway. `w` is
  computed where needed and held nowhere.
- **`kappa = 1 / N` at the root of an exhaustive evaluation**, where `N` is
  the count of layouts a `LayoutSource` enumerates that are consistent with
  the root position — sharing trump, the hand on lead, the trick in
  progress, declarer's and dummy's exact holdings, and a defender split of
  the same outstanding pool — not the source's raw size, which may include
  layouts the root position rules out.
- **Sampling narrows the same root-construction scan to a bounded prefix,
  not a different mechanism.** With a sample size `M` supplied, `make_root`
  scans a `LayoutSource` from index 0, exactly as the exhaustive case does,
  and stops once `M` consistent layouts are found rather than continuing to
  the source's end; a scan budget additionally caps how many `at()` calls
  the search may spend looking, independently of `M`. `kappa` stays `1 /
  M'` — the count of layouts *actually* drawn, never the requested `M` —
  the same line the exhaustive case already has, unchanged: `κ = 1/M` would
  leave a truncated draw's mass below 1 and silently bias every answer
  downward by exactly how short the scan fell, while `κ = 1/M'` keeps mass
  at exactly 1 and makes the result the sample mean over what was actually
  drawn, which is what it is. `is_sample` is true exactly when the scan
  stopped because a cap bound — reaching `M` or the budget with the source
  not yet exhausted — never merely because a sample size was supplied: a
  requested `M` at or above however many layouts actually exist takes the
  whole consistent set before either cap can bind, and that run is
  byte-for-byte the exhaustive one. `is_sample` then propagates to every
  child through both expansion paths, so it is a fact about the whole
  subtree beneath a sampled node, not only the node itself.
- **A sampling evaluator with no replenishment is knowingly degraded, and
  that is not a defect being tracked toward a fix in this document.** A
  sample can end up containing only one layout consistent with the play so
  far, and reporting a value from that layout hands a declarer strategy a
  false certainty about a position that is genuinely still open — the
  strategy-fusion-by-the-back-door failure
  `docs/replenished_belief_evaluation/algorithm.md` warns about. `is_sample`
  and `BeliefView::space_size` (below) both exist to make a collapsed
  sample visible to a strategy that looks; neither removes the
  degradation, and no heuristic compensation for it lives anywhere in this
  capability — an evaluator that quietly compensated for a collapsed
  sample would be worse than one that is honestly degraded, since the
  compensation would be untested and unmeasured. Replenishing the sample as
  it shrinks is the fix, and is out of scope for the capability as it
  stands today (see "Known gaps / non-goals").
- **A `BeliefView`'s posterior is normalised within the node, and sample
  weight never crosses into it.** The posterior a declarer strategy is
  handed is `p_i / Σ_j p_j` over the node's current layouts — not the raw
  `p_i`, not `w_i`, and not `kappa`. `kappa` is the evaluator's own
  accounting for how a sampled node relates to the space it was drawn from;
  it is not part of what declarer would know, and handing it to the
  declarer strategy would let a strategy infer the sampling regime, which
  is exactly the kind of dependence that would make a sampled evaluator's
  results irreproducible.
- **`BeliefView::space_size` is 0 on a sampled node, using the value the
  field already documents for "unknown".** On a sampled node the true size
  of the belief space is genuinely unknown — the evaluator has seen a
  bounded prefix of a `LayoutSource`, not the whole of it — so reporting
  `entries.size()` there would be a false certainty of exactly the kind the
  sampling degradation above describes: a node down to one drawn layout
  would announce a belief space of size one. A strategy that wants the
  number of layouts it is actually reasoning over still has
  `entries.size()` from the view's own span; `space_size` on a sampled node
  adds nothing but a wrong number, so it reports nothing instead.
- **An exhaustive evaluation returns the root value and root-child values,
  and retains no tree unless asked to.** Nodes are built on the recursion
  stack and released as each subtree completes; retaining a belief set at
  every node is affordable at the small endings this evaluator targets and
  is not affordable once a real belief space is involved, so retention is
  an explicit, off-by-default opt-in. Root-child values — the value of each
  candidate action at the root — are exposed unconditionally regardless of
  the opt-in, since that is what a search layer above the evaluator
  actually consumes: for a declarer-node root, one entry per legal first
  card, each the value of committing to that card and following the given
  strategy thereafter (alternatives, not a partition — the root value
  equals whichever entry the strategy actually chose, not their sum); for a
  defender-node root, exactly the children defender-node expansion already
  produces, which do sum to the root value (mass conservation).
- **A user callback's contract violation is reported in the result, never
  thrown.** A callback is user input, not an internal, so a card or
  distribution that fails validation surfaces as an error value carrying
  which callback, which seat, and which layout — never an exception across
  the callback boundary. This keeps the evaluator usable from a `noexcept`
  context and keeps a future Python binding's job (translating the error
  into a Python exception) an explicit mapping rather than a catch-and-
  rethrow. A genuine internal invariant failure — mass conservation off by
  more than tolerance — is a different category and is not modelled this
  way.
- **Evaluation here is exhaustive**, and its cost is proportional to the
  size of the belief space it enumerates — a reference implementation and a
  small-ending tool, not something to point at a full deal.
- **Evaluation is single-threaded, callbacks run on the calling thread.**
  Declarer strategy, defender strategy and any layout source are user
  callbacks; a future Python binding requires the GIL for each of them, which
  is far simpler to guarantee by never dispatching a callback off the calling
  thread. Any double-dummy calls this capability makes for its own purposes
  go through a `SolverContext` and may use that context's internal
  parallelism.
- **Early cuts skip a subtree only when doing so is guaranteed to contribute
  exactly zero to the result, and tier 1 and tier 2 are sound for entirely
  different reasons — the asymmetry is the fact worth keeping straight, more
  than either rule on its own.** Tier 1 (a node already holding enough
  tricks; a node that cannot possibly hold enough) reads only
  `tricks_won_by_declarer`, `tricks_needed` and the outstanding-card pool —
  all common knowledge, identical across every layout a node holds,
  regardless of π, δ, or whether the node is a sample. It is sound
  unconditionally, with no gate and no caller obligation. Tier 2 (a node
  whose every layout is dead by a caller-injected double-dummy bound)
  instead concludes something from the specific layouts the node happens to
  hold, which carries two preconditions tier 1 has none of (below) — a
  future cut belongs on whichever side of this line its own soundness
  argument actually falls on.
- **Tier 2's bound is a one-directional fact, and reusing it in the other
  direction is the strategy-fusion trap early cuts have to be built around.**
  `R ≤ DD` — declarer's actual value under π is bounded by the double-dummy
  value — holds only when δ holds declarer to the double-dummy trick count
  whatever declarer does (`EvaluateOptions::delta_is_double_dummy_optimal`);
  a double-dummy solve assumes best defence, so against a δ that errs,
  declarer following π can take *more* tricks than the double-dummy value,
  and a cut on `DD < ρ` then reports zero for a contract that in fact makes.
  `DoubleDummyDefender` (either `SpreadPolicy`) satisfies the precondition —
  trick-maximising for both sides at every node — and pairing it with
  `DoubleDummyBound` is the intended sound configuration. The other
  direction is equally load-bearing and easy to miss precisely because it
  looks free once the bound is already computed: `DD ≥ ρ` implies **nothing**
  about `R`, since π may play worse than double dummy, so the single solve
  behind a bound must never be reused to conclude a contract makes. No cut
  in this capability does; a symmetric make-cut is the one addition to this
  code a future contributor is most likely to reach for and must not.
- **Early cuts are node-level, never per-layout, because per-layout pruning
  changes what a declarer strategy sees, not merely what it costs to
  compute.** `make_belief_view` normalises the posterior over the layouts a
  node currently holds; dropping one dead layout renormalises every
  survivor's posterior, and π — an arbitrary caller-supplied function of that
  view — may return a different card in response, which changes the value of
  every surviving layout too, not just the dropped one's own (zero)
  contribution. A node-level cut has no such problem: it returns before any
  view is ever built at or below the node it fires on. This is a decision
  about what a cut is allowed to touch, not an optimisation deferred for
  later — a per-layout cut would reproduce every test in this capability's
  suite bitwise (most scripted π here does not read its belief view) while
  being wrong for any π that does, which is every real one.
- **Four caller obligations cannot be validated, and are named together for
  that reason.** An injected `LayoutBound` is checkable against nothing
  short of solving the position, which is the work it exists to avoid; the
  `delta_is_double_dummy_optimal` declaration cannot be checked at all, ever.
  Both fail silently — a bound that is too high, or a declaration that does
  not hold, produces a wrong probability with no error surfaced anywhere.
  `DeclarerStrategy::state_key` (above) is the third obligation of this kind
  already in this module. The fourth: a `LayoutSource` supplied to a
  sampling evaluator must present its layouts in an order that is already
  effectively random with respect to consistency with any given root —
  "a randomised array of all possible layouts", in
  `docs/replenished_belief_evaluation/algorithm.md`'s own terms — since
  sampling takes a prefix of that order rather than drawing from it at
  random (see "Sampling narrows..." above). A source that is sorted, or
  grouped by some property correlated with consistency, yields a
  systematically biased sample with no diagnostic anywhere in this
  evaluator: nothing here can distinguish a well-shuffled source from a
  badly-ordered one, since both simply return layouts in whatever order
  `at()` presents them. A future caller-supplied contract this evaluator
  cannot verify belongs in this same register, not treated as a new kind of
  risk each time.
- **Instrumentation is reported in the result, behind an opt-in, and cannot
  change any answer.** `EvaluationCounters`, populated only when
  `EvaluateOptions::collect_counters` is set, holds facts about a run's own
  shape or cost — node count; cut counts by tier; sample size by depth
  (nodes, layout-count sum, and running minimum, each per recursion depth,
  root at 0) today; replenishment count and scan-to-hit are the known
  future additions — never a value read back into `p_make`. This is a
  constraint on every future counter this capability adds, not just a fact
  about the ones that exist today: a counters flag that perturbs the answer
  is a bug invisible to any test that does not run the same fixture both
  ways.
- **The sampling gate: tier 2 is gated on `! node.is_sample`, tier 1 is
  not, and the gate is fully engaged across the whole tree the moment a
  sample size is supplied and actually binds.** `is_sample` propagates to
  every child through both expansion paths, so once a root is a genuine
  sample, tier 2 never fires again anywhere beneath it. Tier 1's own
  soundness argument does not depend on whether a node holds the whole
  remaining space or a sample of it (common knowledge either way); tier
  2's does — "every layout this node holds is dead" is "every layout
  *drawn* is dead" on a sample, which says nothing about the true space a
  made contract might still be hiding in. Gating both cuts together,
  rather than tier 2 alone, would look like the safe choice and would in
  fact disable two cuts that were never unsound on a sample in the first
  place. This is also **stricter than**
  `docs/replenished_belief_evaluation/algorithm.md`, which forbids early
  cuts only "at a node that is at or below the replenishment floor": this
  capability has no replenishment and so no floor to gate on instead,
  which is a fact a future replenishment scheme inherits knowingly rather
  than one this document should let it discover by surprise — whether a
  replenishment floor should refine this gate is an open question this
  document does not answer. Until it is answered, a measured tier-2 cut
  rate of zero under sampling is a fact about this gate, not evidence the
  cut itself is somehow unneeded.

## Key entry points

Everything this capability declares lives in `dds::belief_evaluation`.
`api/dds_data_types.hpp` separately declares its own unrelated,
layout-identical `Card` at global scope (reached transitively through
`api/dds.h`, a thin aggregator); the two coexist by namespace, with no
rename or include-ordering trick anywhere in the module.

- `library/src/belief_evaluation/types.hpp` — `Card`, `StrategyId`,
  `StateKey`, `ObservationState`, `BeliefEntry`, `BeliefView`, `RankMap`,
  `NodeSearchInfo`, and the weight-quantity aliases.
- `library/src/belief_evaluation/declarer_strategy.hpp` — `DeclarerStrategy`.
- `library/src/belief_evaluation/defender_strategy.hpp` — `WeightedCard`,
  `DefenderQuery`, `DefenderStrategy`.
- `library/src/belief_evaluation/layout_source.hpp` — `LayoutSource`.
- `library/src/belief_evaluation/renumber.hpp` — `renumber()`.
- `library/src/belief_evaluation/rank_map.hpp` — `make_rank_map()`.
- `library/src/belief_evaluation/layout_key.hpp` — `layout_key()`.
- `library/src/belief_evaluation/validation.hpp` — `ValidationError`,
  `validate_declarer_card()`, `validate_defender_distribution()`.
- `library/src/belief_evaluation/kahan.hpp` — `KahanAccumulator`.
- `library/src/belief_evaluation/trick.hpp` — `seat_on_play()`,
  `legal_cards()`, `trick_complete_winner()`, `play()`, and the module's one
  boundary between `Deal`'s absolute-rank bit convention and the compacted
  convention `RankMap`, `renumber()` and every lookup table use.
- `library/src/belief_evaluation/node.hpp` — `BeliefNode`, `RootOptions`,
  `RootConstructionResult`, `RootFailure`, `ScanOutcome`, `make_root()`,
  `node_mass()`, `terminal_value()`, `is_terminal()`, `tricks_remaining()`.
  `make_root()`'s result carries the node and a specific failure cause
  rather than a bare `std::optional`, and (on success) a `ScanOutcome`
  recording why the scan stopped — exhausted the source, filled the
  requested sample, or ran out of budget. `tricks_remaining()` is derived
  from declarer's own holding, never the union pool a defender's
  `known_holdings` entry is — summing across all four entries double-counts
  every outstanding card.
- `library/src/belief_evaluation/belief_view.hpp` — `make_belief_view()`.
- `library/src/belief_evaluation/expand.hpp` — `expand_declarer_node()`,
  `make_declarer_children()`, `expand_defender_node()`, and their result
  types.
- `library/src/belief_evaluation/evaluate.hpp` — `evaluate()`, the public
  entry point, plus `EvaluationResult`, `EvaluationValue`,
  `EvaluationError`, `RootChildValue`, `EvaluateOptions`,
  `EvaluationCounters`, `LayoutBound`, and the cut predicates
  `already_made()`, `is_dead()`, `tier2_dead()` — exposed (not
  `evaluate.cpp`-private) for the same reason `is_terminal()` /
  `terminal_value()` are: so a test can construct an `ObservationState` /
  `BeliefNode` directly and check a cut condition in isolation, without
  going through the whole recursion.
- `library/src/belief_evaluation/spread.hpp` — `SpreadPolicy`, `spread()`.
  Part of the core library: solver-free, taking an already-solved
  `FutureTricks`.
- `library/src/belief_evaluation/double_dummy_defender.hpp` —
  `DoubleDummyDefender`. **A separate Bazel target**
  (`//library/src/belief_evaluation:double_dummy_defender`), depending on
  the solver — not part of the core library above, and not linked by
  anything that only needs the core evaluator.
- `library/src/belief_evaluation/double_dummy_bound.hpp` —
  `DoubleDummyBound`, a `LayoutBound` backed by `solve_board()`. **Also a
  separate, solver-linked Bazel target**
  (`//library/src/belief_evaluation:double_dummy_bound`), sibling to
  `double_dummy_defender` above and paired with it for the intended sound
  tier-2 configuration.

## Known gaps / non-goals

- No replenishment: a sampled node's layout count only ever shrinks as
  defenders play, and nothing tops it back up from the layouts a scan never
  reached. See "Behaviour & invariants" for what this means in practice
  (a knowingly degraded evaluator) and for what sampling itself now does
  cover (a bounded root draw with a scan budget).
- No third cut tier: no top-trick / quick-tricks analysis (`quick_tricks.cpp`
  / `later_tricks.cpp`), and no double-dummy result cache beyond whatever an
  injected `LayoutBound` implementation chooses to do internally.
- No per-layout pruning, deliberately — not a gap awaiting later work, but a
  decision: dropping a dead layout from a node renormalises the posterior
  every surviving layout's declarer strategy sees, so it changes the
  *answer*, not merely the cost, for any declarer strategy that reads its
  belief view. See "Behaviour & invariants" above.
- No search over declarer strategies — this capability evaluates one fixed
  strategy against one fixed defender strategy at a time.
- No lookup tables or equivalence-class collapsing beyond the exact gap
  removal `renumber()` performs; in particular no small-card / `least_win`
  style approximation.
- No expected-tricks variant — only the probability of making a target number
  of tricks is in scope.
- No deception-capable or partial-information defender models. The defender
  contract models perfect-information defenders only.
- No defender heuristic beyond double-dummy equivalence. Signalling,
  falsecarding, "low from three low" and similar conventions are all later
  work or out of scope entirely; the only defenders this capability ships
  are a test double and one that spreads uniformly over a double-dummy-tied
  candidate set.
- No `dds_c_*` C-ABI shim entry, and so no Java/FFM, .NET, or WASM binding
  surface for this capability.
