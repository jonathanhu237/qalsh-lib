# QALSH Library and Consumer Migration Specification

**Status:** specification for review; not authorization to implement. The requirements discussion is complete. This document replaces the earlier discussion checklist. Concrete C++ signatures remain an implementation-design responsibility, not a reason to add consumer-specific switches.

**Completion means:** both real consumers use the library on dedicated migration branches, duplicated QALSH infrastructure is removed from their production paths, and baseline-versus-migrated accuracy and performance evidence meets the gates below. A library build, standalone example, or synthetic test suite alone cannot complete this task.

## Problem Statement

The maintainer currently maintains substantial overlapping QALSH infrastructure in `qalsh4c` and `qalsh-h`. Fixes and optimizations must be repeated, while the consumers have genuinely different candidate selection, search progression, and termination behavior.

Simply copying one implementation into a library would either constrain the other consumer or produce a growing collection of special-case flags and callbacks. Conversely, exposing only low-level index primitives would force each consumer to keep its own search loop and much of the duplication.

The maintainer needs a reusable C++ library that owns common indexing and query execution, provides useful default QALSH behavior, and makes a different algorithm such as QALSH-H straightforward to integrate as an external search strategy. Reuse must not come at the expense of query accuracy or a meaningful loss of speed.

## Solution

Build a library with two static projection index types: an in-memory sorted-array index and a disk-backed B+ tree index. The library owns traversal mechanics, point access invocation, distance computation, evaluation deduplication, top-k maintenance, and the search execution loop.

Ship a default QALSH search strategy. Simple variations replace its candidate rule, termination rule, or termination-check timing. More substantial variations supply a complete stateful search strategy, reusing the same execution engine without modifying it. QALSH-H is the primary real-world proof that this extension boundary works; it is not a hard-coded mode of the library.

Original points remain caller-owned and are accessed by point ID. The library supplies L1/L2 distances and the matching random projections. The B+ tree index supports separate construction and query processes through a library-owned persistent format. The in-memory index is ephemeral.

During the future implementation phase, create a dedicated migration branch in each consumer, replace its duplicated QALSH implementation with library usage, and run paired comparisons against immutable pre-migration baselines. Keep the consumers' application logic and user-facing experiment workflows intact. No migration branches or consumer edits are part of writing this specification.

## User Stories

