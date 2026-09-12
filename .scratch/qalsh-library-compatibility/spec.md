# QALSH library reuse, selectable persistent indexes, and performance recovery

Status: ready-for-agent
Labels: ready-for-agent

This is a specification for improving the existing implementation, not a request to implement, commit, push, or merge it. The requirements discussion is complete for synthesis; the exact numerical interpretation of accuracy remains explicitly deferred as described below.

## Problem Statement

The maintainer built qalsh-lib to stop maintaining substantial duplicated QALSH infrastructure in qalsh4c and qalsh-h. Both consumers already have migration work, but acceptance has not been established: recorded comparisons include real slowdowns, obsolete source measurements, and a measurement-harness mistake that previously looked like a quality failure.

The library must provide complete standard QALSH, including collision-threshold candidate selection, search progression, and termination. A collection of low-level primitives alone does not meet the need. QALSH-H must reuse the common implementation while supplying likelihood-based candidate selection and deferred candidate work through a small, coherent extension contract. It must not need numerous internal interfaces or duplicate ordinary scan scheduling to express its differences.

The maintainer requires matching accuracy under the same random seed and targets no performance regression. No slowdown allowance, including the previously proposed 5%, has been approved. The main usage opens an index once and performs many queries, making steady-state query performance the primary timing concern.

## Solution

Improve the existing library and both integrations rather than starting a replacement implementation. Retain complete standard QALSH and move reusable search orchestration into the library while keeping genuinely algorithm-specific state and decisions in the consumer strategy.

Provide two persistent index layouts: B+ trees by default and sorted-array files selected through a construction option. Preserve the existing ephemeral in-memory index. Record the persistent layout in the file and recognize it when opening, so an existing index cannot be reinterpreted through a conflicting query option. Both persistent layouts use the same search and strategy capabilities.

Use matched, reproducible comparisons to distinguish search execution overhead from index layout costs and differences in algorithm work. Recover performance without silently changing search parameters, candidate work, or quality requirements. Deliver acceptance evidence for the actual consumer workflows and the supported backends, not only a passing library test suite.

## User Stories

1. As a maintainer, I want common QALSH infrastructure maintained in one library, so that fixes do not need to be repeated in both consumers.
2. As a library user, I want complete standard QALSH supplied by default, so that ordinary queries require no custom strategy.
3. As a library user, I want the default candidate rule to use a collision threshold, so that the default remains recognizable standard QALSH.
4. As a strategy author, I want to replace candidate selection without retaining an unwanted default threshold filter, so that my algorithm determines eligibility.
5. As a QALSH-H author, I want to accumulate likelihood evidence from projection hits, so that candidate selection retains H's algorithm.
6. As a QALSH-H author, I want to keep private likelihood state, so that H-specific concepts do not become engine internals.
7. As a QALSH-H author, I want to defer candidates and reconsider them after radius growth, so that eligibility can change without new projection hits.
8. As a QALSH-H author, I want to submit deferred candidate IDs through ordinary evaluation, so that I reuse point access, distance computation, and deduplication.
9. As a QALSH-H author, I want deferred candidate work to continue after scan exhaustion when appropriate, so that index exhaustion does not prematurely terminate my algorithm.
10. As a QALSH-H author, I want generic projection-table scheduling supplied by the library, so that customization does not require copying standard orchestration.
11. As a strategy author, I want explicit logical scan and termination boundaries, so that event timing has a stable algorithmic meaning.
12. As a strategy author, I want relevant read-only observations, so that I do not need mutable cursors, pages, or result containers.
13. As a maintainer, I want a compact extension demonstrated by H's actual integration, so that a small interface does not hide substantial consumer boilerplate.
14. As a caller, I want persistent construction to choose B+ trees by default, so that storage behavior is predictable without additional options.
15. As a caller, I want a construction option for sorted-array files, so that I can choose a simpler persistent layout without replacing the search implementation.
16. As a caller, I want a saved index to identify its backend, so that a later process opens the correct implementation automatically.
17. As a caller, I want an ephemeral in-memory index to remain available, so that existing memory-only workflows continue to work.
18. As a maintainer, I want both persistent layouts to share search execution, so that adding a backend does not duplicate candidate processing or result maintenance.
19. As a strategy author, I want backend changes to preserve documented logical boundaries, so that physical page sizes do not silently redefine my algorithm.
20. As a caller, I want actual projection vectors retained in persistent indexes, so that reopening uses the projections that were built.
21. As a migrating user, I want the same seed and inputs to preserve query accuracy, so that library adoption does not change experiment conclusions.
22. As an experiment author, I want actual projection vectors checked between variants, so that an identical seed label does not conceal different random-number consumption.
23. As a maintainer, I want per-query comparisons as well as aggregate metrics, so that offsetting quality differences do not disappear in averages.
24. As a maintainer, I want deferred accuracy details reported explicitly, so that an implementation cannot invent a tolerance or tie-policy exception.
25. As a caller, I want original points to remain caller-owned, so that integration does not force a dataset copy.
26. As a caller, I want reusable-buffer point access and bounded distance computation to remain correct, so that disk access optimizations preserve results.
27. As a caller, I want repeated and concurrent queries to use independent mutable state, so that opening one shared index supports my workload.
28. As a qalsh4c user, I want existing memory and disk estimation workflows preserved, so that the library can replace the production implementation.
29. As a qalsh4c user, I want QALSH-derived weights, sampling, and direction behavior retained, so that higher-level experiments remain meaningful.
30. As a qalsh-h user, I want default and complete-radius query modes preserved, so that existing experiment controls keep their meaning.
31. As a maintainer, I want no query performance regression against frozen consumer baselines, so that code reuse does not cost experiment throughput.
32. As a maintainer, I want backend comparisons inside the same library, so that storage costs can be separated from common execution overhead.
33. As a maintainer, I want fixed work and execution traces on diagnostic fixtures, so that faster timings cannot conceal altered search decisions.
34. As a maintainer, I want reproducible paired measurements with source and build fingerprints, so that performance findings describe the code being delivered.
35. As a long-running caller, I want steady-state query measurements separated from opening and construction, so that results reflect my actual usage.
36. As a maintainer, I want memory, index size, construction, and opening costs reported separately, so that query improvements do not hide resource regressions.
37. As a maintainer, I want failed and inconclusive scenarios kept visible, so that favorable cases cannot mask unresolved regressions.
38. As an integrator, I want source-tree and installed-package consumption to remain supported, so that the library is reusable outside its own build.
39. As a maintainer, I want immutable historical evidence preserved, so that corrected benchmarks do not erase the reason earlier acceptance failed.
40. As a maintainer, I want an explicit completion report for both real consumers, so that a library-only demonstration cannot be mistaken for successful migration.

