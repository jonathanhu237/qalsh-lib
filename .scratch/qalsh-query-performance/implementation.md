# Query-performance fix 1 / attempts 1–2 — final implementation record

Date: 2026-09-13. Fixed baseline: `722ecfbf8b426a3148d30f9cd6b393018bf31fab`.
The fixed implementation scope was preserved: no consumer sources, spec, or
frozen historical evidence were changed; no commit, push, or merge was made.

## Disposition

The measured production optimization was withdrawn after the review-1 timing
result. The saved patch is:

* `.scratch/qalsh-query-performance/failed-optimization.patch`
* `.scratch/qalsh-query-performance/failed-optimization.json`
* SHA-256: `629e59222340ebe8a4d47010de3c5c4dcf812725de1ee5d3b9e9932543291f81`

Only that selected three-file production diff was reverse-applied. The final
production files are byte-identical to the fixed baseline:

| file | final SHA-256 |
|---|---|
| `include/qalsh/qalsh.h` | `aa6ed23650f323867d8276ee283ef8bdbdeab3f046ee9232884a92818be25c41` |
| `src/qalsh.cc` | `7499f1e3952bdf07184336f368db5b843c795226bec8834365662f94ca40c918` |
| `tests/public_test.cc` | `c21c6e20e4c1148a5d529714f29fc285cad782928a79ee162299955a70d37f2c` |

The withdrawn optimization's former report and fingerprint remain available
as historical records at `failed-optimization-implementation.md` and
`failed-optimization-source-fingerprint.json`; they are not final evidence.

## R3 comparator correction

`.scratch/qalsh-query-performance/exhaustive_quality.cc` now removes
`ordinary_hits` from `NoWorse`. Ordinary ID overlap remains printed as a
diagnostic, while tie-aware cutoff membership, ratio/error, returned-distance
validity, and per-rank exact distances remain acceptance predicates.

The comparator has an explicit `--self-test` fixture mode. It creates and
checks:

* an equal-distance cutoff substitution (ordinary overlap decreases, but the
  tie-aware comparison passes);
* a genuinely worse non-tied answer (comparison fails);
* wrong result count;
* duplicate and non-contiguous/missing IDs;
* out-of-range IDs;
* incorrect distances; and
* non-finite distances.

Local validation:

```text
c++ -std=c++20 -O2 -Wall -Wextra -pedantic exhaustive_quality.cc ...
exhaustive_quality self-tests passed: tie, quality, count, ID, distance
```

The remote GNU 15.2.0 validation is recorded in
`r3-remote-validation.log`: the library CTest suite passed 4/4 and the same
R3 self-tests passed. The comparator is a scratch validation tool and is not
part of the installed library.

## Verification

The final source was synchronized one-way to Centaurus before the remote
build. A clean Release build of the final baseline library passed:

```text
100% tests passed, 0 tests failed out of 4
```

The build included `qalsh_public_test`, `qalsh_dispatch_test`,
`qalsh_distance_test`, and `qalsh_build_test`. No post-rollback heavy matrix
was started. At the initial remote check, PID 416377 was still running the
previous session's long-lived `/tmp/old_algo` job; it was not killed or
restarted.

## R1 result

R1 remains unmet/blocked, not accepted. The five fixed HEAD/current primary
measurements from the withdrawn optimization were slowdowns or inconclusive:

| scenario | paired geometric ratio | 95% interval | interpretation |
|---|---:|---:|---|
| C Federalist L1 AB T1 memory | 1.01238 | 1.00805–1.01685 | slowdown |
| C MNIST L2 AB T1 memory | 1.05816 | 1.00762–1.11232 | slowdown |
| H toy k=100 complete=1 B+ | 1.04649 | 1.02974–1.06881 | slowdown |
| H toy k=100 complete=1 array | 1.04247 | 1.03344–1.05149 | slowdown |
| H GIST first 100 k=100 complete=1 B+ | 1.00973 | 0.99798–1.02011 | inconclusive |

These measurements remain historical and are not bound to the final baseline
source. Layout controls that compared B+ with sorted arrays do not establish
an implementation improvement. The complete dual-baseline timing matrix was
not completed; see `r2-coverage.md`.

## R2 result

R2 is explicitly unresolved. Existing results are retained, but the final
source/binary/data/truth provenance requirements are not met for a complete
scenario-by-baseline matrix. The exact coverage and blockers are recorded in
`r2-coverage.md` and `r2-coverage.json`; no historical pass is relabeled as a
final pass.

## Fresh candidate screen (attempt 2)

A second bounded screen targeted two independent, profile-supported hotspots;
all candidate files were temporary Centaurus copies and no production source
was changed. The early-stop candidate for the built-in default range path
returned the same synthetic checksum but increased stable instruction count by
3.52% (1,660,458,858 to 1,718,852,353). The in-memory lower-bound candidate
also returned the same reset-fixture checksum, but increased instructions by
3.01% and `perf stat` cycles by 14.06% (2,107,834,742 to 2,404,175,815).
The forced mask variant was worse. Neither candidate was promoted to the
real-data matrix. Full commands, temporary source hashes, counters, and the
variance caveat are recorded in `attempt-2-screening.md`.

The scout's range-traversal outlining proposal was not implemented: after the
focused candidates were rejected, adding a per-range callback had no
supporting evidence and would have expanded the screen without a defensible
acceptance path.

## Final source binding

`final-source-fingerprint.json` is the current final fingerprint. It binds the
final production source to the fixed baseline and records the R3 tool hash,
remote validation binary hash, withdrawn patch, attempt-2 candidate screen,
and the fact that historical optimized evidence is excluded from final
acceptance. The old stale fingerprint was preserved separately rather than
overwritten.

## Final status

* Production implementation: restored to fixed baseline.
* R3 comparator policy/fixtures: fixed and validated.
* R1 performance gate: **not met**.
* R2 complete quality/coverage/provenance gate: **unresolved**.
* Git commit/push: intentionally not performed.
