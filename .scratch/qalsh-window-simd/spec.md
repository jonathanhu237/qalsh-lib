# SIMD window-prefix search — fixed candidate specification

Status: ready for implementation

## 1. Authority, baselines, and scope of this round

The user authorized a Spec and a fresh Luna Max implementation of the window-clipping candidate identified by the bottleneck diagnosis. This is a new, bounded candidate round, not a reopening or relabeling of the failed optimization bundle.

Fixed cumulative code-review baseline: `722ecfbf8b426a3148d30f9cd6b393018bf31fab`. Tracked production files are clean at publication. Keep this specification and the review baseline fixed through all reviews.

User quality policy remains: no per-scenario retrieval-quality degradation relative to either original C/H or 722ecfb; independent exact reference, equivalent ties allowed, reasonable justified float32 encoding tolerance, valid counts/distances. User performance policy remains: query speed first, overall improvement over BOTH baselines, local regressions individually disclosed, build/open/memory costs separately reported. An optimization must not reduce query populations, change search parameters, or use looser quality gates to win.

Original source identities are the audited snapshots in `.scratch/qalsh-library-compatibility/original-baseline-source-audit.json`: C `266118b6851a30f5dfc7ba80b00760e887578f47`, H `a8c602605661ebc64e19e535f19424982e58e2c0`. Match actual source/binary fingerprints; legacy directory labels alone are not provenance.

This round must deliver a narrowly scoped implementation or a reproducible rejection of this candidate. Passing its focused screening is NOT full-project acceptance. The broader frozen quality/performance matrix in `.scratch/qalsh-query-performance/spec.md` remains the full-project gate; unfinished parts must remain explicitly open. Do not start that large matrix before the focused candidate works.

## 2. Evidence and falsifiable hypothesis

Primary evidence: `.scratch/qalsh-bottleneck-diagnosis/findings.md`, `work-probes/report.md`, `work-probes/raw.json`, and `evidence/`.

Federalist L1 AB memory T1 (1880 queries, 1178 base points, d300) has the same 37,722,223 hits, 37,636,159 collision updates and 1881 distance evaluations in original C and 722ecfb. Current has 1,114,661 ClampToWindow calls, 927,698 binary searches (83.23%), 6,332,792 binary iterations; accepted range median17, mean33.84, available mean117.80. Fresh current cycle samples locate40.04% in memory scanning and27.33% in hit delivery. Merely tuning top-k is low leverage here.

Hypothesis: SIMD processing of a contiguous sorted outward range can find its in-window prefix with lower serial branch/load cost than scalar endpoint-plus-binary probing in partial/short windows. It can also lose by reading more data or mishandling full windows; those cases must be measured, not hidden. SIMD width or instruction-count reductions alone are not success evidence.

## 3. Allowed changes and non-goals

Allowed production surface: `src/qalsh.cc` around `ClampToWindow`, an optional small private internal helper/header, focused tests and minimal CMake test/feature guards. Preserve existing installed API/ABI expectations and persistent formats. No public strategy knobs or H-specific core behavior. Keep SIMD decisions private to the implementation.

Explicitly out of scope: top-k rewrites, result-vector moves, projection-only partition caches, new scan scheduling, point-data layouts, H visited-state layouts, candidate batching, distance kernels, projection arithmetic, search parameters, public callback semantics, consumer-source changes, and unrelated historical cleanup. Do not resurrect the rejected `failed-optimization.patch`.

No per-query or per-range heap allocation and no persistent index-size increase for this candidate. Do not globally enable `-march=native`, AVX/AVX2, fast-math, FMA contraction, or OpenMP threading to obtain a speedup. A conservative x86-64 baseline-ISA implementation (e.g. SSE2) plus scalar fallback is a suitable first approach. Wider ISA requires correct runtime/compile isolation and evidence justifying its complexity; it is not required. Non-x86 must compile and use a correct fallback. Do not build an unnecessary SIMD framework.

## 4. Exact operational contract (stronger than aggregate quality)

For every valid range supplied by the existing scanners, return EXACTLY the prefix length returned by the current predicate:

`std::abs(query_value - projection_value) <= bound`

Use existing float32 arithmetic/rounding assumptions. Do not replace this with `projection <= query + bound` or `projection >= query - bound`: float32 boundary behavior is not generally identical. Do not change the inclusive comparison, subtraction precision, or numeric validation rules.

Preserve:

- forward and reverse order and every delivered point ID/projection;
- original scan quantum, left/right split, leaf/logical-region boundaries;
- hit accounting, empty/window/table exhaustion, cursor progression and callback exception behavior;
- candidate evaluation order, deduplication, radius/termination decisions;
- valid zero-length input without dereference;
- loads confined to the valid entry extent, including reverse traversal and vector tails;
- unaligned storage, object/aliasing rules, and existing serialized entry stride.

