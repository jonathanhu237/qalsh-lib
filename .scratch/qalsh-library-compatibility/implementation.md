# QALSH library compatibility implementation log

## 2026-09-12 — implementation child

This pass starts from the preserved uncommitted implementation snapshot. The
library currently has the shared search engine, in-memory arrays, and persistent
B+ trees, but no persistent sorted-array layout or backend auto-detection. I
will add a small public persistent-layout contract, implement the sorted-array
file and common engine dispatch, update both consumer integrations to build and
open through the auto-detecting entry point, then add public lifecycle tests and
run focused/full validation. I will preserve the existing B+ format behavior,
search semantics, consumer branches, and all historical evidence. Heavy
Centaurus workloads will be announced before they start; no commit or push is
part of this pass.

The orchestration seam is explicit: a library-owned `TableScanSchedule` will
own ordinary projection-table queueing, logical window completion, cursor
exhaustion, and radius requeueing. The default strategy and H strategy will
both use it. H retains only its likelihood state, deferred candidates, and
algorithm-specific boundary and termination decisions; deferred IDs continue
through the ordinary evaluation action.

## Progress

- Added `IndexLayout`, `PersistentBuildOptions`, `PersistentIndex`, and the
  validated mmap-backed `SortedArrayIndex`. Persistent files retain actual
  projection vectors and identify their layout through distinct format magic;
  `PersistentIndex::Open` autodetects B+ versus sorted-array.
- Added `TableScanSchedule` to the library and migrated the default strategy
  and H's likelihood/deferred strategy to it. H no longer owns a duplicate
  table queue or table exhaustion ledger.
- Updated qalsh4c and qalsh-h persistent build/open paths and index CLIs with
  the sorted-array option. Existing consumer-side metadata records the choice
  so a requested rebuild layout cannot be mistaken for an existing one.
- Local focused results: library 4/4 tests, H strategy test, and qalsh4c 23/23
  tests pass. The equal-projection boundary probe initially found B+ descent
  selecting a rightmost equal-key leaf; strict separator descent now preserves
  the leftmost lower bound and the probe matches memory/B+/array exactly.
- Added the equal-projection case as a permanent public regression test. The H
  logical page translation preserves the legacy entries-per-leaf calculation:
  it maps the legacy capacity `(logical_page_size - 12) / 8` into the library's
  32-byte page envelope, which is `logical_page_size + 16` for the normal
  aligned sizes. A randomized 42-query comparison across memory, B+,
  and sorted-array layouts matched result IDs, float distances, counters,
  termination reasons, and completion flags for several k and bounded-distance
  modes. The installed library package also built and ran a small external
  consumer smoke test.
- The local C/H builds above are macOS smoke checks only. They are not counted
  as the required Linux evidence.
- Centaurus Linux/Docker evidence is now available under
  `/home/jonathanhu237/code/qalsh-library-compatibility/implementation-evidence`:
  the library Release build and CTest pass 4/4, qalsh4c Release with the
  Federalist fixtures passes 23/23, and qalsh-h Release in the required Docker
  image passes 1/1. The earlier C source-only run is retained separately and
  is invalid as full evidence because its excluded data fixtures caused six
  expected missing-data failures. Current C and H query harnesses also build
  successfully in Docker. The H test run used the `qalsh-cxx15-deps:latest`
  Centaurus container with the host GCC 15 toolchain mounted at `/usr`; the
  corresponding log is `h-gcc-release.log`. Library and C Release logs are
  `lib-gcc-release.log` and `c-gcc-release-with-data.log` in the same evidence
  directory.
- The first fixed-source H pilot used one current-source B+ index and one
  current-source sorted-array index. B+ and sorted-array reported exactly the
  same ordered float32 results and counts for k=1/100 and complete=0/1. Against
  the historical H baseline, IDs and counts are identical, but distances differ
  on 27, 17, 100, and 100 queries respectively; the maximum difference is 1
  ULP for k=1 and 2 ULP for k=100. These are exact comparison findings, not
  quality passes under a tolerance policy. Pilot query timings (baseline,
  current B+, current array, in ms) were (117.568, 68.690, 70.197),
  (272.143, 162.634, 155.179), (180.220, 98.262, 94.962), and
  (319.357, 176.921, 168.795) for those four cases; an unrelated long-running
  `/tmp/old_algo` process on Centaurus means these timings are preliminary.