1. As a maintainer, I want shared QALSH infrastructure maintained once, so that a fix does not need to be repeated in two projects.
2. As a library user, I want default QALSH search to work without custom strategy code, so that the common use case stays simple.
3. As a researcher, I want to replace the default candidate rule, so that I can test a different candidate criterion without modifying the engine.
4. As a researcher, I want to replace the default termination rule, so that I can change the stopping decision independently of candidate selection.
5. As a researcher, I want to choose when termination is checked, so that candidate-level and radius-level behavior can be expressed accurately.
6. As a researcher, I want to supply a complete search strategy, so that substantial algorithm changes do not require new project-specific library interfaces.
7. As a strategy author, I want to keep my own per-query state, so that collision counters, likelihood statistics, and queues need not become engine concepts.
8. As a strategy author, I want to reuse common QALSH orchestration where appropriate, so that a small change does not require copying an entire strategy.
9. As a strategy author, I want the library to execute scans and distance evaluations, so that I do not have to implement the common search loop again.
10. As a strategy author, I want access to documented projection-hit information and query state, so that my decisions do not depend on private index internals.
11. As a strategy author, I want to submit a point ID that came from my own deferred state, so that a candidate need not originate from the latest projection hit.
12. As a strategy author, I want to process deferred work after radius growth and before further scanning, so that previously observed points can become eligible without new hits.
13. As a strategy author, I want scan exhaustion distinguished from algorithm completion, so that external deferred work is not discarded prematurely.
14. As a caller, I want an in-memory projection index, so that I can build and query within one process without persistence overhead.
15. As a caller, I want a disk-backed B+ tree projection index, so that projection tables do not all have to remain resident in memory.
16. As a caller, I want to build a B+ tree index once and open it in a later process, so that indexing and querying remain separate workflows.
17. As a caller, I want index files to retain the actual projection vectors and structural metadata, so that reopening does not regenerate a different index.
18. As a migrating user, I want a clearly documented rebuild path, so that adoption does not depend on reading historical consumer index formats.
19. As a caller, I want to retain ownership of original points, so that I can use existing memory, file, and cache access without a forced dataset copy.
20. As a caller, I want point access by ID with a clear lifetime contract, so that reusable disk-read buffers can be used safely.
21. As a caller, I want L1 and L2 support, so that the distance modes already needed by the consumers remain available.
22. As a caller, I want distance computation centralized in the library, so that bounded-distance optimizations are not duplicated in consumers.
23. As a caller, I want each point evaluated at most once per query, so that repeated projection hits or deferred submissions do not repeat distance work.
24. As a caller, I want a top-k result contract with k=1 supported naturally, so that both single-neighbor and multi-neighbor applications use one abstraction.
25. As a caller, I want deterministic ordering among evaluated equal-distance results, so that ties are reproducible and testable.
26. As a caller, I want partial results and a termination reason when work is exhausted, so that an early or incomplete result is not mistaken for a full top-k answer.
27. As a caller, I want configurable initial radius and geometric growth, so that existing search schedules can be reproduced explicitly.
28. As a caller, I want default QALSH parameter derivation, so that ordinary index construction does not require reimplementing formulas.
29. As a researcher, I want to supply explicit index and strategy parameters, so that non-default parameter derivations do not get overwritten by the library.
30. As a researcher, I want to supply exact projection vectors as well as a random seed, so that baseline and migrated searches can use identical projections.
31. As a caller, I want to inspect effective index parameters, so that downstream calculations such as sample-size estimation can use them.
32. As a caller, I want concurrent queries to share a read-only index with independent query state, so that existing parallel workloads remain supported.
33. As a library user, I want invalid input and unreadable indexes reported as errors rather than process termination, so that an embedding application controls recovery.
34. As an integrator, I want normal CMake consumption and documented dependencies, so that using the library does not require copying source files manually.
35. As a maintainer, I want the qalsh4c migration isolated on a dedicated branch, so that I can inspect and accept it independently of its baseline.
36. As a maintainer, I want the qalsh-h migration isolated on a dedicated branch, so that I can inspect and accept it independently of its baseline.
37. As a qalsh4c user, I want ANN-based distance estimation and QALSH-derived sampling weights preserved, so that higher-level experiments retain their meaning.
38. As a qalsh-h user, I want likelihood-based candidate selection and deferred work implemented outside the engine, so that my algorithm remains an ordinary extension.
39. As a qalsh-h user, I want both existing termination-check modes preserved, so that the migration does not silently change the meaning of my experiments.
40. As a maintainer, I want duplicated production indexing and search execution removed from migrated consumers, so that the migration actually reduces maintenance.
41. As a maintainer, I want immutable baseline revisions and recorded library revisions, so that comparisons are reproducible.
42. As a maintainer, I want accuracy compared against the original consumers and exact ground truth, so that a plausible-looking result is not accepted as compatibility evidence.
43. As a maintainer, I want performance measured under matched data, compiler, concurrency, and cache conditions, so that speed claims reflect the library rather than an unfair setup.
44. As a maintainer, I want per-scenario accuracy and speed results, so that gains on one workload cannot hide regressions on another.
45. As a maintainer, I want unsuccessful and inconclusive comparisons reported honestly, so that missing evidence cannot be counted as acceptance.
46. As a maintainer, I want a bounded, explicit slowdown allowance, so that “not too much slower” becomes a measurable release decision.
47. As a maintainer, I want build time, open time, memory, and index size reported separately from query latency, so that speed is not purchased through hidden resource costs.
48. As a maintainer, I want preserved copyright notices and checked source provenance, so that code reuse is compatible with the library's MIT license.
49. As a future strategy author, I want a small documented extension surface demonstrated by real consumers, so that a new strategy can be added without negotiating many new hooks.
50. As a reviewer, I want actual migration branches and reproducible comparison artifacts rather than only toy examples, so that I can verify the stated goal has been achieved.

## Implementation Decisions

### 1. Ownership and extension architecture

