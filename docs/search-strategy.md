# Search Strategy Contract

Status: implementation notes for the public API in `include/qalsh/qalsh.h`. The consolidated [specification](spec.md) remains authoritative for scope and acceptance, including actual consumer migration branches and accuracy/performance gates. This document does not establish that those migrations or gates have passed.

The architecture in [spec.md](spec.md) is agreed in principle. This document proposes how to express it without growing a collection of consumer-specific flags and callbacks. The user prioritizes easy external strategy integration over approving every low-level interface detail: H must fit as a new strategy without H-specific public APIs or core-engine changes. The operation-based sketch remains a design candidate, not a mandate to expose a complicated state machine to every caller.

## One execution driver, one strategy decision boundary

The public search operation runs the library's execution loop. A strategy owns algorithm decisions and its private per-query state, but does not implement index access, distance kernels, deduplication, or top-k maintenance.

Conceptually:

1. The driver presents the completed operation's observation and a read-only query view to the strategy.
2. The strategy updates its private state and requests the next operation.
3. The driver validates and executes that request, updates library-owned state, and repeats.

These are synchronous, in-process interactions, not asynchronous messages or a plugin/scripting runtime. The implementation uses one projection-hit callback and value operations; per-hit heap allocations are not implied by this model.

Ordinary users use the default QALSH strategy and optionally replace its candidate or termination rule. Advanced users replace the strategy while using the same driver. They do not have to implement the ordinary strategy's orchestration from scratch merely to change a candidate formula.

## Proposed operation families

| Request | Library responsibility | Result available to strategy |
| --- | --- | --- |
| Continue projection scanning | Advance resumable traversal using the selected index backend | Projection hit or an explicit traversal boundary/exhaustion observation |
| Evaluate a point ID | Validate ID, deduplicate, invoke point access, calculate bounded distance, update top-k | Evaluation outcome and updated result view |
| Advance radius | Apply configured geometric growth with numerical validation; retain traversal position | Updated radius and corresponding search window |
| Finish | Finalize available results and report the termination reason | Query return |

The exact scan request operands are deliberately not fixed here. Table selection, traversal order and incremental scan boundaries must be described before claiming that both consumers fit. End of a scan unit, end of the current radius window, and exhaustion of all projection entries are different facts. In the implementation, `ScanReport::round_complete` identifies the current-window boundary; `all_tables_exhausted` is reserved for permanent cursor exhaustion.

A completed evaluation is visible before the strategy is asked what to do next. Distance computation remains immediate per accepted candidate, not H-style batch sorting. Immediate evaluation does not force the strategy to check termination immediately. The built-in strategy can mark a query finished at its configured evaluation or scan boundary; the current scan still reaches its requested logical boundary, preserving cursor progression.

An ID submitted for evaluation need not be the most recent projection hit. It may come from strategy-owned deferred state. All such evaluations use the same validity checks and deduplication path.

## Proposed ownership boundary

The library owns index resources, traversal cursors, the query's radius, evaluation-deduplication state, and top-k results. A read-only view can expose the current radius, results, evaluation count, and documented traversal facts. A projection hit must expose its point ID, table identity, and projection-difference information needed by the candidate rule. Exact payloads and numeric types remain open.

The strategy owns collision counts, likelihood statistics, deferred candidate
queues, and decisions about progression or termination. The library's
`TableScanSchedule` owns ordinary projection-table queueing, logical window
completion, and cursor exhaustion; an external strategy can reuse it rather
than copying that orchestration. Strategies request operations rather than
mutating library cursors or result containers directly. The standard
`QalshParameters::candidate_budget` participates in the default strategy only;
setting a complete termination rule replaces the standard budget and distance
predicate, while `set_candidate_budget(std::nullopt)` explicitly disables the
default budget.

Evaluation outcomes must distinguish a true distance from a computation pruned above the current bound, as well as duplicate submissions. A partial accumulated distance must not masquerade as an exact distance. `EvaluationEvent` distinguishes evaluated, duplicate, and pruned outcomes; exact distances and deterministic tie handling remain engine-owned.

