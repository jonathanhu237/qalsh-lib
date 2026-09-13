# Fixed SIMD `ClampToWindow` candidate

## Disposition

The candidate is **correctness-clean but not performance-accepted**.  The focused
Stage-B result is retained in the working tree, uncommitted and unpushed, but it
must not be described as a successful optimization:

* The declared first Stage-B attempt had a favorable timing interval against
  both baselines, but 3/12 triplets (25%) were contaminated by the >5% sibling
  occupancy rule.  The specification therefore makes that timing acceptance
  environmentally blocked.
* I made the one permitted evidence-backed adjustment: moved from physical
  CPU10/sibling22 to CPU6/sibling18 after a fresh preflight.  That attempt was
  worse environmentally (9/12 triplets, 75%, contaminated) and its candidate /
  722ecfb interval crossed 1.  No further retries were made.
* Stage C and the full 16-case matrix were not run because Stage B did not pass.
  Full-project acceptance remains open.

No query population, search parameter, layout, quality gate, or unrelated
production behavior was changed.

## Implementation

Production changes are limited to `src/qalsh.cc` and the private
`src/window_clamp.h` seam.  The helper is called at all six existing scanner
sites (in-memory, B+ tree, and sorted-array; both directions).  It preserves
`std::abs(query_value - projection_value) <= bound` float32 prefix semantics,
including inclusive boundaries, reverse traversal, empty ranges, short ranges,
tails, and extreme values.

On x86-64, the partial-window probe uses conservative SSE2 to test four
complete entries at a time, while retaining the endpoint fast paths and final
scalar binary search.  `memcpy` loads are unaligned/alias-safe and cover only
complete entries inside the supplied range.  Other targets and
`QALSH_WINDOW_FORCE_SCALAR` use the scalar binary-search implementation.  No
public API, persistent format, allocation, or query parameter was added.

## Correctness and consumer tests

The final candidate source was tested on Centaurus with g++-15:

* Library Release CTest: **7/7 passed**, including optimized and forced-scalar
  clamp tests and the independent scan/trace test
  (`ctest-candidate-tests.log`).
* Focused ASan/UBSan tests: **2/2 passed**
  (`ctest-candidate-sanitize.log`).
* Current C consumer suite: **23/23 passed** (`ctest-c-consumer-candidate.log`).
* H strategy consumer test: **1/1 passed** (`ctest-h-candidate.log`).
* Installed public-header/static-library smoke test exited 0
  (`installed_smoke.cc`).

The focused oracle corpus includes exhaustive small alphabets, duplicates,
all-outside/all-inside and boundary values, both directions, zero-length and
vector-tail ranges, randomized lengths around SIMD boundaries, and explicit
unaligned/end-of-buffer cases.  The trace test compares independent scalar and
candidate paths across the in-memory, B+ tree, and sorted-array scanners.

## Stage A: bounded kernel screen

`stage-a-20260913.txt` used the recorded length distributions, both traversal
directions and tails, independent scalar-oracle checks, 2,500 repetitions,
and a volatile checksum.  Candidate/scalar ratios were:

| population | ratio |
|---|---:|
| histogram-synthetic (overall) | 0.874739 |
| short | 1.05386 |
| tails | 1.20363 |
| medium | 0.439416 |
| long | 1.26792 |

This is screening evidence only: the mixed per-shape regressions were not
hidden by the overall result.

## Stage B: declared primary attempt

Workload: Federalist L1 AB, 1,880 queries, 1,178 base points, 300
 dimensions, memory backend, approximation ratio 2, fixed seed 1608637542,
one untimed warm population, then exactly 30 timed full populations per
process.  Variants were run in each declared order
`ABC, ACB, BAC, BCA, CAB, CBA`, twice, for 12 complete triplets.  The measured
value is whole-batch query time divided by 30; the raw records also retain
whole-batch `query_ms`, open time, warm time, perf counters, affinity, sibling
occupancy, and a full result file for every process.