- Separate projection index functionality, query execution, and algorithm-specific search strategy. These are logical responsibilities, not a mandate for a particular source layout or hierarchy.
- Keep the execution loop in the library. A custom strategy decides progression and candidate acceptance; it must not have to reimplement file I/O, cursor mechanics, distance kernels, deduplication, or top-k maintenance.
- Support lightweight rule replacement within the default strategy and whole-strategy replacement for deeper changes. Replacing a candidate rule must not leave the default collision threshold in front of it as an extra filter.
- Use a coherent strategy contract rather than adding a boolean or callback whenever a consumer has another requirement. A small public API is not sufficient if it transfers extensive duplicated orchestration to each consumer.
- The operation-based design sketch is explanatory, not a requirement for a public command interpreter, coroutine framework, virtual callback per hit, or heap allocation per event. Select a C++ representation that allows efficient hot paths and validate it through the actual migrations.
- Give strategies documented read access to relevant query state and projection-hit information, and a controlled way to request execution. Do not expose mutable internal cursors or result containers as the extension mechanism.
- Distinguish projection-hit, incremental-scan-boundary, current-window completion, and full scan exhaustion semantics. Their precise ordering is part of the public strategy contract.
- A stateful strategy may own deferred work. The engine accepts ordinary candidate IDs from it, without an H-specific queue, queue inspection callback, or special-case branch.
- Index traversal must be resumable enough to implement the required candidate and termination decision boundaries. Backend page boundaries must not silently redefine a requested logical check boundary.

### 2. Projection indexes and persistence

- First-version index types are a static in-memory sorted-array index and a static disk-backed B+ tree index. Do not introduce a third “mmap index” category: mmap is an I/O mechanism, not an index structure.
- The in-memory index is built and queried within the process and does not require save/load support. The B+ tree index supports a build/save lifecycle and an independent open/query lifecycle.
- Support immutable indexes only. Original point coordinates and ID mappings must remain consistent with those used to construct the projection index; changes require rebuilding.
- The library owns the B+ tree format. Reading either consumer's historical index format is not required. Migrated construction commands must produce the library format, and migrated queries must open it.
- Persist actual projection vectors, point count, dimensionality, metric, projection count, scalar/ID representation, format version, page/layout information, and the structural information needed to interpret the index. Opening must not regenerate projections or silently rederive structural settings.
- Validate metadata, layout bounds, file completeness, and caller/index dimensional consistency. A partially built or incompatible index must not appear to open successfully. Define the supported byte-order/platform contract rather than implying arbitrary binary portability.
- Do not serialize arbitrary strategy objects into the index. Keep application/strategy parameters caller-owned or in consumer configuration, separate from structural index metadata. Reopening must expose the effective parameters needed to construct a compatible strategy.
- A caller is responsible for providing the original dataset corresponding to an index. Matching n and dimensionality alone cannot establish dataset identity; document this limitation rather than claiming full content verification.
- B+ tree queries must be able to read pages on demand; disk-backed construction is not itself a promise of strictly bounded-memory construction. Measure construction peak memory and do not conceal material regressions.

### 3. Original points, distances, and results

- Original points remain caller-owned. Invoke caller-provided point access by ID during construction and query evaluation, without requiring a second persistent copy of all vectors inside the index.
- Define a point-access lifetime contract supporting both borrowed immutable memory and temporary read buffers. Consume a borrowed point before another access can invalidate it unless the accessor explicitly guarantees a longer lifetime. Retained pointers must not outlive their contract.
- Support float32 coordinates and L1/L2 compatibility with the consumers. L1 uses Cauchy projections and L2 uses Gaussian projections. The index metric is fixed at construction; arbitrary custom metrics are not a first-version requirement.
- Evaluate accepted candidates immediately, one at a time. Do not implement H's pending-ID sort and batch evaluation optimization in this version; the user explicitly selected the 4C-style evaluation path.
- The library may bound distance computation by the current top-k threshold. Pruning must distinguish “above the useful bound” from a true distance; an incomplete sum must not be returned as an exact result distance. Before k results exist, do not use an unjustifiably tight bound. Equal-distance tie handling must remain correct under pruning.
- Deduplicate evaluation by point ID within each query, including IDs submitted from deferred state. Points not yet accepted may continue accumulating candidate-rule evidence.
- Maintain top-k within the library and rank evaluated results by distance ascending, then point ID ascending. This is not a promise to discover all tied points before early termination.
- Return the actual available results and a reason when a query finishes. If scanning is exhausted and the strategy has no further candidate work, return exhaustion, possibly with fewer than k results. Do not silently perform exhaustive search to fill the result set.
- Define and test input validation, including invalid IDs, dimensional mismatches, invalid k, invalid radius/configuration, and non-finite numeric input. Report errors through an embedding-safe contract rather than exiting the host process. Exact public error types are an implementation detail to document consistently.

### 4. Default QALSH and parameter configuration

