---
capability: replenished-belief-evaluation
owners: [belief_evaluation]
last-updated: 2026-10-21
---

# Replenished Belief Evaluation

> **Specs vs. doxygen.** Signatures, parameters and return encodings are in
> the header doxygen under `src/belief_evaluation/`. Conventions common to
> all headers are in [`docs/module_map.md`](../docs/module_map.md). This
> spec records the contracts and invariants that span more than one symbol,
> and the reasons for them. It is not a function reference.

## Purpose

This capability computes the probability that a bridge contract makes. It
plays one fixed declarer strategy against one fixed defender strategy over a
*belief space*: the layouts consistent with everything observed so far.
Declarer play is a Partially Observable Markov Decision Process over hidden
layouts. The capability turns it into a search over the belief space, as
[`algorithm.md`](../docs/replenished_belief_evaluation/algorithm.md)
describes. This spec cites that theory and does not restate it.

The capability has these parts:

- the vocabulary and the injection contracts;
- the renumbering scheme;
- an exhaustive evaluator, which computes `P_make` exactly over a belief
  space small enough to enumerate;
- a sampling mode, which draws a bounded prefix of a larger space;
- replenishment, which tops up a collapsed sample from the same source. It
  is node-local and limited by what the source can still supply;
- early cuts. Tier 1 is arithmetic and unconditional. Tier 2 uses an
  injected bound and is gated. Neither changes an answer.

"Behaviour & invariants" says what each part guarantees. "Known gaps /
non-goals" says what is absent.

## Behaviour & invariants

> Per-symbol detail is in doxygen. These facts span the whole capability.

### Layouts and strategies

- **A layout is a `Deal`** (`api/dds_data_types.hpp`). All layouts in a
  belief space share declarer's holding, dummy's holding and the cards
  played. They differ only in how the outstanding cards are split between
  the defenders. The type system does not enforce this. `make_root`
  validates it on entry.
- **`DeclarerStrategy::play` must be a pure function of its arguments.**
  The evaluator walks the tree in its own order and may revisit subtrees. A
  strategy that keeps state across calls can return different cards for the
  same node, which corrupts the result. The common mistake is one PRNG
  stream drawn across the whole search. The card at a node then depends on
  traversal order, which any future cut or cache can change. Derive a random
  choice from the state instead, for example a hash of a seed and the state.
- **`play` returns an absolute rank, never a relative one.** Relative ranks
  change with every card played. A strategy that reasons in relative ranks
  converts with one `RankMap::to_absolute` call.
- **`DeclarerStrategy::state_key` has three states, not two.**
  - Unset: no reuse of this strategy's results.
  - Set and returning an empty key: the strongest declaration. The strategy
    reads nothing beyond the position a cache already keys on, so reuse is
    maximal.
  - A non-empty key: **must not be coarser than what `play` reads.** If two
    states share a key and `play` treats them differently, the probability
    is wrong, not merely slow.
- **π may read the pre-root play record without widening the information
  barrier.** A caller-supplied `PlayRecord` reaches `play` through
  `ObservationState::play_record`. It holds every card played before the
  root, face up to every seat. That is common knowledge, like every other
  `ObservationState` field. The hidden defender split is still reachable
  only through `BeliefView`.
- **`ObservationState::history` is not everything played.** `history` is
  root-relative. It starts with the root's trick in progress (at most three
  cards) and grows only with the cards the search plays. It never holds a
  card from before the root. `play_record` is the full record from trick
  one. It does not change during an evaluation and is `nullptr` unless the
  caller supplies one. Neither is derived from the other.
- **`DefenderStrategy` returns a distribution, not a single card.** This
  lets a defender randomise between equivalent cards: restricted choice, in
  bridge terms. The rules are:
  - every card is held by the queried seat in the queried layout, and is
    legal there;
  - every probability is strictly positive;
  - the probabilities for one query sum to one, within tolerance;
  - **a card the strategy never plays is omitted, not given zero.** The
    evaluator uses "probability greater than zero" as the survival test for
    a layout.
- **δ is independent of π and of the belief space.** It receives one layout
  (perfect information) and the asking seat, nothing more. This lets the
  evaluator treat each node as an independent sub-problem, as `algorithm.md`
  requires.

### Renumbering and layout identity

- **The outstanding-card pool (`aggr`) is the same in every layout of a
  node.** The layouts share declarer's and dummy's holdings and the cards
  played. So one renumbering is valid for the whole node.
- **Renumbering is exact.** It compresses each suit to consecutive positions
  and keeps only cards in the pool. That is an order-preserving bijection
  within each suit. Trick-taking depends only on suit and order within the
  suit. So legal moves, trick winners and trick counts all correspond.
  Renumbering never crosses suits, because suits are not interchangeable
  when there are trumps.