- The fixed-source C Federalist pilot is preserved under
  `/home/jonathanhu237/code/qalsh-library-compatibility/implementation-evidence/c-federalist-pilot`.
  Fresh current B+ and sorted-array indexes were built for L1/L2 and both
  directions with the harness seed `1608637542`; an initial build using the
  CLI default seed 42 was retained as a setup diagnostic and discarded from
  quality conclusions. Current in-memory, B+, and sorted-array queries agree
  exactly in ordered IDs, counts, and reported float32 distances for all 1- and
  4-thread cases. Against the frozen flat-table C baseline, IDs and counts are
  identical; only reported distances differ, affecting 158/1880 (L1 ab),
  78/1178 (L1 ba), 975/1880 (L2 ab), and 584/1178 (L2 ba) queries. Maximum
  differences are 2 ULP (L1) and 7–8 ULP (L2). This remains an explicit
  numerical-policy finding rather than a tolerance-based pass.
- C pilot query timings (baseline, current B+, current array, in ms; open time
  is listed separately in each raw run log) were, at one thread: L1 ab
  (119.545, 113.306, 111.970), L1 ba (109.695, 96.378, 94.507), L2 ab
  (69.493, 47.234, 46.390), and L2 ba (66.794, 41.396, 40.419). At four
  threads they were L1 ab (32.392, 30.231, 29.481), L1 ba (29.269, 25.485,
  25.079), L2 ab (18.648, 12.042, 11.809), and L2 ba (17.660, 11.083,
  11.207). These are fixed-source single runs with CPU affinity 20–23 and
  the unrelated `/tmp/old_algo` process active; they are pilot evidence, not
  repeated paired acceptance measurements.
- A query-only MNIST disk pilot is preserved under
  `/home/jonathanhu237/code/qalsh-library-compatibility/implementation-evidence/c-mnist-pilot`.
  Across 3,000 A-to-B queries, current C and the seed-matched frozen flat-table
  baseline agree exactly in ordered IDs, counts, and float32 distances for L1
  and L2 at one and four threads. Query times (baseline, current, in ms) were
  L1 (17,511.167, 11,030.748) and (4,765.123, 2,908.856), then L2
  (13,355.955, 7,260.975) and (3,584.266, 1,847.221), for one and four
  threads respectively. Opening took 0.627–1.741 ms for the baseline and
  26.313–59.618 ms for the current mmap index. These are single fixed-source
  runs with CPU affinity 20–23 while the unrelated `/tmp/old_algo` process
  remained active, so they are pilot evidence rather than repeated paired
  acceptance measurements.
- The C Federalist in-memory pilot also exposed a common-engine performance
  gap that disk timings alone would hide: current L1 query time was 105.098 ms
  versus 93.188 ms for the baseline at one thread A-to-B (+12.8%), and 28.197
  versus 24.978 ms at four threads (+12.9%); B-to-A was 91.356 versus 85.831 ms
  at one thread (+6.4%). Current L2 in-memory timings were faster. This is an
  open performance-recovery item; the measured disk improvements do not close
  it.
- Added a public regression assertion that candidate-level termination still
  consumes the complete logical scan range (`projection_hits` exceeds the
  number of evaluated candidates). The built-in collision-count delivery path
  now hoists its immutable-during-scan finish flag into a local and refreshes
  it only after evaluation callbacks; custom candidate rules retain the
  ordinary per-hit path. The ID bounds guard remains in the built-in handler.
  Local post-change CTest remains 4/4; the subsequent Centaurus Docker result
  is recorded below.