All 12 result populations were stable.  Every audited-original-C result had
hash `14674142271753866389`; every unmodified-722ecfb and candidate result had
hash `15320891441025449163`.  The baseline and candidate full result files
were byte-identical in every triplet.  The authoritative result-file SHA-256
values are:

* audited original C: `dfeb3c2c217c9974f0624fede9f762624cefb6822b28280a3d750adc939302a4`
* unmodified 722ecfb: `48695f1a09f283e9ec8991cc4d04e94f0e8735a14cccc6b0df90ef8c780c370e`
* candidate: `48695f1a09f283e9ec8991cc4d04e94f0e8735a14cccc6b0df90ef8c780c370e`

The first attempt's paired-log bootstrap used 20,000 resamples and seed
20260912:

| comparison | baseline median ms/pop | candidate median ms/pop | geometric mean ratio | 95% interval | timing evidence |
|---|---:|---:|---:|---:|---|
| candidate / 722ecfb | 90.82398 | 90.31855 | 0.978408 | [0.942299, 0.999902] | improvement, but environmentally blocked |
| candidate / original C | 94.06282 | 90.31855 | 0.957967 | [0.952324, 0.962127] | improvement, but environmentally blocked |

The three contaminated triplets were 1, 3, and 12: sibling22 exceeded 5%
for at least one member.  Effective-frequency differences were below 5%.
The values above include all samples; no slow run was discarded.

The one permitted adjustment selected CPU6/sibling18 from a fresh preflight
(1.8% and 2.5% busy over the preflight sample).  It nevertheless produced
9/12 contaminated triplets.  Its all-sample results were:

| comparison | baseline median ms/pop | candidate median ms/pop | geometric mean ratio | 95% interval |
|---|---:|---:|---:|---:|
| candidate / 722ecfb | 90.57889 | 90.75333 | 1.006959 | [0.991983, 1.021514] |
| candidate / original C | 95.41086 | 90.75333 | 0.953305 | [0.935372, 0.969852] |

The first attempt is the declared primary evidence.  The adjustment is
retained as evidence, not substituted to conceal the primary environment
failure.

## Independent quality check

`stage_b_truth.cc` independently computed exact L1 nearest distances from the
raw float32 Federalist files using double accumulation, and `stage_b_quality.cc`
validated every returned row, ID, reported distance, and population count.
All three variants had 1,880 valid rows, no malformed/missing/invalid rows, no
reported-distance mismatch above the 1e-4 checker tolerance, and 1,025/1,880
exact (tie-aware) nearest hits.  An independent per-query comparison found
1,025 hits in both original C and candidate, with `original_only=0` and
`candidate_only=0`; the candidate therefore did not lose an exact hit on any
query relative to original C.  The candidate output was exactly equal to
722ecfb, so it had no quality delta against that baseline.  The original-C
reported-distance round-trip differed by at most 7.62939453125e-06.
This check is independent of the repeated-output hashes and does not waive
known historical source differences.

## Build/open/resource observations

These are reported separately from query timing.  In the resource smoke run,
maximum RSS was 10,256 KiB (original C), 10,292 KiB (722ecfb), and 10,300 KiB
(candidate).  The current baseline/candidate repeat binaries were 727,216 and
731,224 bytes; their static libraries were 476,640 and 482,272 bytes.  The
candidate's median open times in the primary attempt were 7.300 ms versus
7.297 ms for 722ecfb (original C: 9.114 ms).  The one-process resource smoke
and all build/open observations are not claims about isolated query latency.

## Reproduction/provenance

The fixed spec SHA-256 is
`fa8a00a2104d341054531f018792e925183d346b2b3f80caabab4889428ee5f2`; the
comparison baseline and working-tree HEAD are both
`722ecfbf8b426a3148d30f9cd6b393018bf31fab`.  The audited original C revision
is `266118b6851a30f5dfc7ba80b00760e887578f47`.  Source/build/data hashes and
all raw commands are in `source-fingerprint.json`, the two `raw-stageb-*`
directories, `consumer-source-hashes.txt`, and the preflight logs.