- Provide default collision-count candidate selection and a default QALSH termination rule. The default top-k termination concepts include enough qualifying results within cR and the candidate-budget condition; neither consumer's modified behavior alone defines the standard baseline.
- Ground the L2 default formulas and stopping concepts in the original QALSH publication. Keep a clear distinction between paper-level rules, implementation choices such as check timing, and the consumer compatibility strategies.
- The agreed radius schedule starts at 1 and grows geometrically by c, with configurable positive initial radius and growth multiplier greater than 1. This is an explicit library profile: the original paper also describes skipping empty rounds, which must not be silently added during extraction.
- Preserve supported L1 parameter behavior using verified compatible provenance; do not apply Gaussian-specific formulas to L1 merely because the search framework is shared.
- Provide automatic QALSH parameter derivation and explicit caller-supplied configuration. Validate explicit settings without replacing them behind the caller's back. Candidate-rule parameters, including a collision threshold or H's likelihood controls, belong to that rule/strategy rather than universal engine state.
- Expose effective index/configuration values for downstream use. In particular, qalsh4c's sample-size calculations need more than a black-box search call.
- Support explicit projection vectors and seeded generation. Equal seeds alone are not guaranteed to produce equal projections across random generators or standard-library environments; comparisons requiring equality must share actual vectors.
- Do not claim the original approximation guarantee for arbitrary custom strategies, radius settings, or check schedules. In particular, do not casually identify the internal radius factor c with an unconditional end-to-end c approximation guarantee.

### 5. Termination, deferred work, and concurrency

- Separate the termination predicate from its check timing. Preserve the ability to check after candidate evaluations, at logical scan boundaries, and at the required radius boundaries through strategy behavior, not project-name modes.
- Distance evaluation being immediate does not imply termination is always checked immediately. This distinction is essential for H's full-radius mode and for soft candidate-budget behavior.
- After radius advancement, a custom strategy can submit candidates from private deferred state before requesting further projection scanning. It can continue this work after scan exhaustion and finish once no work remains.
- The engine validates requested operations and detects invalid numeric progression or execution failure. A strategy remains responsible for meaningful algorithm progress; the engine cannot prove termination of arbitrary user code. Tests must protect against the concrete exhaustion/stagnation cases being migrated.
- Share immutable index resources across concurrent queries. Give each concurrent query independent mutable cursor, deduplication, result, and strategy state. No implicit multithreading inside one query is required.
- Point access used concurrently must satisfy the caller's concurrency contract. Avoid shared mutable RNG or process-global query counters that make otherwise independent queries interfere.

### 6. Actual consumer migrations

- At implementation start, create a dedicated migration branch in each consumer from its pinned baseline. Suggested names are `refactor/use-qalsh-lib`; branch names must not use a `codex/` prefix. Do not merge into the consumers' default branches as part of acceptance without separate authorization.
- Pin both consumers to an identifiable, reproducible library revision. Development against an uncommitted local checkout is not the final integration artifact.
- qalsh4c must use library indexing and search execution for its current QALSH memory and disk workflows. Use the library memory index for its memory mode and the library B+ tree index for its migrated disk mode. Do not silently drop the disk scenario because its previous backend was mmap over flat arrays.
- Retain qalsh4c's Chamfer/estimator/sampling application logic. Express its QALSH differences through default-strategy customization: collision-count selection, k=1, no candidate-count termination, incumbent-based distance pruning, and its required check boundaries. Preserve access to effective parameters used by sample-size estimation.
- qalsh-h must build/open the library B+ tree index and supply its likelihood/deferred behavior as an external strategy. Keep parameter derivation specific to H outside the generic core, including dependencies only H needs. Preserve its query controls and the meaning of the existing default and complete-radius modes, subject only to the approved evaluation-order and tie changes.
- Remove duplicated production QALSH index construction, storage/traversal infrastructure, distance execution, and result maintenance where those duties move to the library. Keeping the old implementation as the selected/default production path while demonstrating a library example is not migration.
- Retain genuinely consumer-specific strategy logic, point access, CLI/configuration, reporting, and unrelated search methods. A separate exact linear-scan implementation used by the application is not automatically part of the extraction.
- Preserve public commands and output semantics used by existing experiment tooling, except for documented index rebuild requirements. Any intentional CLI incompatibility requires explicit acknowledgement rather than being hidden inside refactoring.

### 7. Build, dependencies, provenance, and delivery