- Post-change Centaurus validation used the GCC 15 toolchain in
  `qalsh-cxx15-deps:latest` with host `/usr` mounted. The fresh library build
  in `implementation-builds/lib-gcc-opt` passed CTest 4/4. Fresh consumer
  builds in `c-gcc-opt` and `h-gcc-opt` compiled the current C and H sources;
  the H Docker test passed 1/1. The no-op rebuild and test transcript is
  archived as `implementation-evidence/consumer-opt-rerun.log`.
- A bounded Release query pilot was run after rebuilding the comparison
  harness with the current header. Its fresh artifacts are under
  `implementation-evidence/c-federalist-opt-release-2d1c0e6c`: fixed
  Federalist L1 A-to-B, memory layout, 1 and 4 OpenMP threads, CPU affinity
  20–23, baseline/current run pairs, strict exact comparator JSON, and
  provenance hashes. Query totals were baseline/current 93.052/102.502 ms
  at one thread (+10.2%) and 25.130/27.559 ms at four threads (+9.7%). The
  exact comparator still reports identical IDs/counts with 158 distance-only
  query changes and a maximum 2 ULP difference. The unrelated long-running
  `/tmp/old_algo` process remained active, so these are diagnostic pilot
  timings; the L1 memory slowdown remains unresolved and is handed to the
  parent review.
- The first attempt at this pilot used a fresh harness build without
  `CMAKE_BUILD_TYPE=Release` and measured a debug-like 9x slowdown. Those
  artifacts remain in `c-federalist-opt-2d1c0e6c` as a discarded setup
  diagnostic; the release pilot above is the only timing result used here.
- Stable handoff state: no validation or benchmark process is running. The
  current library header hash is
  `2d1c0e6cab0284696aff3279f8e6fab3a9d8c5bf5b06ac65f509b5e18c169ae`, and
  the permanent public-test hash is
  `7d66ecf2fde4ef40e0c618eeba145df960cf8c2ba4abccb7d5f69da46969cb41`.
  The deterministic source manifest is
  `current-source-fingerprint.json`: aggregate hashes are lib
  `39f772325f4f17948f60a9a45d40b076418fb191e4eb2f19fdb3abbe1fcb1c12`, C
  `54d6b95351047ddbf07149c9ad22e47b0932a2627288e72effb0e1f2d7490c9e`, and
  H `32ad9edc853aa0dd62f59b571066cb544438f5330bb5783604217cdfa3a7da74`.
  Known open items for direct review are the unresolved numeric rounding
  policy, the common-engine L1 memory regression, and missing repeated/full
  acceptance-matrix measurements; no tolerance or tie exception has been
  adopted.

## 2026-09-12 — fix attempt 1 (R1/R2/R3)

The initial handoff left two narrowly scoped R1 experiments in the working
tree.  First, `TableScanSchedule::next_table()` was made inline in the public
header so the ordinary queue pop is visible to the typed strategy driver.  The
out-of-line definition was removed from `src/qalsh.cc`.  Second, the built-in
default collision path gained `ForEachHitId()` and `accept_builtin_id()`;
`DeliverRange<DefaultQalshStrategy>` now decodes only the point ID, hoists the
immutable-during-range finish flag, refreshes it after evaluation callbacks,
and still consumes and counts the whole logical range.  Custom candidate
rules and the public strategy callback retain the full `ProjectionHit` path,
including the ID bounds check.

Both changes were compiled from the local source, synchronized one way to
Centaurus, and validated in the required `qalsh-cxx15-deps:latest` Docker
image with the host GCC 15 toolchain mounted at `/usr`.  The fresh remote
library build at
`/home/jonathanhu237/code/qalsh-library-compatibility/implementation-builds/lib-r1-id`
passed CTest 4/4.  The current C comparison harness was rebuilt at
`implementation-builds/c-query-r1-id`; its source and binary hashes are
stored with the raw runs in
`implementation-evidence/r1-profile-id-ef7f2e82`.