- **Rank convention: `1` is the highest card in public; the low-packed
  bitmask is internal.** `RankMap` converts both ways, with `1` meaning the
  highest outstanding card. The bitmask `renumber()` produces builds keys
  and never appears in a callback signature.
- **A layout identity is node-local.** `layout_key()` packs one defender's
  holding exactly: 52 bits, four 13-bit suits. It is unique only among
  layouts that share one node's pool. It is not a whole-game identity.

### Weights and mass

- **Declarer nodes pass weight through undivided. Defender nodes partition
  it.** The type signatures do not show this asymmetry.
  - Declarer's and dummy's cards are public. A declarer node neither filters
    nor reweights. Its child inherits the parent's layouts and each
    layout's `p` unchanged. Each candidate first card is evaluated against
    the whole belief space, not a share of it.
  - A defender node calls δ once per layout and groups the results by card.
    A surviving layout's `p` is scaled by δ's probability for that card. A
    layout with no probability for a card is absent from that card's child,
    not present with zero weight.
- **One layout can appear in several children, with different `p`.** This
  happens whenever δ gives more than one card positive probability. It is
  how declarer learns from the play; restricted choice is the standard
  case. Mass conservation holds across all the children, as in the
  single-entry case.
- **Mass conservation at a defender node.** A node's mass is `kappa` times
  the Kahan-compensated sum of `p`. The masses of a defender node's children
  sum to the parent's mass, within a stated tolerance. This is
  `algorithm.md`'s `Σ_c wᵢ^(…,b,c) = wᵢ^(…,b)`. The check is necessary but
  not sufficient: any reweighting that preserves totals passes it. The tests
  pair it with hand-derived values per child.
- **`kappa` and `p` are separate; `w = kappa · p` is never stored.** `kappa`
  is the node's sample weight; `p` is per layout. Replenishment rescales
  `kappa`, which must move every layout's weight at once. A stored `w` per
  layout would need rewriting entry by entry. `w` is computed where needed.
- **`kappa = 1 / N` at the root of an exhaustive evaluation.** `N` counts
  the layouts from the `LayoutSource` that are consistent with the root, not
  the source's raw size. Consistent means the same trump, hand on lead,
  trick in progress, declarer's and dummy's holdings, and a defender split
  of the same pool.
- **Tricks won are common knowledge.** Every card played is seen, so
  `tricks_won_by_declarer` is the same in every layout of a node. A
  terminal node's value is `node_mass` or zero. It is never a per-layout
  indicator summed over layouts. The two give the same number, but no
  per-layout trick count exists in the node's state.

### Defenders

- **The two spread policies of a double-dummy defender differ in what
  justifies them.**
  - `SpreadPolicy::TouchingSequence` spreads uniformly over the best card's
    touching group. Playing either of two touching cards (a held KQ, say)
    gives positions that are isomorphic under renumbering. So the uniform
    spread is the canonical one, and the elementary `W_{π,δ}` derivation
    covers it.
  - `SpreadPolicy::AllOptimal` spreads uniformly over every card with the
    best double-dummy score, across suits. Those cards are equally good but
    not equivalent, and the positions are not isomorphic. `algorithm.md`
    warns that such a defender is "not guaranteed to stay within the
    requirements for the `W_{π,δ}` based formulation". Its `Ω = S × B`
    construction covers `AllOptimal`. The type does not say this.
- **The double-dummy defender maximises tricks, so it is not best defence
  against a contract.** It solves with `target = -1`. A defender that knew
  the contract would be indifferent once the contract's fate is decided
  double dummy, and its choices would degenerate to noise. Maximising
  tricks keeps a usable gradient at every node. **A trick-maximising
  defender sometimes concedes the contract to hold the trick count down.**
  This gap between maximising tricks and minimising `P_make` is what the
  capability measures. It is not a defect.
- **The double-dummy defender is a separate build target. The core
  evaluator does not depend on the solver.** A caller with their own
  defender links only the core library. This also keeps the callback
  boundary free of the solver's threading model. **Future work must keep
  it that way.**
- **A defender node evaluates its layouts in a stable order. Nothing
  reorders, batches or parallelises them.** Consecutive `solve_board` calls
  keep a warm transposition table (`similarDeal`, `solver_if.cpp`) across
  layouts that are close in the source's order. A change of order loses that
  benefit.
- **There is no double-dummy result cache.** Early cuts' injected bound
  wants the same solver results. So a cache belongs where every such caller
  can reach it, not inside this reference implementation.

### Layout sources and play history

