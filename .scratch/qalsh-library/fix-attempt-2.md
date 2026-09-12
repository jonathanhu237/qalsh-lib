# Targeted fix attempt 2 — self-test evidence

Status: implementation-only self-test evidence. This file does not change the
parent ledger, review findings, acceptance manifest, or review count. No commit,
push, merge, or parent-review claim was made.

## Scope

This fresh attempt addressed the remaining S1/F01/F02/F05/F06/F09/F13 findings
from `review-2/findings.md`:

- Kept one generic `SearchStrategy` seam and removed the earlier fast-path,
  cancellation/freshness, controlled-scan, batch, point-ID, and cursor-reuse
  compatibility surface. The engine retains traversal, point access, bounded
  distances, deduplication, and sorted top-k ownership.
- Default top-k budgeting now uses `min(n, candidate_budget + k - 1)` when no
  explicit strategy budget override is supplied. Explicit `set_candidate_budget`
  remains an absolute override or an explicit no-budget choice.
- H table scheduling is FIFO round-robin, and H requests the generic resumable
  `ScanScope::range` boundary. H's configured logical page size maps to a
  physical library page of `page_size + 16` bytes, preserving the legacy
  `(page_size - 12) / 8` entries-per-leaf boundary without adding an H-specific
  library mode.
- Persistent B+ tree open validation now verifies child subtree ranges and
  separator/routing order in addition to reachability, links, and page layout.
- Query-scratch cursor identity is weak, so a thread-local cache cannot keep a
  released immutable index alive. A regression test now verifies index release.
- The bounded-distance hot path has an unbounded SIMD reduction, and cached
  query snapshots are refreshed only when mutable result state changes rather
  than rebuilt for every non-candidate projection hit.
- H's README documents the logical/physical page mapping and rebuild behavior;
  the library README and strategy notes describe the implemented API.

## Correctness repro

The parent round-2 repro was rebuilt against the latest library and run on
Centaurus/GCC 15:

    default-k101: evaluated=200 neighbors=101 (base budget100 should allow200)
    stop-on-first-hit: evaluated=1 (expected1)
    index-released: 1 (expected1)
    misrouted-root: rejected QALSH internal child ranges are out of order

Raw command:

    c++ -std=c++20 -O2 -I/tmp/qalsh-fix-attempt2/qalsh-lib/include \
      /tmp/qalsh-fix-attempt2/qalsh-lib/.scratch/qalsh-library/review-2/repro.cc \
      /tmp/qalsh-fix-attempt2/build-c-release/qalsh-lib/libqalsh.a \
      -o /tmp/qalsh-fix-attempt2/repro-batch
    /tmp/qalsh-fix-attempt2/repro-batch /tmp/qalsh-fix-attempt2/repro-data

## Builds and tests

Centaurus Linux, GCC 15.2.0, with the source trees synchronized one-way to
`/tmp/qalsh-fix-attempt2`:

- qalsh-lib Release CTest: `1/1` passed (`build-lib-final`).
- qalsh-lib ASan/UBSan CTest (`detect_leaks=0`): `1/1` passed
  (`build-lib-final-asan`).
- qalsh-lib `-Wall -Wextra -Wpedantic -Werror` CTest: `1/1` passed
  (`build-lib-final-werror`).
- qalsh4c Release CTest: `23/23` passed (`build-c-release`).
- qalsh4c ASan/UBSan CTest (`detect_leaks=0`): `23/23` passed
  (`build-c-final-asan`).
- qalsh-h GCC 15 Release CTest: `1/1` passed (`docker-final-h-latest2`).
- qalsh-h GCC 15 `-Wall -Wextra -Wpedantic -Werror` CTest: `1/1` passed
  (`docker-final-h-werror2`).
- qalsh-h GCC 15 ASan/UBSan CTest (`detect_leaks=0`): `1/1` passed
  (`docker-final-h-asan2`).
- Linux Docker (`qalsh-cxx15-deps:latest`, bind-mounted `/usr`) current-source
  H Release CTest: `1/1` passed (`docker-final-h-docker2`).