The primary build was synchronized one-way to
`/home/jonathanhu237/code/qalsh-window-simd` on Centaurus, using Ninja Release
`-O3 -DNDEBUG`, g++-15, and a private x86-64 SSE2 path.  CPU governors were
observed but not changed; no unrelated process was killed, suspended, or
re-affinitized.  No commit, push, or scratch cleanup was performed.

## Review 1 fix attempt 1 — validation-only repairs

This attempt addressed the two requested verifier/test findings without changing
`src/qalsh.cc` or `src/window_clamp.h`; the production helper remains the exact
candidate bound to the Stage-B evidence above.  Stage-B and Stage-C timing were
not retried.

### R1 status: fixed and revalidated

`tests/window_trace_test.cc` now uses an independent scalar linear-prefix
reference with a 15-entry quantum (the previous 3-entry quantum could not enter
the four-entry SIMD probe), long 97-point fixtures, and 512-byte B+/array
regions.  The public scans retain range scope with `max_entries=7`, and the
reference records forward/reverse partial ranges of at least six entries.  The
strategy traces now cover both quantum and range scope for in-memory, B+, and
sorted-array indexes, plus an explicit exhaustion strategy that advances until
all tables are exhausted and then submits the deferred `Evaluate(0)` action.
Trace metadata and hit events remain compared across the optimized index, the
typed/erased and external-index dispatch path, and the independent scalar
index.  A unique timestamped temporary directory is created for each trace
process, so concurrent builds cannot share the old fixed `/tmp` paths.

The trace output recorded SIMD-sized independent-reference ranges in every
backend and direction.  For example, the optimized and forced-scalar logs both
recorded memory quantum `simd_eligible_forward=11,
simd_eligible_reverse=11`, memory range `12/12`, and exhaustive quantum/range
traces with `table_exhausted=1` and `deferred_evaluation=1`; B+ and sorted-array
showed the same coverage (B+ quantum `10/11`, array quantum `11/11`).  These
are independent-reference eligibility counts, not a timing claim.

`CMakeLists.txt` now has the private build-time
`QALSH_WINDOW_FORCE_SCALAR` switch.  It is applied to the production `qalsh`
target, not only the helper test target.  On Centaurus, the exact commands were:

```text
cmake -S . -B build-optimized -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++-15 -DQALSH_BUILD_TESTS=ON -DBUILD_TESTING=ON \
  -DQALSH_WINDOW_FORCE_SCALAR=OFF
cmake --build build-optimized -j2
ctest --test-dir build-optimized --output-on-failure
cmake -S . -B build-forcedscalar -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++-15 -DQALSH_BUILD_TESTS=ON -DBUILD_TESTING=ON \
  -DQALSH_WINDOW_FORCE_SCALAR=ON
cmake --build build-forcedscalar -j2
ctest --test-dir build-forcedscalar --output-on-failure
```

Both complete library CTest runs passed **7/7**.  The optimized helper reported
`window clamp mode: sse2`; the forced production-library build reported
`window clamp mode: scalar`.  A noinline assembly probe of the same private
helper emitted `movdqu`/`pshufd`/`subps`/`cmpleps`/`movmskps` in the optimized
build and none of those SIMD probe instructions under
`QALSH_WINDOW_FORCE_SCALAR`, confirming the fallback does not require an
unsupported ISA.  The forced compile command contains
`-DQALSH_WINDOW_FORCE_SCALAR` for `src/qalsh.cc`, and its static library hash is
`e7a055e907c1343c7ca2c628dfcbda0c6995f5b29543f4924e7a144a81a75902`; the
optimized library hash remains
`d2a15e985f00bb336c6c10ab38c1b131461352b0bd339f26ad2ba8054d6736c4`.
Focused ASan/UBSan tests (`qalsh_window_clamp_test` and
`qalsh_window_trace_test`) also passed **2/2**.  Logs and compile fingerprints
are under `validation-fix1/` and are listed in `source-fingerprint.json`.

### R2 status: fixed and bounded-tested