- **`ExhaustiveLayoutSource`** (`exhaustive_layout_source.hpp`) is the
  shipped `LayoutSource`. Its belief space is every defender split of the
  outstanding pool, with trump, the trick in progress and declarer's and
  dummy's holdings fixed. With a play history it keeps only the splits
  consistent with the voids the history shows.
  - It applies exactly the narrowing the play establishes as fact. A caller
    with *inferred* information, such as what the bidding ruled out, still
    supplies their own narrower source. The line is between fact and
    inference.
  - `size()` is exact and never unknown. With no history it is `C(n, k)`:
    the pooled card count choose the fixed defender's count. With a history
    it is the same count over the cards no void forces.
  - The order is randomised from a seed fixed at construction. This meets
    the randomised-order obligation below. The same seed, root and history
    reproduce a run.
  - Two seeds enumerate the same space in different orders. Use them to
    assess sampling error at a fixed `sample_size`. Raising `sample_size`
    answers a different question.
  - A history makes the space smaller. So the same seed and root give a
    different order with a history than without one. That is correct.
- **A play history narrows the enumeration only through voids.**
  - The input is a `PlayRecord`: a `PlayTraceBin` (the type
    `ObservationState::history` and `analyse_play` use) and the opening
    leader.
  - A `PlayRecord` checks its own shape at construction: the card count,
    the opening leader's range, and no repeated card. Malformed input cannot
    construct one. It is not checked against a root there. Only
    `ExhaustiveLayoutSource` runs the root checks below.
  - `EvaluateOptions::play_record` is a separate input. It goes to
    `ObservationState::play_record` with no root check. **Keeping it
    consistent with the `LayoutSource` is the caller's obligation.**
  - Voids follow from trick arithmetic. A seat that does not follow suit,
    by discarding or ruffing, is void in that suit from then on. That
    includes the trailing, possibly incomplete, trick.
  - Voids are the only fact the root does not already hold. Played cards
    are gone from the hands, and hand sizes are in the position. Which card
    a seat followed suit with is not read.
  - Only defender voids narrow anything; declarer's and dummy's holdings
    are already exact. Their voids are still derived, for one of the checks
    below.
  - `is_consistent()` does not apply voids; the source does. It runs on
    every candidate in every scan, so changing it would change every
    existing fixture.
- **Enforcing voids is what makes vacant-spaces reasoning correct.** Take a
  pool of `n` cards with one defender holding `k`. With no other constraint,
  that defender holds a given card with probability `k/n`. Suppose they have
  shown out of a suit with `h` cards left in the pool. Those `h` cards are
  all the partner's, so the probability becomes `k / (n - h)`. Enumerating
  without voids still gives `k/n`. That is wrong for every card in the pool,
  because the denominator is wrong. **A mid-play root is sound only if every
  defender void the play has shown is applied.**
- **Without a history, a mid-play root still gets the unconstrained
  enumeration, and it is still wrong.** The history parameter is optional in
  its type only. The fix is to supply the position's history.
- **A history that does not fit its root is rejected, with a cause that
  differs from an empty root.** `verify_history` checks, in this order:
  1. The input's shape: `history.number` in `[0, 52]`; `opening_leader` and
     `declarer` in `[0, DDS_HANDS)`; each played card's suit in
     `[0, DDS_SUITS)` and rank in `[2, 14]`. These run before any value is
     used as an index.
  2. The card partition. The played cards and the cards the root's hands
     hold must be exactly the 52 cards of a deck. A duplicate, a card both
     played and held, and a card in neither are separate causes.
  3. The trailing trick. The root's `currentTrickSuit` and
     `currentTrickRank` must equal the history's trailing cards, in order.
     Count is checked first, then identity.
  4. The leader. Replaying the complete tricks from `opening_leader` must
     reach `root.first` as the leader of the trailing trick. **This check is
     needed.** Checks 2 and 3 look only at cards and order, not seats. A
     history with every seat rotated by the same amount passes both, and
     would force a suit onto the wrong defender.
  5. The declarer and dummy cross-check. A derived void in a suit the root
     shows declarer or dummy holding is a contradiction. This catches seat
     errors that are not a single rotation.

  A history that fits the root but leaves no legal split is a further,
  separate case. Its causes are: both defenders void in a suit the pool
  still holds; more cards forced to the fixed defender than it holds; or
  too few free cards for the fixed defender to reach its hand size.
  `history_verdict()` and `constrained_space_status()` report which case
  applies. `size() == 0` alone would look like an empty root, and
  `make_root` would report `RootFailure::NoLayoutSurvived`. That tells the
  caller to fix the source when the history or root is at fault.
  **A rejected history is never treated as no history.** That fallback would
  bring back the defect voids fix, in exactly the case where the caller
  tried to supply them.
