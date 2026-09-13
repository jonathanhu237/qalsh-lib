# Fixed query-performance round: final implementation report

Date: 2026-09-12/13.  Baseline: clean `HEAD`
`722ecfbf8b426a3148d30f9cd6b393018bf31fab`.  This report is deliberately
not a performance-acceptance claim: the quality and compatibility gates pass,
but the primary paired timing gate did not establish an improvement. No Git
commit, push, merge, or consumer-source edit was made.

## Library changes

Only the library and its focused public test were changed:

* `SearchEngine::search_impl_typed` computes `complete` before result
  construction and moves the per-query neighbor vector into `SearchResult`
  instead of copying it. This is safe after the search loop because no
  callback can observe the runtime after result formation.
* `InsertNeighbor` has a `k == 1` replacement path and a tail rejection before
  `lower_bound` for a full result set. Both retain the existing distance-then-ID
  ordering; the general insertion path remains unchanged otherwise.
* Large in-memory indexes retain a projection-only `values` partition cache
  (enabled at 4096 points) for `lower_bound`; small indexes use the original
  interleaved entries. The scan loops retain table/query values in locals.
* `tests/public_test.cc` exercises the large in-memory cache path and verifies
  complete ordered top-k output.

The final tracked source SHA-256 values are:

* `include/qalsh/qalsh.h`: `bfd733a8a23ef3a77a3ba0ad3586bd743f06154c16ea6224017e651138caacd1`
* `src/qalsh.cc`: `c2052a2102c413a9e9d52a154aba77955120fd2751e25a5cb2f072cb6d305c4f`
* `tests/public_test.cc`: `fa63d2b10b0d5d37fd6ecb57a5e92689b2f2fa47c36aac39d84eb0545d28635`

The complete provenance binding is in
`final-source-fingerprint.json`; historical scratch evidence was preserved.

## Verification and quality

* Library GNU 15.2.0 Release build: CTest **4/4 passed**. The local
  AppleClang distance-test abort is a pre-existing clean-HEAD failure; it is
  not used as authoritative evidence.
* Final C consumer build against this library: **23/23 CTest tests passed**,
  including `qalsh4c_direction_estimate` and
  `qalsh4c_qalsh_weights_cache`.
* Final H consumer build: strategy test **1/1 passed**; the final query
  consumer also built and passed a Federalist/toy smoke run.
* Federalist C coverage includes L1/L2, AB/BA, memory/B+/sorted-array, T1/T4.
  MNIST includes L1/L2, AB/BA, memory/B+/sorted-array, T1/T4. H coverage
  includes toy and first-100 GIST queries, k=1/100, complete and incomplete
  modes, B+/array, overrides, and exact controls.
* The independent C++ validator uses straight-line double accumulation and
  tie-aware reference checks. Current-vs-HEAD Federalist L1/L2 AB/BA runs
  have zero count/distance failures and zero quality regressions:
  `evidence/independent-final/c-fed-{l1,l2}-{ab,ba}.json`.
* MNIST direct distance validation has zero failures. Maximum L2 errors are
  `0.0001218761` (AB) and `0.0001220488` (BA), below the recorded tolerances
  `0.0025606641` and `0.0030902717`; see
  `evidence/final-rerun-c-mnist/distance-validation-summary.json`.
* H GIST B+ and sorted-array truth checks have zero count/distance failures
  for k=1/100 and complete=0/1. H toy, override, and exact controls also
  pass their independent checks. Historical strict float-output differences
  against the original consumers are retained rather than relabeled as
  passes; returned IDs and direct distances are validated independently.

## Paired timing

All primary pairs used optimized matched GNU builds, affinity `20-23`,
`OMP_PROC_BIND=true`, `OMP_PLACES=cores`, one warmup population, alternating
order, all 10 pairs, and the fixed 20,000-resample log-ratio bootstrap with
seed `20260912`. `query_ms` excludes open/startup/output. The Federalist
repeat harness uses 30 complete repetitions, so its `query_ms` is explicitly a
diagnostic aggregate over all repetitions.