- Provide CMake-based consumption using ordinary target linking, with both source-tree consumption and installation/package discovery validated. Do not require consumers to copy the library's implementation files into their own targets.
- Remain compatible with the supported consumer toolchain and dependency environment; do not introduce a newer language/compiler requirement without a concrete reason. Hide application-only dependencies from the generic core and public API wherever possible.
- Preserve MIT notices from substantial reused consumer code and record provenance. The authors' reference repositories are GPL-3.0 according to GitHub's license metadata; they are algorithm references, not MIT source donors. Do not copy their code into the MIT library without resolving license compatibility. A local MIT label is not a substitute for checking the provenance of copied material.
- All source edits and Git operations happen locally. When implementation is authorized, synchronize code one-way to Centaurus and run substantial validation there. Do not silently run resource-intensive experiments locally if Centaurus is unavailable.
- Deliver the library, both integration branches, build/integration documentation, and a baseline comparison report with enough artifacts to reproduce the result. Do not commit or push during this specification-only task.

## Testing Decisions

### Test seam and prior art

**Primary new seam:** the public projection-index lifecycle plus public search entry point, exercised by an external consumer of the library. Supply original points, explicit projections, configuration, and a strategy, then assert externally observable results, errors, lifecycle behavior, and documented strategy interactions.

**Existing acceptance seam:** the two consumers' existing build/index/query/estimation commands and experiment workflows. This is required to demonstrate real integration and speed; it is not a new production testing interface.

Prefer these high-level seams over directly testing private cursors, queue layouts, memory stamps, or a particular command-dispatch representation. A good test survives changing those internals while failing when documented results, event ordering, persistence, or resource/error behavior changes. The library currently has no implementation or test suite to reuse.

Prior art in qalsh4c includes CTest coverage for clone independence, in-memory queries, direction handling, sampling consistency and weight caches, index reuse/corruption/missing-index errors, and dataset workflows. Its sampling-consistency test compares memory/disk L2 weights and estimates for both directions, using a deliberately matched projection seed. Reuse these behaviors as regression requirements, not as permission to assert private representation details. The development build workflow does not itself run CTest, and the release preset disables tests; invoke correctness tests explicitly and benchmark a separate optimized build.

qalsh-h has no registered CTest suite in the inspected baseline. Add its regression coverage at its existing index/query executable and the library's public query seam, including its exact linear-scan control. Some existing C benchmark scripts emit obsolete CLI/index-layout forms; check each harness against current command help before using it. Do not classify a stale script failure as an algorithm failure or assume every checked-in benchmark script is runnable.

The proposed seams are awaiting user confirmation as part of specification review. They do not require a renewed requirements interview or low-level API approval.

### Public behavior coverage

- Build and query both index types with deterministic small datasets and exact projection vectors; cover L1/L2, k=1 and multiple k values, repeated queries, equal projections, equal distances, duplicate coordinates, zero-distance neighbors, and boundary-radius cases.
- Validate distances and top-k independently with a straightforward exact oracle on bounded fixtures. Use complete-enumeration test strategies to test engine correctness separately from approximate default-strategy quality.
- Exercise default candidate behavior, full candidate-rule replacement, full termination-rule replacement, different termination boundaries, and an externally defined stateful strategy. Rule replacement must not retain an unintended default filter.
- Verify exactly-once distance evaluation across repeated projection hits and deferred submissions without asserting a specific internal stamp/bitset mechanism.
- Verify that deferred state can yield candidates without new projection hits, including after scan exhaustion, and that an exhausted strategy returns partial results with an explicit reason.
- Exercise bounded-distance evaluation and ties so that a pruned value cannot become a false exact result or discard a preferable equal-distance ID.
- Construct a persistent index, destroy construction state, and reopen it in a separate process. Verify stable projections/results and rejection of truncated, incompatible, or incomplete indexes.
- Exercise borrowed-memory and reusable-buffer point access, plus concurrent independent queries against one index. Use sanitizer builds for memory and undefined-behavior checks; any suitable race validation is separate from timing measurements.
- Run invalid-input and execution-failure cases through the public API. The application process must not be terminated by library logging.
- Compile and run an external installation consumer to catch missing exported dependencies, headers, or link requirements.

### Frozen baseline and comparison manifest