- **`is_consistent()` does not compare hand sizes.** It compares trump, the
  seat on lead, the trick in progress, declarer's and dummy's holdings per
  suit, and the defender pool per suit. A candidate that gives one defender
  four cards and the other two, where the root has three each, passes. It
  would enter the belief space through `make_root()`, though it is not a
  legal position. `ExhaustiveLayoutSource` enforces the fixed defender's hand
  size itself. **A hand-built fixture, or another generator that relies on
  `is_consistent()` alone, is not protected.** This stays open on purpose.
  Tightening it changes which layouts enter every existing fixture, at the
  root and in replenishment scans. That change needs its own argument.

### Sampling and replenishment

- **Sampling is the same root scan, stopped early.** With a sample size `M`,
  `make_root` scans the `LayoutSource` from index 0, as in the exhaustive
  case. It stops after `M` consistent layouts. A scan budget also caps the
  number of `at()` calls, independently of `M`.
  - **`kappa` is `1 / M'`, where `M'` is the number actually drawn,** not
    the requested `M`. `1 / M` would leave a short draw's mass below 1 and
    bias every answer down. `1 / M'` keeps the mass at 1, so the result is
    the sample mean over what was drawn.
  - `is_sample` is true exactly when a cap stopped the scan before the
    source ran out. Supplying `M` is not enough. If `M` is at least the
    number of consistent layouts, the run is byte-for-byte the exhaustive
    one.
  - Children inherit `is_sample` as a starting value. A node's own
    replenishment scan can set it back to `false` (see below).
- **A sample can collapse, and replenishment bounds that only as far as the
  source allows.** `EvaluateOptions::sampling` groups `sample_size`,
  `scan_budget` and `replenish_below`. The doxygen of `SamplingOptions`
  gives their coupling.
  - A sample can shrink to one layout consistent with the play. Its value
    gives the declarer strategy false certainty about an open position.
    `algorithm.md` warns about this. `is_sample` and `BeliefView::space_size`
    make a collapsed sample visible to a strategy that checks.
  - With `replenish_below` set, a node with fewer layouts is topped up
    towards `sample_size` before it is evaluated further.
  - A node whose scan has exhausted the source stays as collapsed as it
    was. The false-certainty risk applies in full there.
  - With `replenish_below` absent (the default), nothing compensates for a
    collapsed sample. A hidden compensation would be untested and
    unmeasured, which is worse than honest degradation.
- **Replenishment tops a node up from the same `LayoutSource` and keeps its
  mass.**
  - It scans from index 0, the order every scan uses. It looks for layouts
    consistent with the root that match every card played on this node's
    path.
  - **It runs before any early cut at the node and before any `BeliefView`
    over it.** A cut on a node about to grow, or a view over layouts about
    to change, is exactly what this order prevents.
  - **The trigger reads only the node's layout count.** It never reads
    `is_sample`, the number of made or dead layouts, or anything else.
    `algorithm.md` allows only sample size or total mass. Anything else adds
    bias.
  - Each new layout's `p_j` comes from replaying δ at every defender ply on
    the path, as ordinary expansion does. So it does not depend on draw
    order, on when replenishment ran, or on what else the node holds.
  - `kappa` is rescaled by the node's mass before and after:
    `κ' = κ · E / E'`. Both sums are Kahan-compensated, as in `node_mass`.
    The node's mass is unchanged within floating-point tolerance. With
    nothing added, this reduces to defender-node mass conservation.
- **The result with replenishment is a sample mean, not the true
  posterior.** A replenished layout arrives below declarer decisions already
  taken without it. New layouts join only below the node that replenished,
  never a decision above it. `algorithm.md` accepts this. It stays
  tractable because a new layout's `p_j` depends only on the candidate and
  the observed history.

### What a declarer strategy sees

- **A `BeliefView`'s posterior is normalised within the node.** The
  strategy sees `p_i / Σ_j p_j` over the node's current layouts. It never
  sees the raw `p_i`, `w_i` or `kappa`. `kappa` is the evaluator's
  bookkeeping for sampling, not something declarer would know. A strategy
  that could read it could depend on the sampling regime, and the results
  would not reproduce.
- **`BeliefView::space_size` is 0 on a sampled node**, the value the field
  uses for "unknown". There the true size is unknown. Reporting
  `entries.size()` instead would be false certainty: a node down to one
  layout would claim a belief space of one. A strategy can still read
  `entries.size()` for the number of layouts in its view.

### Results and errors

- **An evaluation returns the root value and the root-child values. It keeps
  no tree unless asked.** Nodes live on the recursion stack and are released
  when their subtree completes. Keeping every node is affordable only for
  small endings, so `retain_root` is an opt-in.
- **Root-child values are always returned**, because a search layer above
  the evaluator needs them.
  - Declarer or dummy on lead: one entry per legal first card. Each is the
    value of that card followed by the strategy. These are alternatives:
    the root value equals the entry the strategy chose, not their sum.
  - A defender on lead: the defender node's children. These sum to the root
    value.