## Walkthroughs (design reasoning, not executed tests)

### Default QALSH strategy

- Request projection scanning and accumulate collision evidence.
- When the default candidate rule accepts a point, request its evaluation.
- Apply the default termination predicate at the default strategy's documented check boundaries.
- Advance the radius or finish as appropriate.

The algorithm reference for the default is Huang et al., *Query-Aware
Locality-Sensitive Hashing for Approximate Nearest Neighbor Search*, PVLDB
2015 (Algorithm 1 and Sections 4.2, 4.3, and 5.3; the primary PDF is linked
from [the specification](spec.md)).  This implementation follows the agreed
first-version profile recorded there: it derives the configured parameters,
starts at `initial_radius`, grows the radius geometrically, uses the shared
table schedule, and checks the built-in termination predicate after an
evaluation and at a completed round boundary.  The default candidate budget
is `min(num_points, candidate_budget + k - 1)`; a supplied complete termination
rule replaces that budget and the default incumbent-distance predicate.  The
public lifecycle and dispatch tests exercise these boundaries.  Exact
cross-version numerical output and the consumer acceptance gates remain
separate validation questions documented in the specification.

### QALSH4C-style customization

- Reuse the collision-count rule and select k=1.
- Evaluate accepted candidates immediately using the current incumbent bound.
- Check the distance-based termination rule after candidates and at the required radius boundaries; omit the candidate-budget stop.
- Reuse library-owned index and per-query execution state across independent queries as appropriate.

Legacy C logically stops processing candidates inside an incremental scan, while the lower-level scan can still consume the remaining segment. Preserving this needs a precise scan-boundary contract: a strategy can stop candidate processing and consume observations to the boundary, but an immediate driver exit would change cursor advancement and possibly statistics. The simple operation-family sketch alone does not prove preservation of this behavior.

C's and H's persistent indexes use the library's recorded layout identity;
`PersistentIndex::Open` selects B+ tree or sorted-array traversal from the
file. Both layouts share the same strategy and engine contract, including the
documented logical scan boundaries.

### QALSH-H-style custom strategy

- On a projection hit, update private likelihood statistics.
- If accepted, request an immediate evaluation; otherwise update a private deferred queue when applicable.
- After advancing the radius, process due queue entries by requesting ordinary evaluations before resuming projection scanning. Reuse `TableScanSchedule` for table queueing and exhaustion while retaining the likelihood and deferred state in H.
- Select the configured termination-check boundaries without requiring deferred-state knowledge inside the engine.
- If projection scanning is exhausted but deferred work remains, continue radius progression and evaluation. If no work remains, finish with exhaustion.

There is no engine-level H queue or queue-specific `has_pending_points` callback. H's strategy knows when its own work is exhausted. Candidate evaluation order and tie handling differ from legacy H only as already explicitly approved; further differences are not implicitly approved.

## Limits and validation

The engine rejects invalid IDs, impossible traversal requests, and invalid numerical progression with exceptions. `SearchResult` reports strategy, scan-exhaustion, invalid-action, and step-limit termination. A custom strategy is responsible for meaningful progress and termination of its algorithm; the engine applies a configurable step limit because it cannot prove arbitrary strategy termination.

Proposed acceptance evidence:

- Express default QALSH, C-style customization, and H-style custom progression without consumer-specific changes to the engine.
- Inspect the external H integration for unnecessary boilerplate and copied QALSH orchestration. Reuse shared/default strategy facilities where appropriate; passing an interface-count check alone is insufficient.
- Trace projection hits, candidate evaluations, radius changes, check boundaries, and result updates with fixed projection vectors.
- Exercise externally submitted candidates, duplicate submissions, and scan exhaustion with and without deferred work.
- Test extension using a small library-external strategy, not by patching the core or exposing mutable internals.

The public lifecycle and dispatch tests exercise both persistent layouts and
the shared strategy seam; consumer tests cover the migrated C and H paths.