`stage_b_quality_compare.cc` is now fail-closed.  It independently computes
double-accumulated L1 distances, validates finite query/base data and every
result row, checks query completeness/duplicates and point IDs against `nb`
before any coordinate indexing, rejects same-file (including canonical/equivalent
alias) baseline/candidate evidence, and validates reported float32 distances.
It compares every query rather than inferring quality from global maxima:
baseline exact-hit loss, selected-distance regressions, and approximation-ratio
regressions return status **5**; malformed/missing/duplicate/out-of-range/
non-finite input returns **3**; same-file evidence returns **4**.  Equivalent
exact ties are allowed.  The independent double-distance tolerance is `1e-9`,
the ratio tolerance is `1e-12`, and the float32 reported-distance round-trip
tolerance is `1e-4`; exact ties are not penalized by ratio noise.

`test_stage_b_quality_compare.py` compiles the checker with g++-15 and exercises
positive tie equivalence plus negative recall-loss, both-nonexact farther
candidate, missing, duplicate, out-of-range, non-finite, wrong-distance, and
same-file fixtures.  All fixtures passed with the expected return codes.  On
retained Stage-B triplet 01 outputs, the checker emitted 1,880 per-query
`status pass` records against audited original C and against unmodified
`722ecfb`, with `original_only=0`, `candidate_only=0`, and
`quality_failures=0` in both comparisons.  These are checker/quality-validation
runs over existing evidence, not new performance measurements.

### E1 status: unchanged environment blocker

The primary and one permitted adjustment timing attempts remain contaminated
under the frozen sibling-occupancy rule (3/12 and 9/12 triplets respectively).
No Stage-B retry, Stage-C run, CPU/governor change, threshold change, or slow
sample removal was performed in this fix attempt.  Candidate performance is
still **not accepted**; a reserved physical-core/time window is the exact next
input needed for performance adjudication.  The fixed spec hash remains
`fa8a00a2104d341054531f018792e925183d346b2b3f80caabab4889428ee5f2`, and the
final source/build/tool map is `source-fingerprint.json`.

## Review 2 fix attempt 2 — independent R2 quality validator

This final delegated R2 attempt changes only `.scratch/qalsh-window-simd/stage_b_quality_compare.cc`
and its focused fixture source. The production SIMD helper and all library
sources are unchanged. The checker now uses an independent Neumaier
`long double` L1 accumulator and a gamma forward-error bound (`u=epsilon/2`,
`gamma_n=n*u/(1-n*u)`) tied to its operation count. Exact-zero nearest
queries are handled as exact only when the returned distance is mathematically
zero; nonzero ties may be accepted only when independent error intervals
actually overlap. Candidate-vs-baseline distance and ratio comparisons use
intervals, and the ratio comparison is unconditional even if both exact flags
are true. Reported float32 values are validated through their adjacent-float
rounding cell plus the derived float32 arithmetic error, rather than an
absolute `1e-4` tolerance. The old absolute `1e-9`, `1e-4`, and `1e-12`
quality constants are gone.

`test_stage_b_quality_compare.py` retains every prior malformed/quality
fixture and adds the parent zero-to-nonzero and wrong-zero-encoding negatives,
normal adjacent-float and nonzero-tie cases, both-nonexact ratio regression,
subnormal and finite-extreme inputs, and a known float32 accumulation
rounding discrepancy. Local and Centaurus g++-15 Werror builds pass all
fixtures. The parent negative script now returns exit 5 for both cases. The
full validation details and formula are in
`validation-fix2/r2-quality-fix2.md`.

The checker was run (without timing) against existing Federalist triplet-01
results using distinct audited-original/current and unmodified-722ecfb/current
paths. Both comparisons have 1,880/1,880 per-query passes, 1,025 exact hits,
zero `original_only`, zero `candidate_only`, and zero quality failures. The
maximum original reported-distance error is `7.62939453125e-06`; the current
candidate error is zero. No Stage-B/C benchmark, CPU adjustment, or old timing
rebinding occurred; E1 remains environment-blocked and performance is not
accepted.