- **The result says which of the two it is.** A consumer cannot tell from
  the values. "Declarer root" includes dummy on lead, which the obvious
  reading gets wrong. So the result carries a flag next to the values. Two
  differently named keys would break every existing consumer; a consumer
  that ignores the flag keeps working.
- **A callback's contract violation is returned in the result, never
  thrown.** A callback is user input. A card or distribution that fails
  validation becomes an error value naming the callback, the seat and the
  layout. No exception crosses the callback boundary. So the evaluator works
  from a `noexcept` context, and the Python binding maps the error to an
  exception explicitly. An internal invariant failure, such as mass
  conservation out of tolerance, is a different category and is not
  reported this way.
- **Instrumentation is opt-in and cannot change an answer.**
  `EvaluationCounters` is filled only when `collect_counters` is set. It
  holds facts about a run's shape and cost:
  - nodes visited;
  - cuts by tier;
  - sample size by depth: nodes, layout-count sum and minimum, root at 0;
  - replenishment by depth: scans attempted, scans that added a layout,
    layouts added, and `source.at()` calls. All zero at the root, which is
    never replenished.

  No counter is ever read back into `p_make`. **This binds every future
  counter.** A counter that changes the answer is a bug that only a test
  running the fixture both ways would catch.

### Cost and threading

- **Exhaustive evaluation costs in proportion to the belief space.** It is a
  reference implementation and a tool for small endings, not for a full
  deal.
- **Evaluation is single-threaded, and callbacks run on the calling
  thread.** Declarer strategy, defender strategy and layout source are user
  callbacks. The Python binding needs the GIL for each, which is simplest
  if no callback leaves the calling thread. Double-dummy calls the
  capability makes for itself go through a `SolverContext` and may use its
  internal parallelism.

### Early cuts

- **An early cut skips a subtree only when it contributes exactly zero.
  Tier 1 and tier 2 are sound for different reasons.**
  - Tier 1 fires when a node already has enough tricks, or cannot reach
    enough. It reads only `tricks_won_by_declarer`, `tricks_needed` and the
    pool. These are common knowledge and the same in every layout. So tier
    1 is sound for any π, δ or sample, with no gate and no caller
    obligation.
  - Tier 2 fires when every layout at a node is dead by a caller-injected
    double-dummy bound. It reasons from the layouts the node holds, so it
    has two preconditions (below).
  - A future cut goes on whichever side its soundness argument puts it.