## Implementation Decisions

### Complete default algorithm and shared orchestration

- Keep complete standard QALSH in the library. Standard candidate selection, ordinary table scheduling, radius progression, and default termination must be usable without custom consumer code.
- The principal measure of reuse is the removal of substantial duplicated code, not the number of public methods or a mandatory inheritance hierarchy. Separate consumer-controlled loops have not been selected as the solution.
- Preserve the library's ownership of cursor mechanics, point access invocation, distance computation, evaluation deduplication, and top-k maintenance. Reuse generic scheduling rather than requiring H to maintain an equivalent table queue and exhaustion ledger.
- H retains its likelihood state and deferred candidate data structure. Its extension needs initialization, projection-hit observations, radius-change handling, and a way to produce candidates without a new hit. A boolean rule invoked only on current hits is insufficient.
- Support the algorithm's required termination-check timing, including processing due deferred candidates at the appropriate radius boundary. Exact method names, callback counts, templates, virtual dispatch, and state representation are implementation choices to evaluate, not requirements fixed by this discussion.
- Do not add H-specific engine modes, expose mutable internal storage, or blindly merge superficially similar stop predicates whose budget semantics differ. Removing duplicate orchestration must preserve observable ordering and boundaries.

### Persistent backends

- B+ trees are the default persistent layout for both consumers. An explicit construction option selects a sorted-array file; defaults do not vary by consumer.
- Persist backend identity alongside version, effective index parameters, and actual projection vectors. Opening selects the recorded backend. The option chooses a new index layout; it does not reinterpret an existing file or silently convert it during a query.
- Retain ephemeral in-memory indexing. Reuse array traversal between memory and persistent array implementations where their contracts permit it; avoid creating separate search engines for the layouts.
- mmap is an access mechanism, not a third persistent index type. Its use does not imply that all pages are resident or that physical I/O is absent.
- Preserve strategy-visible logical scan boundaries across physical layouts. In particular, page capacity must not accidentally change when H checks termination. Supporting equivalent logical boundaries does not require exposing backend internals to H.
- Keep immutable index construction and reopen behavior, error reporting, corruption checks, and explicit rebuild requirements. Any file-format change must have a documented version/rebuild treatment. The new array format need not read legacy consumer files.

### Consumer integration and performance recovery