The first post-inline profile moved the separate scheduler samples out of the
hot path but still measured roughly 99.8–100.5 ms for current Federalist L1
memory A-to-B versus roughly 92.7–93.3 ms for the frozen flat-table baseline.
The subsequent ID-only build measured a bounded taskset-20–23 pair of
93.35911/99.221117 ms at one thread and 25.413515/26.546013 ms at four
threads (baseline/current).  The current profile samples were approximately
32.44% `DeliverRange`, 40.66% `SearchEngine::search_impl`, 9.27%
`InMemoryIndex::reset_cursor`, 4.65% default-strategy `next`, and 3.29%
projection/result work.  These runs are diagnostic: the unrelated long-lived
`/tmp/old_algo` process was active on Centaurus, and the numerical policy is
still pending.  The residual shared-engine L1 regression remains open.

At the same remote revision, the C consumer release build and H consumer
Docker build were rerun from current sources under
`implementation-builds/c-gcc-opt` and `implementation-builds/h-gcc-opt`;
the H test passed 1/1 and the rerun transcript is in
`implementation-evidence/consumer-opt-rerun.log`.  No benchmark or
validation process is left running after these diagnostics.

The next fix-attempt steps are public multi-page strategy/lifecycle coverage,
current-source package and full consumer checks, then the frozen comparison
protocol's alternating paired Federalist L1 measurements.  Timing and matrix
rows will retain exact comparator failures and unresolved numerical-policy
findings rather than inheriting historical tolerance exceptions.

### Fix-attempt-1 bounded handoff

The public coverage was extended before the final validation pass.  The
multi-page fixture in `tests/public_test.cc` now uses 130 points, page size
512, several physical B+ leaves/array regions, and equal projection keys.  It
runs both `ScanScope::quantum` and `ScanScope::range` and records ordered
boundary, radius, and evaluation events.  After all tables are exhausted it
requests one radius advance and verifies that the deferred evaluation occurs
after that radius event exactly once.  The same trace and result are required
for B+ and sorted-array.  The lifecycle loop also covers omitted default
factory options, both persistent layouts, separate-process reopen, no-replace
behavior, one-byte tail truncation with the valid header retained, and a
sorted-array body entry with an out-of-range point ID.  The old short-magic
factory rejection remains a separate check.

The required Centaurus Docker command rebuilt the library, C, and H from the
current synchronized source under GCC 15 in
`qalsh-cxx15-deps:latest`.  The library CTest result is 4/4 in
`implementation-evidence/r3-docker/latest-library.log`; the C consumer is
23/23 and H is 1/1 in `implementation-evidence/r3-docker/combined.log`.
The source-tree package consumer passed 1/1 and the installed
`find_package(qalsh CONFIG)` consumer passed 1/1 in
`implementation-evidence/r3-package/combined-rerun.log`, using builds
`package-source-r1-rerun` and `package-installed-r1-rerun` and install prefix
`implementation-prefix-r1-rerun`.  The package fixture exercises the
exported default, B+, sorted-array, and search API.  No validation process is
left running.

The frozen paired R1 run uses the existing open-once query harness
`tools/c_query.cc`, taskset CPUs 20–23, Federalist A-to-B L1, memory layout,
and fresh evidence directory
`implementation-evidence/r1-paired-ef7f2e82`.  It ran ten alternating
baseline/current pairs at one thread and ten at four threads.  All 20 strict
comparisons validated 1,880 complete rows and identical ordered IDs/counts;
each reported 158 distance-only changes with a maximum two-float32-ULP
difference.  The frozen bootstrap procedure is in
`comparison-protocol.md` and `paired_stats.py`.  At one thread the baseline
median/current median was 92.962543/99.152425 ms, the geometric ratio was
1.0702995, and the 95% paired-log bootstrap interval was [1.0649058,
1.0762589].  At four threads the medians were 24.901061/26.801242 ms, the
geometric ratio was 1.0755331, and the interval was [1.0618461, 1.0865833].
Both intervals are wholly above one, so the common-engine L1 memory
regression remains a failed diagnostic gate despite the built-in ID/finish
check optimization.  The unrelated long-lived `/tmp/old_algo` process
(PID 416377) remained active throughout and is recorded as timing noise.

