# QALSH query-performance optimization — fixed implementation spec

**Status:** fixed for this implementation/retest round (2026-09-12)

## Objective and boundary

Optimize steady-state QALSH query execution in `qalsh-lib` while preserving
query quality. The tracked source baseline is immutable commit
`722ecfbf8b426a3148d30f9cd6b393018bf31fab` (the current clean tracked
checkout before this round). The original C/H consumer sources and all
previous comparison artifacts under `.scratch/` are frozen evidence and must
not be overwritten or cleaned. This round may change the library and its
focused tests/tools only; it must not change public semantics, relax search
work, reduce query/data populations, lower projection/candidate parameters,
or change unrelated consumer code to obtain a timing gain. No commit,
push, or merge is part of this round.

The optimization target is the complete query path after one index open:
projection of the query, resumable table traversal, strategy dispatch,
point access, distance evaluation, deduplication, top-k maintenance, and
result formation. Build, open, memory/file size, and full-command costs are
separate observations and must be reported, not traded away silently.

## Frozen acceptance matrix

Freeze these scenario families before any timing run; a missing or failed case
is a coverage gap, not a pass. Use the already available datasets and exact
truths only; do not invent missing truths.

* **qalsh4c:** Federalist (A=1880, B=1178, d=300) and available MNIST
  (A=3000, B=67000, d=784); L1 and L2; A→B and B→A where a truth file is
  available; `both` where supported; in-memory and persistent B+ (default)
  plus sorted-array diagnostic comparison; k=1; fixed production thread
  counts T1 and T4; QALSH-based weights/estimators including uncached and
  cache-hit controls where the existing workflow provides them.
* **qalsh-h:** frozen toy data (n=10000, 100 queries, d=256) and available
  GIST data (1,000,000 points, d=960, frozen first 100 queries); L2;
  default and complete-radius modes; k=1 and k=100; B+ default and sorted
  array; the declared m/beta override cases; the exact linear-scan control.
* **library controls:** deterministic bounded fixtures covering L1/L2,
  repeated queries, equal projections/distances, duplicate coordinates,
  zero-distance and boundary-window cases, partial results/exhaustion,
  candidate duplicates/deferred IDs, and independent concurrent queries.

The representative performance population is the complete declared query
population for each selected real scenario, in a fixed order. Any restricted
population (for example the frozen GIST first 100) is named in the result and
used identically for both variants. Effective metric, projections, m,
beta/candidate settings, radius schedule, scan quantum, k, layout, thread
count, direction, cache policy, and query order are fixed before measurement.

## Quality gate and independent reference

For every scenario, compare the current implementation with **both** the
original frozen consumer baseline and the pre-round `722ecfb` implementation;
quality is judged per scenario, never by an overall average. Record full
ordered IDs, counts, returned distances, termination reason, evaluated
candidate/hit counts, and application outputs where applicable.

Validate returned distances and top-k quality with an independent exhaustive
nearest-neighbor reference implemented in the harness using a simple
straight-line double-precision accumulation. It must not call or share the
library distance kernel, projection code, candidate strategy, or result
maintenance. For each returned entry, the reference distance check uses a
predeclared float tolerance of
`max(8 * epsilon_float * max(1, abs(reference)), 1e-5)` (and the corresponding
L2/L1 double result is rounded only for comparison); this covers final
float32 representation and bounded accumulation without accepting substantive
errors. A failed distance check is a failure, not a tolerance-adjustment
prompt.

Recall@k is tie-aware: an answer at the exact reference cutoff may be any
member of the cutoff equivalence class, and IDs at equal reference distance
are not treated as a quality loss. Report ordinary ID overlap and tie-aware
overlap separately. Approximation/distance Ratio and error are computed from
the fixed reference and must be no worse than either baseline in every
scenario (within the above numerical tolerance); no mean may hide a query or
rank loss. Returned count must equal the requested k for complete results (or
match the documented partial-result contract), and every returned distance
must pass the independent check. Same-work scenarios additionally require
ordered IDs and counts to match except for the declared equivalent-tie rule.
The historical strict byte-for-byte N1 issue remains historical evidence and
is not rewritten as passed under this tie-aware policy.

## Frozen timing/statistics protocol

Use optimized, non-sanitized builds with matched compiler, flags, architecture,
dependencies, feature macros, affinity, thread count, dataset/query order,
and container/host. Confirm actual OpenMP/SIMD feature definitions rather
than trusting link metadata. Open one index per measured process, warm each
variant once, prepare the worker/query state outside the timed region, then
time the full declared query population. Startup, open, output formatting,
build, and cache preparation are outside query latency and recorded separately.
Keep warm-cache and disk-oriented runs separate; opening/validation touches
pages and is not a cold-query claim.

For each baseline/current and current-B+/current-array pair, collect at least
10 alternating AB/BA paired runs without discarding inconvenient pairs.
Repeat the complete population identically within a batch only if necessary
to dominate timer noise. Primary observations are all absolute query times,
per-pair current/baseline ratios, medians, tails, and the geometric mean of
paired ratios. Before inspecting values, use a percentile paired bootstrap on
log ratios (20,000 resamples, deterministic seed 20260912, 95% interval,
exponentiated back to ratios). An interval wholly below 1 supports an observed
improvement; wholly above 1 is evidence of slowdown; overlap with 1 is
inconclusive. There is no approved positive slowdown allowance. Report local
regressions even when the cross-scenario geometric mean improves.

Record source, binary, harness, dataset, query, truth, projection, and
parameter fingerprints in every result directory, plus machine/container
identity, affinity, competing workload, cache state, build time, open time,
index bytes, peak RSS with anonymous/file-backed components, and concurrency
observations. Bind every result to the final source/binary fingerprint; prior
`.scratch/qalsh-library*` measurements are historical and cannot be relabeled
as current evidence.

## Iteration and verification

Before implementation, preserve the fixed spec and confirm the current
hotspots against the current source. Make the smallest semantics-preserving
change, run focused tests and optimized builds, then perform the allowed
Centaurus sync/run for resource-intensive validation. Run the complete library
CTest suite once at the end; run consumer/matrix cases that the environment
supports and report all omissions or blockers. Never modify the frozen
original sources, old results, or this protocol to fit observed results.