- Improve the existing migration work, retaining C's application-specific estimator and sampling logic and H's algorithm-specific likelihood behavior. Both persistent backends must be usable through the library's normal strategy contract.
- Preserve existing consumer controls and production workflows. Make backend choice available through normal integration configuration without forcing consumer code to instantiate internal storage classes. Exact command-line spelling is an implementation decision.
- Profile and compare query setup, projection traversal, strategy work, candidate distance evaluation, and result maintenance. Separate shared costs from page-layout costs before attributing a regression to B+ trees.
- Retain reproducible diagnostic traces without adding an intrusive production tracing API solely for tests. Compare projection hits, candidate evaluations, radius transitions, and check boundaries where needed to explain differences.
- Treat full validation during opening as a distinct cost. The open-once workload permits measuring steady-state queries independently; it does not authorize removing validation or ignoring memory consumption.
- Preserve caller-owned original points, borrowed-view lifetimes, independent query state, bounded-distance correctness, and the existing supported metrics and package-consumption model.

### Accuracy and performance requirements

- The user requires identical accuracy under the same random seed. This is stronger than the earlier allowance for merely non-worsening aggregate quality. Do not lower projection counts, reduce search effort, or change stopping behavior to make a timing comparison pass.
- Match random-number generation and consumption where required for consumer compatibility; verify actual projection vectors instead of assuming that equal numeric seeds imply equal indexes. Explicit projection injection remains useful for diagnosis, but does not replace checking the real seeded build path.
- The user deferred numerical accuracy details: bitwise distance identity, float tolerance, and equal-distance ID policy are not settled. Record exact per-query distances, returned IDs, and aggregate metrics. Do not adopt a new tolerance or tie exemption, or claim that changed results satisfy the requirement, without resolving the specific discrepancy with the user.
- No performance regression is the target. There is no approved 5% budget or other acceptable positive slowdown. A failure or uncertain result remains open; passing correctness tests or observing no statistically significant timing difference does not establish performance acceptance.
- Evaluate required scenarios separately. A faster backend, mode, or dataset cannot compensate for a slower required scenario. In particular, default B+ tree regressions cannot be hidden by reporting only the optional array backend.

## Testing Decisions

### Test seams and prior art

Use the existing public index construction/open/query entry points as the primary library seam. Use the real C/H commands and experiment workflows as the migration acceptance seam. The user explicitly confirmed these existing high-level seams during synthesis; do not invent internal test APIs.

Good tests verify externally observable results, lifecycle behavior, errors, and documented strategy event ordering. They should survive a change to cursor representation, dispatch mechanism, scratch-buffer layout, or private queue implementation.

Extend existing public lifecycle, dispatch, distance, and construction coverage. Reuse C's integration tests for memory/disk queries, directions, weights, sampling, and index errors, and H's existing strategy and executable-level regressions. Do not recreate those tests as assertions over private fields.

### Functional coverage

1. Build without specifying a backend and verify that the persisted metadata identifies B+ trees. Build with the array option and verify sorted-array persistence. Close and reopen each in a separate process and query through the ordinary API.
2. Exercise the same standard QALSH and external H strategy against supported backends. Supply identical explicit projections to isolate backend effects, and independently exercise real seeded construction and reopening.
3. Cover L1/L2 where supported, k=1 and larger k, repeated queries, equal projections, equal distances, duplicate coordinates, boundary windows, partial results, and scan exhaustion. Use independent exact oracles to check distance correctness; do not demand exact-nearest-neighbor results from an approximate algorithm as a substitute for compatibility tests.
4. Test deferred candidates becoming eligible after radius growth with no new hit, including after scan exhaustion. Verify exactly-once distance evaluation and the documented order of evaluation and termination checks.
5. Test logical boundaries across leaf/page and array-region boundaries. Changing the backend must not silently change the requested scan quantum or strategy check timing.
6. Preserve borrowed-buffer and concurrent-query tests, bounded-distance pruning and ties, error propagation, and construction/reopen integrity checks for both persistent formats.
7. Validate ordinary source-tree linking and installed-package consumers. Run applicable existing consumer regressions as well as library tests.

### Comparison protocol