| pair | baseline median ms | comparison median ms | geometric ratio (95% interval) | result |
|---|---:|---:|---:|---|
| C Federalist L1 AB T1 memory, HEAD/current (30 repetitions) | 2675.03 | 2716.12 | 1.01238 (1.00805, 1.01685) | slowdown |
| C MNIST L2 AB T1 memory, HEAD/current | 4851.04 | 5147.27 | 1.05816 (1.00762, 1.11232) | slowdown |
| H toy k=100 complete=1 B+, HEAD/current | 177.92 | 185.85 | 1.04649 (1.02974, 1.06881) | slowdown |
| H toy k=100 complete=1 array, HEAD/current | 176.38 | 184.41 | 1.04247 (1.03344, 1.05149) | slowdown |
| H GIST first 100 k=100 complete=1 B+, HEAD/current | 35096.50 | 35579.00 | 1.00973 (0.99798, 1.02011) | inconclusive |

Evidence: `evidence/paired-final-head-current-fed`,
`paired-final-head-current-mnist`, `paired-h-toy-head-current`,
`paired-h-toy-array-head-current`, and `paired-h-gist-head-current`.

Current B+/array controls are separate layout observations, not mixed with
HEAD/current claims:

* Federalist L1 AB T1: ratio **0.94967** (0.94754, 0.95192), improvement.
* MNIST L2 AB T1: ratio **0.94894** (0.90552, 1.00045), inconclusive.
* H toy k=100 complete=1: ratio **0.98839** (0.97918, 0.99807), improvement.

The no-`InsertNeighbor`-k1 diagnostic still measured a Federalist ratio of
**1.01320** (1.00546, 1.02220), so the observed regression is not attributed
to that fast path alone. There is no positive slowdown allowance; therefore
the final timing gate is **not accepted** despite quality passing.

## Separate build/open/resource observations

A direct library control (`build_open_bench.cc`) measured build and open
separately with point data loaded before the timed build:

| dataset/config | existing B+ open | in-memory build | B+ build / open | array build / open |
|---|---:|---:|---:|---:|
| Federalist L1 B+ | 0.978 ms | 9.780 ms | 13.184 / 0.890 ms | 12.417 / 0.588 ms |
| MNIST L2 B+ | 23.228 ms | 938.846 ms | 962.542 / 24.086 ms | 959.611 / 6.653 ms |

Representative final resource files keep full-command wall time, reported
steady-state query time, peak RSS, and anonymous/file-backed components
separate. Examples (current variant):

| case | full wall | open/init | query | peak RSS (anon + file) | index bytes |
|---|---:|---:|---:|---:|---:|
| Federalist L1 AB memory | 112.3 ms | 7.26 ms | 90.6 ms | 9.8 MiB (4.7 + 4.9) | in-memory |
| MNIST L2 AB memory | 5.652 s | 613 ms | 5.000 s | 268.9 MiB (263.8 + 5.1) | in-memory |
| MNIST L2 AB array | 4.798 s | 8.65 ms | 4.771 s | 48.9 MiB (10.1 + 38.9) | 117.6 MB |
| H toy k=100 B+ | 204 ms | 3.86 ms | 185.5 ms | 21.4 MiB (1.1 + 20.5) | 6.12 MB |
| H GIST k=100 B+ | 36.091 s | 560 ms | 35.354 s | 4279.2 MiB (72.2 + 4207.3) | 566.7 MB |
| H GIST k=100 array | 35.937 s | 163 ms | 35.625 s | 4284.6 MiB (84.1 + 4201.0) | 560.3 MB |

The full resource matrices are in `evidence/final-resources-*`; they include
T1 representative controls and both H layouts. File-backed RSS on GIST is
mmap-backed index residency, not anonymous allocation. The cache-enabled
MNIST memory observation is intentionally reported (higher peak anonymous
RSS) rather than traded against the timing result.

## Final disposition

The implementation is buildable, compatible, quality-preserving under the
independent tie-aware checks, and fully self-tested. The measured fixed
HEAD/current query gate is slowdown/inconclusive across the representative
cases, so this round must not be described as an accepted query-performance
improvement. The worktree is intentionally left uncommitted for the user to
choose whether to retain, revise, or revert the optimization after this
negative timing result.
