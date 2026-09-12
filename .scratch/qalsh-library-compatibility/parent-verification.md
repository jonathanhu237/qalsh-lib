# Parent repair and verification after review 3

Status: three-review loop complete; not accepted

The parent took over R1/R2 after the two delegated fix attempts. This is direct
repair and verification under implement-loop, not a fourth formal review.
The final source map is `current-source-fingerprint-parent.json`; only the
library header, implementation and dispatch regression test changed from fix2.
The complete three-repository source map remains recorded, and the pinned
specification, ADR and CONTEXT hashes are unchanged.

## R1: focused L1 regression repaired

A new profile repeats the full Federalist query population30times after one
open. It collected2,970 cycles samples with zero lost samples; about35.8% was
in memory scanning and35.5% in the typed default range driver. It is a
profiling-only workload, not the primary one-population comparison. Raw data
and report are in `parent-evidence/profile/`.

The private built-in collision handler repeated the ID check already guaranteed
by immutable final built-in indexes at Build/Open. Parent removed that repeated
check and added explicit validation at the external-index callback adapter.
Public strategy handling and candidate evaluation retain their checks. An
external malformed-hit regression exercises typed default, base-erased default,
and a custom strategy declining all candidates. Before the adapter fix the
new test failed; afterward all four library tests passed. Public custom
strategies are now protected even when they decline a malformed hit.

Ten alternating baseline/current pairs at each thread count, one warmup per
variant, the frozen baseline and current optimized harness, affinity20–23:

| Case | Baseline median ms | Current median ms | Geometric ratio | Bootstrap95% interval |
| --- | ---: | ---: | ---: | --- |
| Federalist L1 A→B memory T1 |92.753848|89.273106|0.96855058|[0.95686715,0.98625518]|
| Federalist L1 A→B memory T4 |24.9238395|24.220496|0.97212180|[0.96672827,0.97832404]|

Statistics follow the predeclared20,000-resample paired-log-bootstrap protocol
with seed20260912. No run or outlier was removed. Parent independently
recomputed the raw pairs and all20 quality comparisons after copying the
artifacts locally: zero ID/order/count changes,158 distance-only differences
per comparison. These intervals resolve the focused timing regression; they
do not establish no regression for every dataset, backend or control mode.
No positive slowdown allowance was adopted.

Raw commands, binaries/source fingerprints, TSVs, stdout and statistics are
in `parent-evidence/boundary-t1/` and `boundary-t4/`.

## Final-source regression tests

Centaurus Linux Docker, GCC15, image qalsh-cxx15-deps:latest
(digest sha256:c78838db4d5f8aa1c52328c13847a377d5c25658316192eecab64b5e3fe24e7c):

- Library4/4, including the new malformed external index test.
- C23/23, with its real data fixtures.
- H1/1, in Linux Docker.
- Source-tree consumer1/1 and freshly reinstalled package consumer1/1.

Logs are under `parent-evidence/parent-*-build*.log`.
The first query-rebuild command omitted the validation-source mount and failed
at CMake regeneration; the corrected command restored that mount and succeeded.
This setup failure is retained and is not counted as a source test failure.

## R2: direct coverage additions

`parent-tools/final_coverage.py` freezes the declared cases, records source and
binary hashes before/after, writes each full per-query TSV, and instruments a
separate resource run using native GNU time plus sampled Linux RSS components.
All runs are serial with affinity20–23 in Docker. These are single-run
quality/resource observations, not repeated performance acceptance.

Completed C coverage:

- Federalist24 cases: L1/L2 × A→B/B→A × T1/T4 × memory/B+/array.
  All retain baseline ordered IDs/counts. Distance-only differences remain.
  The28 within-current backend/thread comparisons (including four H toy
  backend comparisons) are exact in `parent-evidence/backend-equivalence.json`.
- MNIST full A→B3,000queries and B→A67,000queries, both L1/L2 on disk:
  all four comparisons match baseline IDs, order, count and float32 distances
  exactly. The former3,000-query reverse-only limitation is removed here.
- H toy8 default/complete × k1/k100 × B+/array cases retain baseline ordered
  IDs/counts, with distance-rounding differences. Both current layouts agree
  exactly. Two further m32/beta0.01 override cases are recorded; complete mode
  exposes a rank100 equal-distance membership discrepancy (query39), documented
  in `parent-evidence/tie-diagnosis.md`.

GIST B+ coverage is complete for100queries on1,000,000points of960dimensions,
k1/k100 and default/complete-radius. Both k1 cases retain ordered IDs, with
rounding differences. For k100 default,51queries reorder tied neighbors with
identical ID sets. For k100 complete,58queries similarly reorder tied neighbors
and2queries change membership at an equal-float32-distance cutoff:
query30 replaces970785 with9010 (distance1.1136281490325928), and query36
replaces391088 with391064 (distance1.7872439622879028). The paired reordered
IDs are tied in both the baseline and current outputs. See
`parent-evidence/h-tie-audit.json` and the raw quality records.

All42cases /84commands finished successfully. All returned the requested count;
source/binary hashes were unchanged throughout. Per-run native peakRSS,
anonymous/file-backed samples, open/query stdout and full results are retained
in `parent-evidence/final-coverage/`. Instrumentation and first-open page
validation mean these are not cold-I/O or paired-performance claims.

## Remaining acceptance gates

N1 is unresolved: exact distance differences and equal-distance neighbor
ordering/selection must not be silently waived. The concrete H override case
has different selected IDs, so an unconditional identical-ID claim would also
be false. The independent mathematical reference supports the newer distance
arithmetic, but does not settle compatibility policy.

R2 is still incomplete as a full acceptance matrix: the additional cases do
not provide repeated paired timings for all backends/data/modes; GIST sorted
array and overrides, remaining full MNIST modes/layouts, exact-scan controls,
and complete final-source build/open/storage/resource comparisons remain.
Earlier CLI and seeded projection evidence is retained with its original
source provenance rather than relabeled as a new final-source run.

No code commit, push or merge was made. Existing uncommitted changes and
consumer branches were preserved. Three formal reviews are complete; the
work is reviewable but not unconditionally accepted.