- Preserve frozen original consumer revisions and historical raw results. Also snapshot the actual current source used for each new experiment, including uncommitted changes, harnesses, linked library artifacts, compiler flags, feature macros, and executable fingerprints. Do not use a moving branch name as a baseline.
- Freeze a representative scenario matrix before optimization. Retain the previous required consumer coverage: C memory/disk, L1/L2, ab/ba/both, single and fixed production thread counts, weights and sampling; H default/complete-radius, k=1 and larger k, supported parameter overrides, and exact controls. Extend relevant persistent scenarios to both new layouts. Missing data or an unfinished case is a coverage gap, not a pass.
- Include available Federalist/MNIST C workloads and toy/GIST H workloads as appropriate, with verified query and ground-truth fingerprints. Confirm availability before scheduling experiments rather than assuming that historical remote files still exist.
- Measure two distinct comparisons: each migrated consumer against its frozen original, and B+ versus array within the same library/configuration. The second comparison diagnoses backend cost but cannot replace migration acceptance.
- Follow the existing repeated paired protocol: documented warmup, at least ten alternating or randomized baseline/migrated pairs, matched optimized builds, fixed thread counts and affinity, consistent data/query order, and recorded cache conditions. Choose the confidence-interval method before reading the results and increase duration or repetitions when noise dominates.
- Verify relevant feature macros and actual concurrency, not merely linked runtime libraries. Preserve the earlier harness failure as a regression lesson: missing an OpenMP compile definition previously invalidated comparisons.
- Report absolute query times, paired ratios, distributions and uncertainty. No particular confidence threshold or positive equivalence margin has been newly approved; do not turn inconclusive measurements into a no-regression claim.
- Prioritize open-once repeated-query timing. Also report construction, opening, complete-command timing, peak memory with file-backed accounting, index size, and concurrency scaling. Do not call post-validation queries cold if opening already touched the index pages, or label software read counters as physical disk I/O.
- Apply quality checks before interpreting speed. Inspect per-query differences and exact-reference quality as well as aggregate Recall/Ratio and C application outputs. A better average cannot establish the requested identical accuracy.
- Run substantial validation on Centaurus after one-way synchronization of locally edited code. Preserve H's Linux/container execution requirements. If Centaurus is unavailable, report that and do not substitute resource-intensive local runs without permission.

### Completion evidence

Completion requires both integrations to use the library in their production paths, complete default QALSH, compact H customization without duplicated generic scheduling, both persistent layouts with the default/option contract, applicable correctness and package checks, and current-source quality/performance evidence for the declared matrix. Any unresolved numerical-policy discrepancy, regression, missing case, or inconclusive performance finding must be visible in the final report and prevents an unconditional acceptance claim.

## Out of Scope

- Implementing, committing, pushing, or merging during this specification-writing task.
- Rewriting unrelated consumer application algorithms, estimators, or exact-search controls.
- Adding dynamic index updates, arbitrary new metrics, GPU execution, or distributed query execution.
- Importing legacy consumer index files or copying reference implementations into the library.
- Making H a hard-coded library mode or exposing mutable backend internals as its extension contract.
- Choosing exact C++ signatures, dispatch representation, private data structures, or an arbitrary maximum public-method count before implementation evidence.
- Redesigning H's mathematical candidate rule or using unapproved evaluation-order changes as an optimization shortcut.
- Accepting slower execution under the old proposed 5% allowance, or silently adopting numerical/tie tolerances that the user deferred.

## Further Notes

- This specification supersedes conflicting earlier requirements that excluded persistent arrays, treated B+ trees as the only disk choice, or allowed changed per-query behavior based only on non-worsening aggregate quality. Other established library and consumer obligations remain in force. In particular, prior H evaluation-order allowances are not sufficient justification for changed accuracy under the user's current requirement.
- The user accepted the dual-backend decision with B+ trees as the default. This is recorded in [ADR 0001](../../docs/adr/0001-selectable-persistent-index-layout.md). The shared domain glossary remains [CONTEXT](../../CONTEXT.md).
- Historical baseline and review material remains under the [original effort](../qalsh-library/spec.md). The [later progress checkpoint](../qalsh-library/review-3/progress.md) explicitly records that acceptance was incomplete. Its measurements predate subsequent source changes and cannot be relabeled as current acceptance.
- Source inspection confirms that the library already uses mmap-backed B+ pages and resumable leaf traversal. Existing array-mode regressions also show that adding an array backend alone is not evidence that shared execution overhead has been solved.
- The [original author's QALSH source](https://github.com/HuangQiang/QALSH/blob/master/methods/qalsh.h) uses B+ trees, but its layout and traversal details are not identical to the library. The user's recollection that switching to arrays produced little improvement is a useful lead, not an independently verified benchmark result.
- The exact accuracy interpretation was deliberately deferred by the user. Work that preserves exact observed outputs can proceed once implementation is separately requested; a change needing a numerical or tie-policy exception must obtain a concrete decision before it is accepted. This deferred item is not permission to resume a broad requirements interview.
- Status `ready-for-agent` publishes the specification to the local issue tracker. It does not authorize implementation and does not assert that the migration already passes acceptance.

## Comments

- 2026-09-12: Synthesized the completed architecture discussion using the to-spec workflow. No production code changed and no new benchmark results are claimed.
- 2026-09-12: User confirmed testing through the public build/open/query API and real C/H commands and experiment workflows, rather than private implementation structure.