Before migration edits, record baseline revisions, source status, compiler/build flags, dependency versions, datasets and query/ground-truth fingerprints, all effective parameters, actual projection vectors, random settings, thread count, machine/storage information, and cache policy. Pin qalsh4c's thread count explicitly rather than using its machine-dependent default. Include direction, sampling count/mode, weight-cache state, page size, logical scan settings, and profiling options where applicable.

A concrete seed pitfall already exists: C's matched memory/disk consistency test uses projection seed 1608637542 because the fixed-seed memory path draws a construction seed from mt19937(42), whereas a disk build may default directly to 42. H also has generator state that can continue across builds in one thread. Record actual vectors and generation provenance rather than assuming two fixed-seed flags imply identical indexes.

The intended baseline revisions are recorded in Further Notes. If a working tree contains additional changes, preserve them and explicitly snapshot the intended baseline instead of silently incorporating or discarding them. Do not compare against a moving branch name.

Build original and migrated executables separately from those frozen inputs. Generate fresh library-format indexes without overwriting the original baseline index. Share exact projections for equivalence tests; a same-seed claim is not enough. Any test-only projection export needed from a baseline must be isolated and shown not to change query execution or measured timing.

### Required migration matrix

| Consumer/workflow | Required coverage | Accuracy observations | Timing observations |
| --- | --- | --- | --- |
| qalsh4c in-memory QALSH | L1/L2, k=1, ab/ba/both directions, single and fixed production thread counts | Per-query distance, independent exact-reference error/ratio, directional/application outputs | Query-stage latency and existing end-to-end workflows |
| qalsh4c disk QALSH | L1/L2 and ab/ba/both; original flat/mmap baseline versus migrated library B+ tree; index reuse and rebuild | Same quality measures; report traversal-related changes rather than pretending identical storage | Warm-cache queries and explicitly controlled disk/cache experiments; report separate results |
| qalsh4c QALSH-based weights and estimators | Memory/disk and supported norm/direction combinations; explicit/automatic sample counts; uncached, cache-miss, cache-hit cases | Weight/distance changes, sample-count implications, repeated estimator error | Weight-generation stage and end-to-end estimation time, separating cases that actually perform QALSH work from weight-cache hits |
| qalsh4c unaffected controls | Existing exact scan, uniform sampling, and in-memory quadtree workflows | Existing regression tests and output semantics | Sanity checks; these cannot substitute for QALSH timing |
| qalsh-h default mode | L2; k=1 and representative larger k within metadata capacity; default/explicit build m, valid query m and beta overrides | Recall@k, reported distance Ratio, per-query/rank distance changes and result ordering | Query-stage latency and existing query-command timing |
| qalsh-h complete-radius mode | Same inputs/parameters as its baseline mode | Same metrics, independently of default mode | Same timing protocol, independently of default mode |
| qalsh-h exact control | Existing linear-scan query mode | Ground-truth and distance-convention sanity checks | Control only, not proof of QALSH performance |

Use the existing real datasets/workloads available for each consumer, supplemented by tiny deterministic edge cases. Freeze a named, representative matrix before optimization, including data exceeding practical cache where available. Cover the dimensions above with a declared matrix rather than silently omitting inconvenient modes. Missing a required real-data scenario is a coverage gap, not a pass. No single toy dataset, favorable seed, or aggregated cross-mode average is sufficient.

Verified starting data: C has a committed Federalist dataset (A=1880, B=1178, d=300) with L1/L2 directional truths and a local MNIST dataset (A=3000, B=67000, d=784) whose ba truths are absent. H has a toy dataset (n=10000, queries=100, d=256, ground-truth k=100); its larger benchmark datasets are distributed separately. Do not invent missing truth values or assume these two consumers share a data format. Generate or obtain required exact truths reproducibly on the approved execution environment before using missing directions in a quality gate.

Larger dataset availability on Centaurus is not yet verified. C's repository guidance restricts local dataset-dependent execution and H requires Linux Docker testing; reconcile these by using the appropriate Linux/container environment on Centaurus, documenting toolchain/image identity. Do not run resource-intensive tests locally to bypass those constraints.

### Accuracy gate