Do not interpret point-ID bytes as arbitrary floating-point operands with observable invalid operations. Loads may cover complete valid entries, but may not cross allocation/mapping/page-region bounds just because the extra bytes are not subsequently used.

## 5. Test-first correctness gates

Read/follow the TDD skill for this seam. Implement a simple scalar *linear prefix* oracle in tests, not by calling the new helper or copying its binary/SIMD algorithm. Test private helper behavior without expanding the public API; small private seams or separately built test objects are allowed.

Required corpus:

1. Exhaustive small sorted projection alphabets with duplicates, queries inside/outside partitions, lengths0–16, both directions and every valid alignment offset for the byte entry representation.
2. Fixed-seed randomized sorted ranges with lengths around vector boundaries and 31/32/33,63/64/65,127/128/129,255/256/257 and larger persistent logical ranges. Record the seed/case count.
3. All-outside, all-inside, first/last cutoff, single entry, short partial windows and repeated equal projections; +/-0, subnormal values, adjacent `nextafter` boundary values, finite extreme values whose subtraction can overflow, bounds0 and +infinity where valid.
4. Explicit tail/end-of-buffer tests; ASan/UBSan for the focused tests. No vector load may rely on readable padding not part of the legal range.
5. Differential public scan/strategy traces against an independently forced scalar/reference path: in-memory, B+ and sorted-array; quantum and range scope; partial/exhausted results, duplicate/deferred candidate submissions, typed/erased/external-index dispatch. Compare events and all result metadata, not only final nearest ID. Existing tests can be extended, but two paths that both use the SIMD helper do not constitute an independent reference.
6. Optimized SIMD-enabled and forced-scalar builds both pass. A private test compile definition may force fallback; avoid an installed user-facing runtime option just for testing. Verify generated code actually selects SIMD for supported builds and does not require unsupported ISA for fallback.
7. Full library CTest, relevant C and H consumer suites, and source-tree/installed consumer smoke tests at final source. Record pre-existing failures separately, with their baseline reproduction; do not omit them from the report.

This is semantics-preserving optimization: candidate-vs-722ecfb event and output equality is the first real-data gate, stronger than merely equal mean recall. Against original C/H retain the user's independent tie-aware quality policy; known historical differences are not newly waived by this Spec.

## 6. Staged measurement and frozen focused cases

### Stage A — bounded kernel screening

Benchmark reference and SIMD on the exact recorded available/accepted-length distribution where recoverable, plus explicit empty/short/half/full-range populations. Name synthetic reconstructions as synthetic; a histogram alone does not reproduce joint distributions, values or locality. Include both traversal directions and all vector tails. Protect from dead-code elimination and exclude generation/allocation from kernel timing. This screen may reject a candidate but cannot establish end-to-end improvement.

### Stage B — first end-to-end gate

Federalist L1 AB memory T1, the complete1880-query population, same parameters/projections as diagnosis. Use matched optimized uninstrumented builds. Exactly30 full populations per measured process, open once; one untimed full population warms each worker. Record both whole-batch query time and time divided by30; do not confuse this amortized diagnostic with isolated single-query latency. Verify every result population is stable or retain enough checksums to detect changes, and retain full per-query results at least for each process.

Compare all three variants: audited original C, unmodified722ecfb, candidate. At least10 measured triplets after warmup; cycle the six variant permutations in declared order (ABC, ACB, BAC, BCA, CAB, CBA), repeated to reach10. No discarded slow runs. Record candidate/722ecfb and candidate/original paired ratios from each triplet. Bootstrap paired log ratios20,000 times with seed20260912, exponentiate to95% intervals. A credible benefit requires intervals below1 against both baselines; overlap means inconclusive, not a small win. This primary requirement is a candidate screening gate, not an added demand that every full-project scenario improve.

If this gate fails or is environmentally uninterpretable, investigate at most one evidence-backed adjustment. No unbounded implementation enumeration. Archive rejected diffs/source identities and raw evidence; do not leave a demonstrated regressive candidate as a successful optimization.

### Stage C — fixed cross-path regression and candidate-coverage matrix

Only after Stage B succeeds, evaluate these16 cases, with the original and722ecfb baselines held constant:

- C Federalist memory: L1/L2 x AB/BA x T1/T4 (8 cases; k1).
- C Federalist L1 AB T1: B+ and sorted-array (2 cases; k1).
- C MNIST L2 AB T1: memory and B+ (2 cases; k1, full3000 queries).
- H toy k100 complete-radius: B+ and sorted-array (2 cases; full100 queries).
- H GIST k100 complete-radius: B+ and sorted-array (2 cases; frozen first100 queries, full1M-point base).