The representative current-source matrix and every deliberately pending row
are frozen in `r2-scenario-matrix.md`.  In particular, current-source full
Federalist L1/L2 memory and persistent-layout rows outside the paired L1
case, MNIST, GIST, current H toy queries, resource rows, and the C `both`
workflow remain pending; pre-fix pilots are retained as context and are not
promoted.  The strict numerical policy also remains pending: no tolerance,
tie exemption, or changed-math waiver was adopted.

The refreshed source provenance is in
`current-source-fingerprint-fix1.json` and the canonical
`current-source-fingerprint.json`.  The library aggregate hash under the
documented path-and-file-digest method is
`bee7ca572eeb230be394461382d29bfe1b4824f6169cdd496689d0e90ee26e5f`; C and H
source aggregates are unchanged at
`54d6b95351047ddbf07149c9ad22e47b0932a2627288e72effb0e1f2d7490c9e` and
`32ad9edc853aa0dd62f59b571066cb544438f5330bb5783604217cdfa3a7da74`.
The current header/source/public-test hashes are
`ef7f2e82e287cf449d43de2baaefa2bf16226d1e59b5d35e47d4f98df9e8eb28`,
`19ab474aa13f76ddc841cb80f48a6bb577cf89cb9587e73ae0127673e5a9feb3`, and
`c21c6e20e4c1148a5d529714f29fc285cad782928a79ee162299955a70d37f2c`.

This is the stable fix-attempt-1 handoff for parent review 2.  There are no
uncommitted source edits after the R3 coverage/docs changes beyond the
existing migration work, no commit or push was made, and no benchmark is
running.  Remaining work is deliberately handed back as the R1 performance
finding, the pending R2 rows, and the unresolved N1 numerical decision.

### Fix-attempt-2 checkpoint

Parent review 2 assigned a final bounded implementation pass.  I changed the
library search dispatch so the shared search loop is instantiated for both
`SearchStrategy` and `DefaultQalshStrategy`; the public default overload uses
exact-type dispatch to the typed loop while arbitrary derived strategies keep
the erased-compatible path.  This is a single implementation template, with
no duplicated orchestration or exposed strategy state.  The local focused
build `/tmp/qalsh-lib-fix1-build` compiled and ran `qalsh_public_test` and
`qalsh_dispatch_test` successfully.  The synchronized Centaurus Docker
library build `implementation-builds/lib-r2-dispatch` passed 4/4 CTest, and
the optimized current C harness build `implementation-builds/c-query-r2-dispatch`
compiled under GCC 15.

The fresh fixed-source Federalist L1 memory pair run is in
`implementation-evidence/r1-r2-paired-0ef1cec8`.  It used the open-once
query harness, taskset CPUs 20–23, ten alternating pairs at T1 and T4, and
the frozen strict comparator/statistics protocol.  All rows retained identical
ordered IDs and counts; 158 query distances changed by at most two float32
ULPs.  The current/baseline geometric timing ratios were 1.05694669 (T1,
bootstrap 95% [1.05389317, 1.06025532]) and 1.06309160 (T4, bootstrap 95%
[1.05053815, 1.07871325]).  This is an improvement over attempt 1 but still
fails the no-regression diagnostic.  The attempt-2 optimized range profile is
in `implementation-evidence/r1-r2-dispatch-profile`; it confirms the typed
`DeliverRange<DefaultQalshStrategy>` path, with no active profile or benchmark
left running.

The current H Docker build `implementation-builds/h-r2-dispatch/qalsh-h`
passed 1/1 CTest.  The current-source toy controls (k=1 and k=100,
default and complete-radius, B+ and sorted-array) ran successfully in
`implementation-evidence/h-toy-r2-dispatch`; B+ and array results matched,
and separate `/usr/bin/time -v` resource rows were captured.  The remaining
bounded work is a current C rebuild, MNIST B→A representative rows, seeded
projection/hash provenance, and then refreshing the matrix/fingerprint with
the residual R1/R2/N1 gaps stated plainly.