- The objective is no query-quality regression against each frozen consumer baseline. A speed improvement does not excuse worse quality, and a better mode does not compensate for a worse mode.
- For scenarios intended to preserve the same projection and execution decisions, compare per-query results directly: distances must agree within a predeclared float32 numerical tolerance, and IDs must agree except for the explicitly approved deterministic tie policy.
- For approved execution-order/backend changes, exact ID identity is not automatically required, but evaluate the whole frozen query set against both the original output and exact ground truth. In every scenario, Recall@k must not decrease and the applicable mean distance Ratio/error must not worsen, apart from predeclared numerical tolerance. Report tails and per-query differences so that aggregate equality cannot hide unexplained outliers.
- For qalsh4c, retain the existing mathematical meaning of directional distance/estimator outputs: a direction is a sum of nearest-neighbor distances, and `both` adds the two directed sums. Its reported relative error is absolute estimate error divided by truth, with explicit near-zero-truth handling. Do not substitute an average or a different error metric and call it accuracy preservation. Match sample randomness when comparing stochastic downstream estimators, and report repeated-estimate behavior when sample counts or weights change.
- H's Ratio averages rank-wise returned/ground-truth distances, while Recall measures overlap with the ground-truth top-k IDs. Ground-truth ties can affect ID recall and must be reported rather than silently changing the metric. Its current reporting assumes k returned results and lacks a zero-ground-truth ratio guard: use independent validation for zero-distance/partial-result fixtures, and adapt consumer reporting safely if those library-supported outcomes occur. Do not let division by zero or out-of-bounds reporting stand in for a quality result.
- Do not hide boundary failures by increasing tolerances after inspecting the results. Numerical tolerances apply to floating-point computation, not to a substantive recall loss. Report the chosen tolerances and why they are appropriate before measurement.
- If approved H immediate evaluation or the 4C disk-backend migration causes a quality failure, the migration is not accepted. Diagnose the strategy/traversal contract or request an explicit scope decision; do not silently reintroduce an old production implementation or weaken the gate.

### Performance gate and measurement protocol

The user requires unchanged speed or only a small slowdown, with improvement preferred. The conversation did **not** specify a numerical slowdown allowance. To make acceptance executable, this draft proposes the following policy; **the 5% margin is an approval item, not a previously agreed requirement**:

- **Proposed maximum slowdown: 5% per required scenario.** Measure the ratio of migrated to baseline elapsed time using repeated paired runs. Pass when the upper endpoint of a documented 95% confidence interval for the paired median ratio is at most 1.05. A clear ratio above the budget fails; an interval overlapping the limit is inconclusive and needs more controlled evidence rather than being counted as success.
- Use at least ten measured, alternating/randomized paired runs after documented warmup. Increase run duration/repetitions when timer noise or variability makes the comparison inconclusive. Select the confidence-interval method before inspecting the results.
- Use optimized non-sanitized builds with matched compiler, flags, architecture settings, dependencies, threads, affinity policy, and dataset/query order. Avoid measuring while another experiment saturates the machine or storage.
- Include real strategy dispatch, point access, and distance computation in query timing. Do not time only a kernel and omit the overhead introduced by the abstraction.
- Measure query execution separately from index construction/opening and process startup, and also run the existing end-to-end workflows. C's current estimate timer may include in-memory index construction or weight generation; H's timer covers its query loop after initialization. Record these different boundaries explicitly. A startup-heavy command or a weight-cache hit cannot by itself prove that the query loop did not regress.
- Keep warm-cache and disk-oriented measurements separate. Use a reproducible, permitted cache protocol and report it. Starting a new process is not proof of a cold OS page cache. Do not equate logical read counters with physical storage I/O.
- Report absolute times, medians, distributions/tails where measurable, speed ratios, confidence intervals, thread counts, and raw per-run observations. Do not accept from a single run or from “no statistically significant difference.”
- Report construction time, open time, peak memory, index size, and concurrency scaling separately. C's displayed memory delta is not peak RSS, and H's I/O counter is an integer-averaged software counter, not physical disk traffic. Add trustworthy measurement alongside existing output rather than renaming these values. The query slowdown budget does not automatically approve a large resource regression or the accidental loading of an entire disk index into memory.
- Apply accuracy gates first and hold the effective search configuration fixed. Do not improve time by reducing projection count, lowering candidate work, weakening termination, or silently changing the metric. An approved mode change must be visible in the comparison manifest.
- If the user does not approve a slowdown margin, do not claim that an observed regression is acceptable merely because it is below the proposed 5%. Preserve the raw result and leave the acceptance decision open.

### Completion checklist