- Linux Docker current-source C Release CTest: `23/23` passed
  (`docker-final-c-docker2`).
- Linux Docker current-source H ASan/UBSan CTest: `1/1` passed
  (`docker-final-h-docker-asan2`).

The host-side C/H builds were used for fast iteration; the final consumer
smoke/build checks above were also repeated in the required Linux Docker
environment. The initial local macOS ASan run required
`ASAN_OPTIONS=detect_leaks=0` because leak detection is unsupported by that
runtime; the remote GCC 15 sanitizer runs used the same explicit setting.

## Real consumer accuracy/performance checks

The current migrated C executable was built at
`/tmp/qalsh-fix-attempt2/build-c-release/qalsh4c`; the pinned C baseline
executable was `/home/jonathanhu237/code/qalsh4c-baseline-fixed/build-centaurus-release/qalsh4c`.
Runs used `--use-fixed-seed -T 1`, `c=2`, explicit index seed `1608637542`,
and warm-cache Centaurus execution. Federalist data fingerprints were:

- A: `5bb17396ec0d8b77741f059cb484901a46ae62acee3437afb1d72ae05be07efd`
- B: `4ab3e20e57692ef81665c6ac97ad32a41d11b4b7bf99fb843c779e9e720b3e95`
- metadata: `67f67afba504a7a3c2d3ec05274cc50a3bedb0b245fd2786ae7c16d211c588a5`

Migrated and baseline Federalist indexes were freshly built for both norms and
both targets in separate directories. All 24 paired command rows
(`l1/l2 × ab/ba/both × memory/disk`) returned the same printed estimates and
relative errors, apart from float-rounding differences in migrated L1-BA and
L2 rows. Representative final runs (milliseconds; migrated / baseline) were:

| workload | baseline | migrated | ratio |
| --- | ---: | ---: | ---: |
| Federalist L1 memory A→B | 105.610 | 243.631 | 2.307 |
| Federalist L1 disk A→B | 116.300 | 265.224 | 2.281 |
| Federalist L2 memory A→B | 62.277 | 146.013 | 2.345 |
| Federalist L2 disk A→B | 69.624 | 157.754 | 2.265 |
| Federalist L2 memory both | 118.413 | 293.725 | 2.481 |
| Federalist L2 disk both | 133.300 | 308.501 | 2.314 |

The canonical Centaurus MNIST files also exist and were freshly copied into
separate baseline/migrated directories. Fingerprints were A
`2b8371e53bf95a02a807954072a32df309b6585a184bc36eabc4a10ae05b1346`, B
`cf038f3d4dd9a7a822a794bab1ffd62bf633250d1c43592f07ab0af7e35c749d`, and
metadata `2bd86c69433bcb5a80f20de51159060e38da286ff910145bc6f49fd9072f537f`.
For L2 A→B, both implementations reported the same estimate and relative error
(`3547537.5`, `11.83%` in memory; `3547538.75`, `11.83%` in disk):

| workload | baseline | migrated | ratio |
| --- | ---: | ---: | ---: |
| MNIST L2 memory A→B | 10898.556 ms | 25024.723 ms | 2.296 |
| MNIST L2 disk A→B | 12679.412 ms | 25524.598 ms | 2.013 |

The C profile confirms that the migrated and flat baseline did equivalent
projection work on MNIST (about 4.08 billion migrated versus 4.09 billion
baseline table-entry reads and 3001 candidate evaluations); the remaining
slowdown is generic callback/B+ traversal overhead, not reduced candidate work.
The full required ten-pair confidence-interval protocol was not repeated in
this fresh attempt. These final-source rows are raw evidence only, not a
performance pass.

H validation used the deterministic toy workload (`n=10000`, 100 queries,
256 dimensions) in Linux Docker. The clean pinned baseline source files are
archived at `.scratch/qalsh-library/review-1/attempt-1-evidence/h-pinned/`;
the migrated H source remained outside the library in `src/ann_searcher.cc`.
A fresh migrated build with logical page size 4096 produced physical index page
size 4112 and config page size 4096. Against the pinned baseline, the final
host-side Release query rows were:

| mode | k | baseline ms/query | migrated ms/query | baseline Ratio / Recall | migrated Ratio / Recall |
| --- | ---: | ---: | ---: | --- | --- |
| default | 1 | 1.276 | 1.962 | 1.118111 / 1.00% | 1.118111 / 1.00% |
| default | 100 | 1.621 | 2.502 | 1.069267 / 7.02% | 1.069267 / 7.02% |
| complete-radius | 1 | 2.641 | 3.909 | 1.011630 / 47.00% | 1.011630 / 47.00% |
| complete-radius | 100 | 3.097 | 4.640 | 1.021711 / 30.62% | 1.021711 / 30.62% |

All migrated H rows reported zero partial queries. A Docker smoke query on the
same final source reported default `k=100` Ratio `1.069267`, Recall `7.02%`,
zero partial queries, and `2.611 ms/query`. H's required larger external data
was not available in the validation checkout; no toy result is presented as a
larger-data acceptance pass.

## Fingerprints and raw artifacts

Current synchronized source hashes (content-addressed; no Git revision exists
for these uncommitted changes):

- qalsh-lib `src/qalsh.cc`: `4797bbec3549aa59907f8a33554fcc8eb3b3170429a6824f153b220c430d07c1`
- qalsh-lib `include/qalsh/qalsh.h`: `a4a1dc030dd1deb28ba72abf2094f4fa3f85e8759847069c9b5ba08dbd380a66`
- qalsh4c `src/ann_searcher.cc`: `0c6d38c959f0bdaa8048c17501d2cecca0ede1e7ba7a4e2bbd44d382fa5e0da1`
- qalsh4c `src/disk_qalsh_index.cc`: `223ea95fdbe8bcb727ebd535b7b2f88880f2f31b53ca52d9ba0d1f00a1a4476c`
- qalsh-h `src/ann_searcher.cc`: `4de61280990f1b4ab44067ce5acf13cfbeace19fc395c0aaf57d8b6df2fc1e88`
- qalsh-h `src/command.cc`: `f3f52251ce2b228798de6aec432dce26033fee60828c0fca5a9c7bfae2b1cb1d`

Pinned baseline C binary SHA-256 is
`4838032b64df98f39b0d0d5f7e5105f6403ce8d3f43a57ee7e22691a29df9c9d`. The
pinned H baseline source hashes are the archived files referenced above;
intermediate baseline binaries from earlier validation directories are not
used as a claim of immutable final pinning. Current Docker migrated binaries:

- H: `219bdc1dec2dbf258e68c391f4ccf128ccd3cdf6e5d5c6cd85d76e520e38f5b1`
- C: `6ebe53122e339f55a4b6cae1007a6dfdcc17c8342d25b915a12ca20619103386`

Raw final-source output is retained on Centaurus under
`/tmp/qalsh-fix-attempt2/`, including `final-fed-*.out`,
`final-mnist-*.out`, `h-final2-*.out`, `h-pinned-*.out`, and the Docker build
and sanitizer directories. No local source or parent review artifacts were
modified by this report.

## Remaining acceptance status

- F05, F06, F09, and F13 repros pass in this attempt; H default quality now
  matches the pinned baseline on the fixed-seed toy workload, including k=1
  and k=100 and complete-radius modes.
- F01 remains open: final-source real C timings are about 2.0–2.5× baseline and
  H timings are about 1.48–1.54× baseline. No slowdown is called acceptable.
  The ten-pair confidence-interval protocol and further optimization remain
  for parent acceptance review.
- F02 remains a coverage gap for the full acceptance matrix: MNIST A→B was
  rerun with matching quality, but B→A has no frozen exact truth, the complete
  ten-pair timing matrix was not repeated, and larger H data was not available
  in this checkout. No toy-only or build-only acceptance claim is made.
- All work remains uncommitted on the existing migration branches, and this
  attempt did not edit `implementation.md`, either parent `review-*` finding,
  or `acceptance-manifest.md`.
