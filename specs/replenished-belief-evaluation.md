---
capability: replenished-belief-evaluation
owners: [belief_evaluation]
last-updated: 2026-08-22
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
the three injection contracts, and the renumbering scheme. No evaluator or
recursive search exists yet — see "Known gaps / non-goals".

## Behaviour & invariants

> Per-symbol detail lives in doxygen; these are the facts that span the whole
> capability.

- **A layout is a `Deal`** (`api/dll.h`). A belief space is a set of `Deal`
  values that share declarer's holding, dummy's holding, and the cards played
  so far, differing only in how the outstanding cards are split between the
  two defenders. Nothing in the type system enforces this; it is a contract
  the evaluator's callers and future evaluator itself must uphold.
- **Declarer strategy (`DeclarerStrategy::play`) must be a pure function of
  its arguments.** A future evaluator will walk the belief-space tree in its
  own order and may revisit sibling subtrees; a strategy that accumulates
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
  is omitted, never given zero probability — a future evaluator uses
  "probability greater than zero" as the survival test for a layout.
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
- **Evaluation is single-threaded, callbacks run on the calling thread.**
  Declarer strategy, defender strategy and any layout source are user
  callbacks; a future Python binding requires the GIL for each of them, which
  is far simpler to guarantee by never dispatching a callback off the calling
  thread. Any double-dummy calls this capability makes for its own purposes
  go through a `SolverContext` and may use that context's internal
  parallelism.

## Key entry points

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

## Known gaps / non-goals

- **No evaluator yet.** Nothing in this capability recurses over a belief
  space, samples it, or computes a probability that a contract makes. That is
  future work built on the vocabulary and contracts this spec describes.
- No sampling or replenishment, and no rescaling of sample weight.
- No early cuts.
- No search over declarer strategies — this capability evaluates one fixed
  strategy against one fixed defender strategy at a time.
- No lookup tables or equivalence-class collapsing beyond the exact gap
  removal `renumber()` performs; in particular no small-card / `least_win`
  style approximation.
- No expected-tricks variant — only the probability of making a target number
  of tricks is in scope.
- No deception-capable or partial-information defender models. The defender
  contract models perfect-information defenders only.
- No `dds_c_*` C-ABI shim entry, and so no Java/FFM, .NET, or WASM binding
  surface for this capability.