1. The library builds and its public behavior/install tests pass in the supported environment.
2. Both dedicated consumer migration branches build and use the pinned library through their actual production commands.
3. Duplicated common QALSH production infrastructure is removed or replaced, with consumer-specific strategy/application code clearly retained.
4. H's strategy compiles outside the library and integrates without H-specific core changes or public hooks.
5. All required accuracy scenarios pass with documented projections, tolerances, and baselines.
6. All required performance scenarios pass the approved regression policy; missing or inconclusive evidence remains open.
7. The report includes source revisions, branch names, exact run instructions, configuration/data fingerprints, raw measurements, differences, and resource observations.
8. Index format/rebuild instructions, ownership/lifetime requirements, supported toolchain/dependencies, and source notices are documented.
9. No default-branch merge is performed without explicit authorization. Library-only demonstrations are not a substitute for either migration.

## Out of Scope

- Dynamic insertion, deletion, or coordinate updates.
- Persistence for the in-memory index.
- Compatibility with the consumers' historical index file formats.
- A separate flat-file/mmap index backend in the first version; mmap as an internal B+ tree access mechanism is an engineering choice, not a third index type.
- Arbitrary custom distance metrics, other numeric representations as a broad generic feature, or a general plugin/scripting runtime.
- H-style sorting and batch distance evaluation of pending candidates.
- Automatic parallelism within a single query, distributed indexing/query services, and GPU acceleration for the new core.
- Rewriting the consumers' application algorithms, unrelated baselines, sampling methods, or command-line products.
- New algorithmic improvements mixed into extraction. Performance work must preserve the accepted search semantics and pass the accuracy gates.
- Automatic exhaustive fallback to fill top-k results.
- Proof that every possible user-supplied strategy terminates or fits future APIs without any engine extension.
- Automatic merge or release of the migration branches.
- Implementation, branch creation, commits, pushes, or benchmark execution during this specification-writing task.

## Further Notes

### Baselines and deliberate differences

- Library repository starts from commit `89797d839e56f9198489ad880231bf68d858bb88` with README and MIT license; design documents were added during discussion. The earlier premature implementation was moved out of the repository and is not an approved design or source baseline.
- qalsh4c baseline: `266118b6851a30f5dfc7ba80b00760e887578f47`.
- qalsh-h baseline: `a8c602605661ebc64e19e535f19424982e58e2c0`, on `refactor/qalsh-h-config`. This includes the modifications the user explicitly asked to retain and commit.
- The latest requirement expands eventual delivery to actual consumer migration branches. Earlier instructions not to edit consumers governed the discussion/prototype phase; this task remains specification-only, and branch work belongs to a separately authorized implementation phase.
- Already approved differences from legacy H are immediate per-candidate evaluation instead of sorted batches, and deterministic distance/ID tie ordering. These are not permission to lose accuracy or evade the performance gates.
- The required 4C disk migration changes index structure from mmap-backed flat arrays to the selected B+ tree backend. This is a material compatibility/performance risk, not a reason to exclude 4C disk from acceptance.
- Existing legacy scan completion/check timing, H deferred-threshold equality, and empty-pending termination behavior deserve explicit boundary fixtures. Discovering a legacy bug does not authorize silently changing behavior; report it separately.

### Algorithm references and provenance

- Primary L2 reference: Huang et al., *Query-Aware Locality-Sensitive Hashing for Approximate Nearest Neighbor Search*, PVLDB 2015, especially Algorithm 1 and Sections 4.2, 4.3, and 5.3: https://www.vldb.org/pvldb/vol9/p1-huang.pdf
- The paper specifies collision qualification, distance/budget termination concepts, top-k termination changes, and parameter formulas. It also describes radius skipping; the agreed first-version geometric schedule is therefore a documented implementation profile, not an exact reproduction of every paper optimization.
- Author reference repositories: https://github.com/HuangQiang/QALSH and https://github.com/HuangQiang/QALSH_Mem. GitHub license metadata was checked and identifies both as GPL-3.0. Use the publication to document algorithms and compatible-provenance consumer material for implementation; do not assume the authors' source is MIT.

### Review and publication status

- The high-level test seams and proposed numerical performance budget need confirmation in specification review. No further broad requirements interview is needed.
- Tracker configuration and the project's triage vocabulary have not been provided. Run `/setup-matt-pocock-skills` before publishing this specification to the configured issue tracker with `ready-for-agent`. No issue has been created or labeled by this task.
- No implementation tests or performance experiments have been run for this specification. The acceptance plan is not evidence that the library or migrations already work.