Original consumers have no equivalent new sorted-array layout: compare candidate array with original supported B+ and matched722ecfb array, explicitly label the cross-layout original comparison. No switching layouts to hide a slower same-layout candidate. Keep these comparisons out of claims that isolate implementation-only effects; report them as end-to-end variant comparisons.

C Federalist uses30 repetitions; MNIST and H use one complete population after one untimed population, identically across variants. Matched data, seed/projection, m/beta, k, mode, quantum, threads and cache preparation. At least10 triplets per case using the Stage B ordering/statistics. The primary within-this-matrix aggregate is the equal-case-weight geometric mean of per-case paired ratios, computed separately against each baseline. Report all16 ratios, intervals and both aggregate intervals; no omitted adverse case or mean recall hiding local quality loss. Bootstrap within cases for the aggregate, retaining its dependence structure when timing sets are shared. State any remaining dependence limitation.

Collect final-bound exact-current/722ecfb result/trace equivalence and independent original-baseline quality checks, plus build/open/file-size/peak-RSS observations separately from query timing. Reuse prior truth/evidence only after checking source, data, parameters and population provenance. Old truth tools using float32 cutoff classes or self-comparisons are not sufficient for the precise independent quality gate; fix or explicitly leave that gate open, never silently widen it.

A16-case candidate result is not the entire broader matrix in the previous Spec. Clearly separate 'candidate validated in focused matrix', 'quality unresolved', 'environment blocked' and 'full-project acceptance'. Do not declare the overall user objective complete while broader gates remain uncovered.

## 7. Environment and evidence integrity

All code and Git operations local; rsync one-way to isolated Centaurus work/build paths and run there. Preserve original sources,722ecfb snapshots, old artifacts and other users' changes. New tools through mise when needed. If Centaurus is unavailable, report it; no local heavy fallback.

Before timing, record physical-core/SMT topology, affinity, governor/frequency observations, competing workload and sibling occupancy. CPU20 has siblingCPU8; affinity20 alone was insufficient in diagnosis. Choose the least busy suitable physical core/core set using a preflight occupancy sample BEFORE inspecting candidate timing. Do not kill, suspend or re-affinitize existing jobs, change global governors, disable SMT or drop global caches without user permission. For T4 use distinct physical cores, not sibling threads of the same core.

Record per-run CPU occupancy on benchmark cores and their siblings, task-clock/cycles/instructions where available, context switches and effective frequency; counters from a separate diagnostic run must be labeled separately from primary query times. Flag a triplet as contaminated if any selected sibling is >5% busy over a run or paired effective frequency differs by >5%. Keep all samples, even contaminated ones, in primary statistics. If >20% of triplets in a case are contaminated, its performance acceptance is environment-blocked regardless of a favorable interval. Do not keep retrying until a lucky batch passes. Request a reserved physical core/time window instead. These thresholds are screening controls, not proof the remainder is perfectly isolated.

Bind results to source/patch, binary, compiler/flags, harness, dataset/query/truth/projection/parameter hashes, host/container identity, actual commands and exit status. Reject same-file baseline/current comparisons (including aliases); identical bytes in genuinely separately produced files are valid. Do not relabel instrumented times or older candidate artifacts as current acceptance evidence. Full failed attempts remain inspectable.

## 8. Deliverables and verification ledger

- Narrow production diff, or archived rejected candidate with production restored safely.
- Focused oracle/property/tail/trace tests and fallback coverage; real test logs.
- `implementation.md`: exact change, mechanism, generated-ISA check, all attempts, per-gate status and remaining blockers.
- Frozen measurement plans, per-case raw outputs/commands/provenance, statistics and an explicit case x baseline x quality/timing/resources coverage ledger. Repeated output equality alone is not independent original quality validation.
- `source-fingerprint.json` binding FINAL source/builds/tools and the unchanged Spec.
- User-facing report distinguishing measured improvement, inconclusive results and unresolved global acceptance.

No commit/push during implementation or fix attempts. Parent performs direct Standards/Spec reviews against722ecfb (working diff and new files included), at most three rounds, with fresh Luna Max for every implementation/fix attempt. Initial counts: reviews0, fix attempts0 for all new issues. If an issue survives two delegated fixes, parent takes it over and verifies any repair; unavailable resources or an unsupported performance hypothesis must be reported rather than disguised as success.

Blocked stop: stop with the tested hypothesis, retained evidence, failed/untestable gate, production disposition and exact next input needed. An environment-blocked benchmark is not evidence that the code is slower; a passing correctness suite is not evidence it is faster.