### Fix-attempt-2 final bounded handoff

The final C consumer was rebuilt from the synchronized source in Centaurus
Docker image `qalsh-cxx15-deps:latest` with GCC 15 and passed 23/23 CTest in
`implementation-builds/c-r2-full`; the C binary SHA-256 is
`8bc17799bb4f440d4883fbefc389e5de89a91d4b67fc5fd193538ed29b96046a`.  The
final header/template was also exercised by fresh source-tree and installed
package consumers, each 1/1, in `package-r2-source-ctest.log` and
`package-r2-installed-ctest.log`.  The H Docker build remained 1/1 in
`implementation-builds/h-r2-dispatch` (binary hash
`cc6632d7a65da37bb1980c99362ec4377c62ca18f25f40e905da2401da6d1872`), and
the library CTest remained 4/4 in `implementation-builds/lib-r2-dispatch`.

The current C CLI completed the real Federalist `direction=both` workflow in
memory and disk modes using both B+ and sorted-array indexes.  The four
outputs are in `implementation-evidence/r2-c-federalist-both-v3/` and
`r2-c-federalist-both-v3-array/`; all report estimated distance
`69645.984375`, ground truth `59591.914062`, and 16.87% relative error.  The
updated `/usr/bin/time` child-resource helper recorded RSS and fault counters
without putting its launcher RSS into the measurement.

Current C query harness evidence covers MNIST A→B and B→A, L1/L2, disk mode,
and 3,000 queries in each direction.  Strict comparison reports in
`implementation-evidence/r2-mnist-ab-v2/` and `r2-mnist-ba-sample/` each
validate 3,000 rows with exact ordered IDs, counts, and reported float32
distances.  A→B query-only baseline/current times were 17555.18/9640.44 ms
(L1) and 13361.37/6808.82 ms (L2); B→A times were 658.68/577.66 ms (L1)
and 397.39/295.60 ms (L2).  These are single representative runs, not
paired performance acceptance evidence.  Native child resource JSON is
stored beside each result.

The public projection-hash probe in
`implementation-evidence/projection_hash_probe.cc` opened the current
Federalist B+ and sorted-array indexes and extracted all L1/L2 A/B projection
vectors.  The eight SHA-256 values in
`implementation-evidence/r2-projection-hashes/` match the frozen seeded
reference files byte-for-byte; each index records seed `1608637542`.

The current H toy CLI matrix in `implementation-evidence/h-toy-r2-dispatch/`
ran k=1 and k=100 with default and complete-radius strategies for both B+ and
sorted-array (8/8 runs, zero partial queries), with separate child resource
files.  Its output is aggregate Ratio/Recall/Time only; it does not claim
strict per-query neighbor equality.  A strict H TSV run and real GIST rows
remain pending.

The final source map is `current-source-fingerprint-fix2.json` and is copied
to `current-source-fingerprint.json`; the library aggregate is
`ab9266a2e091d0573b36fac0ee402e3e37115698d02b988c4ef7be44ab3d7452`, with
header `432a85af02c2b37c0e293742b5076c79a141b8f781ffeeb91e78f5d8a8d718b0`
and source `0ef1cec8587eae5bbfa242b71b77b2b5e09d05884ae5fa013476acd590a45ce3`.
The executed rows and remaining gaps are frozen in
`r2-scenario-matrix.md`.  No benchmark, profile, or build process is left
running.  R1 still fails the frozen no-regression timing gate after the
template dispatch optimization, R2 remains incomplete for full repeated
matrix/GIST/H strict coverage, and N1 has no approved numerical tolerance or
tie exception.  No commit or push was made.

## Parent takeover after third review

Final status and authoritative latest evidence: [parent-verification.md](parent-verification.md).
The parent fixed the focused L1 performance regression, reran all consumer and
package checks, and completed42quality/resource cases. Earlier logs above
remain historical. Strict numerical/tie compatibility and the full performance
acceptance matrix remain open; implementation is not unconditionally accepted.