- **Tier 2's bound works in one direction only.**
  - `R ≤ DD` (declarer's value under π is at most the double-dummy value)
    holds only if δ holds declarer to the double-dummy trick count whatever
    declarer does. The caller declares this with
    `EvaluateOptions::delta_is_double_dummy_optimal`. Against a δ that errs,
    declarer can take more tricks than double dummy. A cut on `DD < ρ` then
    reports zero for a contract that makes.
  - `DoubleDummyDefender`, with either `SpreadPolicy`, meets the
    precondition. Pair it with `DoubleDummyBound`.
  - **`DD ≥ ρ` implies nothing about `R`**, because π may play worse than
    double dummy. A bound's solve must never be reused to conclude that a
    contract makes. No cut here does. **Do not add a make cut.** It looks
    free once the bound is computed, which is why it is tempting.
- **Early cuts are node-level, never per-layout.** `make_belief_view`
  normalises the posterior over a node's layouts. Dropping one dead layout
  changes every survivor's posterior. π may then return a different card,
  which changes the value of every surviving layout. A node-level cut
  returns before any view is built at or below the node. A per-layout cut
  would pass every test here, because most scripted π ignore their view. It
  would be wrong for any π that reads it.
- **The sampling gate: tier 2 is gated on `! node.is_sample`; tier 1 is
  not.** Tier 1's argument holds on a sample. Tier 2's does not: on a sample,
  "every layout is dead" means only "every layout drawn is dead". Gating
  both would disable cuts that are sound on a sample.
- **Decision: the gate is not refined to the replenishment floor.**
  `algorithm.md` allows early cuts once a node reaches such a floor.
  Re-enabling tier 2 there would buy a cut of unproven value: a bound call
  costs several defender-node calls and saves no later solve. The price is
  a second soundness argument to maintain as the floor moves. Cuts run
  *after* replenishment at a node. That meets the intent of `algorithm.md`'s
  floor rule: no cut on a node that replenishment could still top up. **The
  gate stays exactly `! node.is_sample`, stricter than `algorithm.md`
  requires.**
- **A node whose own replenishment scan exhausts the source has
  `is_sample == false`, so tier 2 fires there.** The node holds all of its
  remaining belief space (see the doxygen of `BeliefNode::is_sample`). This
  is the gate working as documented, not a floor exception. So `is_sample`
  is not monotone down the tree: a child can be more complete than its
  sampled parent. A non-zero tier-2 count under sampling is this case.

### Caller obligations the evaluator cannot check

- **Four caller obligations cannot be validated. They are listed together
  for that reason.**
  1. **An injected `LayoutBound`.** Checking it means solving the position,
     which is the work it saves. A bound that is too low fires
     `tier2_dead()` on a layout declarer would make, so a making contract
     scores zero. A bound that is too high only loses a prune.
  2. **The `delta_is_double_dummy_optimal` declaration.** It cannot be
     checked at all. If it is false, the probability is wrong, with no
     error.
  3. **`DeclarerStrategy::state_key`** (above).
  4. **The order of a `LayoutSource` given to a sampling evaluator must be
     effectively random** with respect to consistency with any root:
     "a randomised array of all possible layouts", in `algorithm.md`.
     Sampling takes a prefix of that order. A sorted source, or one grouped
     by a property linked to consistency, gives a biased sample. Nothing
     here can detect that. `ExhaustiveLayoutSource` meets this obligation,
     but it still binds every other source.

  A future caller contract the evaluator cannot verify belongs in this list.

## Key entry points

Everything this capability declares is in `dds::belief_evaluation`.
`api/dds_data_types.hpp` declares an unrelated `Card` with the same layout
at global scope, reached through `api/dds.h`. The two coexist by namespace,
with no renaming or include-order trick.

- `src/belief_evaluation/types.hpp`: `Card`, `StrategyId`, `StateKey`,
  `ObservationState`, `BeliefEntry`, `BeliefView`, `RankMap`,
  `NodeSearchInfo`, and the weight aliases.
- `src/belief_evaluation/declarer_strategy.hpp`: `DeclarerStrategy`.
- `src/belief_evaluation/defender_strategy.hpp`: `WeightedCard`,
  `DefenderQuery`, `DefenderStrategy`.
- `src/belief_evaluation/layout_source.hpp`: `LayoutSource`.
- `src/belief_evaluation/defender_split.hpp`: `DefenderPool`,
  `defender_pool_decomposition()`, `binomial_coefficient()`,
  `unrank_combination()`, `apply_defender_split()`. The combinatorics
  `ExhaustiveLayoutSource` is built from: the pool and each defender's count,
  the k-subset enumeration, and applying a subset to a root as a split.
- `src/belief_evaluation/keyed_permutation.hpp`: `keyed_permutation()`, a
  seeded bijection on `[0, N)` that stores nothing. General-purpose.
  `ExhaustiveLayoutSource` uses it to randomise its order.
- `src/belief_evaluation/void_derivation.hpp`: `VoidsBySeat`,
  `derive_voids()`. From a play history to every seat's voids, by trick
  arithmetic alone.
- `src/belief_evaluation/history_verification.hpp`: `HistoryVerdict`,
  `verify_history()`. Whether a history belongs to a root, and the cause
  when it does not.
- `src/belief_evaluation/constrained_decomposition.hpp`:
  `ConstrainedSpaceStatus`, `ConstrainedDecomposition`,
  `decompose_constrained()`, `constrained_space_size()`. Splits a
  `DefenderPool` by per-suit voids into cards forced to the fixed defender,
  cards forced away, and free cards. With no voids it gives the same count
  as `defender_pool_decomposition`, through the same
  `binomial_coefficient()` call.
- `src/belief_evaluation/exhaustive_layout_source.hpp`:
  `ExhaustiveLayoutSource`, the shipped `LayoutSource` (see "Layout sources
  and play history").
- `src/belief_evaluation/renumber.hpp`: `renumber()`.
- `src/belief_evaluation/rank_map.hpp`: `make_rank_map()`.
- `src/belief_evaluation/layout_key.hpp`: `layout_key()`.
- `src/belief_evaluation/validation.hpp`: `ValidationError`,
  `validate_declarer_card()`, `validate_defender_distribution()`.
- `src/belief_evaluation/kahan.hpp`: `KahanAccumulator`.
- `src/belief_evaluation/trick.hpp`: `seat_on_play()`, `legal_cards()`,
  `enumerate_legal_cards()` (the `Card` list form of `legal_cards()`'s
  per-suit bitmasks), `trick_complete_winner()` and `play()`. This is the
  module's one boundary between `Deal`'s absolute-rank bit convention and
  the compacted convention of `RankMap`, `renumber()` and the lookup tables.
  - Python binds `bsle.seat_on_play`, `bsle.play` and
    `bsle.trick_complete_winner`. `bsle.legal_cards` binds
    `enumerate_legal_cards()`, since a strategy must return a `Card`.
    `bsle.play_out()` composes them to replay a history.
  - There is no new logic: these are the functions the evaluator calls. A
    Python reimplementation that disagreed on follow-suit or trick winners
    would build a wrong root, silently.
- `src/belief_evaluation/node.hpp`: `BeliefNode`, `RootOptions`,
  `RootConstructionResult`, `RootFailure`, `ScanOutcome`, `make_root()`,
  `root_observation_state()`, `history_for()`, `is_consistent()`,
  `node_mass()`, `terminal_value()`, `is_terminal()`, `tricks_remaining()`.
  - `make_root()` returns a specific failure cause, not a bare
    `std::optional`. On success it also returns a `ScanOutcome`: the scan
    exhausted the source, filled the sample, or ran out of budget.
  - `tricks_remaining()` reads declarer's holding. **Never derive it from a
    defender's `known_holdings` entry**, which is the union pool: summing
    all four entries counts every outstanding card twice.
  - `root_observation_state()`, `history_for()` and `is_consistent()` are
    what `make_root()` uses. They are public so replenishment uses the same
    logic, not a copy.
- `src/belief_evaluation/replenishment.hpp`: `replay_candidate()`,
  `ReplayResult`, `scan_for_replenishment()`, `ScanCandidate`, `ScanResult`.
  `replay_candidate()` plays a root candidate along a node's path and
  accumulates `p_j` through δ at each defender ply.
  `scan_for_replenishment()` scans for candidates the node does not hold. It
  uses the same source order and `at()` budget as `RootOptions::scan_budget`.
  Solver-free, in the core library like every header above.
- `src/belief_evaluation/belief_view.hpp`: `make_belief_view()`.
- `src/belief_evaluation/expand.hpp`: `expand_declarer_node()`,
  `make_declarer_children()`, `expand_defender_node()`, their result types,
  and `advance_state()`. Both expansion paths use `advance_state()`. It is
  public so `replay_candidate()` rebuilds the same states, not a copy.
- `src/belief_evaluation/evaluate.hpp`: `evaluate()`, the public entry
  point, with `EvaluationResult`, `EvaluationValue`, `EvaluationError`,
  `RootChildValue`, `EvaluateOptions`, `SamplingOptions`,
  `EvaluationCounters` and `LayoutBound`.
  - The cut predicates `already_made()`, `is_dead()` and `tier2_dead()` are
    public, like `is_terminal()` and `terminal_value()`. A test can check a
    cut on a hand-built node without the recursion.
  - `EvaluateOptions` has six fields, all off by default: `retain_root`,
    `collect_counters`, `bound`, `delta_is_double_dummy_optimal`,
    `sampling` and `play_record`. A default `SamplingOptions` means
    exhaustive enumeration, no scan budget and no replenishment.
- `src/belief_evaluation/spread.hpp`: `SpreadPolicy`, `spread()`. Core
  library and solver-free: it takes an already-solved `FutureTricks`.
- `src/belief_evaluation/double_dummy_defender.hpp`: `DoubleDummyDefender`.
  **A separate Bazel target** (`//src/belief_evaluation:double_dummy_defender`)
  that depends on the solver. Nothing that needs only the core evaluator
  links it.
- `src/belief_evaluation/double_dummy_bound.hpp`: `DoubleDummyBound`, a
  `LayoutBound` backed by `solve_board()`. **Also a separate solver-linked
  target** (`//src/belief_evaluation:double_dummy_bound`). Pair it with
  `DoubleDummyDefender` for a sound tier 2.
- `src/belief_evaluation/defender_heuristic.hpp`,
  `defender_heuristic_chain.hpp`, `second_seat_low.hpp`,
  `high_in_third.hpp`, `third_seat_low.hpp`, `fourth_seat_low.hpp`,
  `ruff_small.hpp`, `suit_top_tricks.hpp`, `discard_keep_winners.hpp`,
  `default_defender_heuristics.hpp`:
  - `DefenderHeuristic` is a plain callable, not an interface. A caller's
    rule and a built-in rule are the same kind of value.
  - `DefenderHeuristicContext`.
  - `DefenderHeuristicChain`. `add()` is the only way to build a chain: no
    denomination argument, and no enum of built-ins.
  - The six rules, as factory functions.
  - `make_default_defender_heuristics()`, a convenience builder. Nothing
    that consumes a chain treats it specially.
  - Solver-free: every rule reads an already-solved candidate set. So this
    is its own `cc_library` (`:defender_heuristic`), a sibling of
    `:belief_evaluation`.
- `src/belief_evaluation/heuristic_defender.hpp`: `HeuristicDefender`, a
  second `DefenderStrategy`. It runs a caller-assembled
  `DefenderHeuristicChain` and falls back to `spread()` when every rule
  defers.
  - **A separate solver-linked target**
    (`//src/belief_evaluation:heuristic_defender`), like
    `double_dummy_defender`. It does not replace it; either works as
    `evaluate()`'s `delta`.
  - Not best defence against a contract, for the same reason as
    `DoubleDummyDefender`. It only offers cards from the double-dummy-optimal
    set a `solve_board` call produces. Neither a rule nor `spread()` adds a
    card outside that set.
  - `second_seat_low`'s `randomize_touching_honors` controls restricted
    choice, as `SpreadPolicy::TouchingSequence` does. On, the rule defers on
    a touching pair, so the fallback `spread()` randomises between them.
    Off, it always plays the lower card. That is the one way to model a
    defender who leaks which card was held. It reuses `SpreadPolicy`'s
    justification.
  - **This guarantee depends on chain position.** It holds only if no later
    rule in the chain fires where `second_seat_low` defers. No rule can know
    what a later, caller-supplied rule will do, so this is a caller
    obligation. See the doxygen of `second_seat_low.hpp`, and
    `DefenderHeuristicChainTest.ALaterRuleAtTheSameDecisionPointDefeatsSecondSeatLowsDeferral`
    for the behaviour when it is violated.
- `src/belief_evaluation/brute_force_declarer.hpp`: `BruteForceDeclarer`, a
  lookahead `DeclarerStrategy`.
  - At a declarer or dummy node it tries every legal card and keeps the best
    under a fixed `DeclarerObjective`: expected tricks, or probability to
    make. At a defender node it groups and sums each surviving layout's
    response from an internal `opponent_model`. That defaults to an owned
    `DoubleDummyDefender`.
  - **A caller-supplied `opponent_model` must answer from the position
    alone, never from `ObservationState::history` or `play_record`.**
    `DefenderQuery::state` exposes both, and a `DefenderStrategy` such as
    `ScriptedDefender` may read them. But the cache key
    (`BruteForceCacheKey`) omits both fields. That lets two `play()` calls
    on one line share a cached value, and is sound only if the model cannot
    tell the calls apart. The precondition is not checked at run time (see
    the class doxygen).
  - To `evaluate()` it is one ordinary `DeclarerStrategy`. The search is
    private to each `play()` call, and `evaluate()` does not know it
    happened.
  - The cache belongs to the instance, so two differently configured
    instances never share it. Without `max_depth`, calls share it. A hit across calls is
    rare: each call's root is renormalised to 1, but the inner recursion
    never renormalises, so only a chain of probability-1 choices matches.
    Reuse within one call is unaffected. With `max_depth`, the cutoff
    depends on depth within the call, so each call starts with a clean
    cache.
  - The result stands in for "the double-dummy-optimal declarer" only when
    `delta` matches its `opponent_model`. With any other `delta`,
    `evaluate()` still returns a well-defined `P_make` for that pairing.
  - **A separate solver-linked target**
    (`//src/belief_evaluation:brute_force_declarer`), like
    `double_dummy_defender` and `heuristic_defender`. Its cache key,
    `brute_force_cache.hpp`, is a solver-free sibling target, for the same
    reason `defender_heuristic` is separate from `heuristic_defender`.

## Known gaps / non-goals

- `is_consistent()` does not compare defender hand sizes, so it accepts some
  candidates that are not legal positions. `ExhaustiveLayoutSource` enforces
  sizes itself. Left open on purpose; see "Layout sources and play history".
- Replenishment rescans from the front of the source. It does not enumerate
  the part of the space not yet sampled, which would also allow using the
  whole remainder when it is small.
- No third cut tier: no top-trick or quick-tricks analysis
  (`quick_tricks.cpp`, `later_tricks.cpp`). No double-dummy result cache,
  beyond what an injected `LayoutBound` does internally.
- No per-layout pruning. This is a decision, not pending work: it changes
  the answer for any declarer strategy that reads its view. See "Early cuts".
- No search over declarer strategies. The capability evaluates one fixed
  strategy against one fixed defender strategy. `BruteForceDeclarer` does
  not change this: its lookahead is private, and `evaluate()` still calls
  `play()` once per node.
- No lookup tables or equivalence classes beyond the exact gap removal of
  `renumber()`. In particular, no small-card (`least_win`) approximation.
- No expected-tricks variant of the evaluator. `p_make` is always a
  probability. `BruteForceDeclarer`'s `DeclarerObjective::MaximizeExpectedTricks`
  is only the objective its internal search uses to choose a card.
- No deception-capable or partial-information defenders. The defender
  contract models perfect-information defenders only.
- No signalling or falsecarding. `HeuristicDefender`'s conventions
  (second, third and fourth hand low, high in third, ruff small, discard
  keeping winners) still choose among double-dummy-optimal candidates. None
  reasons about what partner or declarer can infer from the choice.
- No `dds_c_*` C-ABI entry, so no Java/FFM, .NET or WASM binding for this
  capability.
