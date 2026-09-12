# Migration acceptance manifest

Status: **not accepted; two targeted fix attempts consumed, actual parent reviews completed2/3. Parent review3/direct fixes continue; paired-v2 has stopped and no formal matrix is running.**

Current authoritative checkpoint: `review-3/progress.md`; protocol/tools under `review-3/`. Archived post-v2 source passes core4/4 Release/ASan-UBSan/Werror, C23/23 Release/ASan-UBSan, and H1/1 Docker Release/ASan-UBSan/Werror. Installed-package reruns and the full formal matrix remain outstanding; full C Werror is not claimed green. V2 stopped at a C harness macro defect; corrected MNIST reverse results match all67,000 rows, but final-source full-matrix evidence is missing. Diagnostic paired measurements still show C regressions; H mapped RSS is a resource finding. No cross-case averaging or unapproved5% margin will erase them. Versions1/2 and the older tables below are preserved historical evidence, not acceptance.

Parent correction: this file contains child self-test evidence, not parent review approval. The authoritative review count and findings are in `implementation.md` and `review-2/findings.md`.
No commit or push was made. The implementation and validation changes below are
uncommitted working-tree content; final revision pinning is not yet possible.

## Frozen inputs

- qalsh-lib baseline: `89797d839e56f9198489ad880231bf68d858bb88`; working branch `main`.
- qalsh4c baseline/working branch: `266118b6851a30f5dfc7ba80b00760e887578f47` / `refactor/use-qalsh-lib`.
- qalsh-h baseline/working branch: `a8c602605661ebc64e19e535f19424982e58e2c0` / `refactor/use-qalsh-lib`.
- Historical child executables came from `/home/jonathanhu237/code/qalsh4c-baseline-fixed` and the **wrong H baseline** `/home/jonathanhu237/code/qalsh-h-baseline-94be153`. They are not the current comparison. Parent v2 uses verified frozen archives under `/work/c-baseline` and `/work/h-baseline`, as detailed in review-3/progress.md.
- Machine: Centaurus, Linux `7.0.0-30-generic`, AMD Ryzen 9 5900X, 24 CPUs, GCC 15.2.0. Release flags were `-O3 -DNDEBUG -std=gnu++23 -fopenmp`; qalsh-lib additionally uses conditional `-fopenmp-simd` for projection reductions.
- C settings: `-T 1 --use-fixed-seed`, approximation ratio `c=2`, explicit disk-index seed `1608637542`, target `both`, and the production logical scan quantum (`256`, two legacy 128-entry sides). No OS page-cache dropping was used; repeated runs were warm-cache and alternating baseline/migrated pairs.

## Dataset fingerprints

The paired runs used the same canonical files under `/home/jonathanhu237/datasets/qalsh4c`:

| Dataset | A points | B points | dimensions | A SHA-256 | B SHA-256 | metadata SHA-256 |
| --- | ---: | ---: | ---: | --- | --- | --- |
| Federalist | 1,880 | 1,178 | 300 | `5bb17396ec0d8b77741f059cb484901a46ae62acee3437afb1d72ae05be07efd` | `4ab3e20e57692ef81665c6ac97ad32a41d11b4b7bf99fb843c779e9e720b3e95` | `67f67afba504a7a3c2d3ec05274cc50a3bedb0b245fd2786ae7c16d211c588a5` |
| MNIST | 3,000 | 67,000 | 784 | `2b8371e53bf95a02a807954072a32df309b6585a184bc36eabc4a10ae05b1346` | `cf038f3d4dd9a7a822a794bab1ffd62bf633250d1c43592f07ab0af7e35c749d` | `2bd86c69433bcb5a80f20de51159060e38da286ff910145bc6f49fd9072f537f` |

The actual projection-vector byte ranges were identical for baseline flat indexes and migrated B+ indexes for every dataset, norm (`l1`, `l2`), and target (`a`, `b`). This was checked from the generated indexes, not inferred from the seed. The migrated B+ index uses 16 KiB pages.

## Prior comparison evidence (superseded)

The timing and accuracy tables in the first implementation report were not
accepted. In particular, the measured H directory used source hashes
`d9c6f15febed6f987a326f6787c4e143b80681fe495afa3ad826f8328a4f5840` and
`3924fbdca411a55ae91ec578fac45bcc42b5bbee92222ec2b89e7f5d3d16f241`, which
are not the pinned H revision. The required H baseline is
`a8c602605661ebc64e19e535f19424982e58e2c0`; its relevant source hashes are
archived in `review-1/attempt-1-evidence/h-pinned/`. The earlier claims of
accuracy completeness, and the C disk size accounting based on an implausibly
small flat-table payload, must not be used as acceptance evidence.

## Accuracy/equivalence (historical, not a gate)

The following table is retained only to preserve the first attempt's raw
observations; it is superseded until rebuilt from the pinned H source and a
fresh matched matrix.

All listed C production estimates were run in memory and disk mode for `l1`/`l2` and `ab`/`ba`/`both`. Baseline and migrated aggregate values matched byte-for-byte at the printed float precision; the exact cached QALSH weights also matched byte-for-byte for all eight `(dataset, norm, source)` files (`A`/`B`, `l1`/`l2`). Representative Federalist outputs:

| norm | mode | A→B | B→A | both |
| --- | --- | ---: | ---: | ---: |
| L1 | memory | 43971.777344 | 25674.210938 | 69645.984375 |
| L1 | disk | 43971.773438 | 25674.212891 | 69645.984375 |
| L2 | memory | 3267.908447 | 1922.095459 | 5190.003906 |
| L2 | disk | 3267.907715 | 1922.095947 | 5190.003906 |

Federalist ground truths are L1 `(37563.8125, 22028.1035)` and L2 `(2719.130371, 1597.465332)` for `ab`/`ba`; both-direction truth is their sum. MNIST `ab` estimates also matched (L1 memory/disk `30329392`/`30329360`; L2 memory/disk `3547537.5`/`3547538.75`). MNIST `ba` truth is unavailable in the frozen data and was not treated as a quality pass.

The qalsh-h toy workload (`n=10,000`, 100 queries, 256 dimensions) was run with default and complete-radius QALSH-H modes, `k=1` and `k=100`, plus linear-scan control. Baseline/migrated quality was:

| mode | k | baseline Ratio / Recall | migrated Ratio / Recall |
| --- | ---: | --- | --- |
| default | 1 | 1.118111 / 1.00% | 1.112412 / 1.00% |
| complete-radius | 1 | 1.011630 / 47.00% | 1.011630 / 47.00% |
| default | 100 | 1.069267 / 7.02% | 1.065654 / 7.56% |
| complete-radius | 100 | 1.021711 / 30.62% | 1.021711 / 30.62% |
| linear control | 1/100 | 1.000000 / 100.00% | 1.000000 / 100.00% |

Migrated H runs reported zero partial queries. The H ratio did not worsen and recall did not decrease in this workload.

## Repeated timing evidence (historical, not a gate)

The following measurements are retained as failed/superseded observations.
Ten alternating baseline/migrated pairs were run for each row. Ratios are migrated/baseline application-reported query-stage times; the interval is a bootstrap 95% interval for the paired median ratio. Memory-mode C timing includes in-process index construction; disk-mode timing includes index opening and querying. Raw per-run output remains on Centaurus under `/tmp/qalsh-acceptance-final/timing-final` and `/tmp/qalsh-h-acceptance-final/timing-final`.

| consumer/workload | baseline median | migrated median | ratio | bootstrap 95% interval |
| --- | ---: | ---: | ---: | ---: |
| C Federalist L2 memory | 62.266 ms | 65.888 ms | 1.059 | [1.034, 1.078] |
| C Federalist L2 disk | 69.052 ms | 68.656 ms | 0.994 | [0.975, 1.010] |
| C MNIST L2 memory | 10886.117 ms | 12353.511 ms | 1.129 | [1.120, 1.143] |
| C MNIST L2 disk | 12684.195 ms | 14890.543 ms | 1.163 | [1.156, 1.180] |
| H toy default k=1 | 1.478 ms/query | 3.725 ms/query | 2.485 | [2.458, 2.546] |
| H toy complete k=1 | 3.568 ms/query | 6.319 ms/query | 1.776 | [1.737, 1.824] |
| H toy default k=100 | 2.082 ms/query | 4.936 ms/query | 2.374 | [2.321, 2.402] |
| H toy complete k=100 | 4.322 ms/query | 7.254 ms/query | 1.684 | [1.651, 1.723] |
| H linear control k=1 | 12.748 ms/query | 6.720 ms/query | 0.527 | [0.525, 0.530] |
| H linear control k=100 | 12.793 ms/query | 6.741 ms/query | 0.528 | [0.527, 0.533] |

The specification's 5% slowdown is explicitly a proposed, not user-approved, policy. Even against that proposal, C Federalist memory, C MNIST memory/disk, and H default/complete QALSH rows exceed the proposed budget. Therefore these measurements are reported as open/failing performance evidence rather than silently accepted. The H migrated searcher now reuses the library engine/cursors across its query loop; this does not eliminate the observed generic-strategy/B+ traversal overhead. The migrated B+ indexes also use more space (for example, Federalist L2 B is 688,128 bytes versus the legacy flat representation's 45,600-byte table payload; MNIST L2 B is 36,995,072 bytes versus 206,976 bytes of flat table payload), which is a separate resource tradeoff.

## Latest targeted-fix validation

After the round-1 and final-acceptance fixes, the latest source remains uncommitted. Content and executable hashes, raw per-run output, and environment details are archived under `review-1/attempt-1-evidence/final-atomic/`. The latest executable hashes are:

- pinned C baseline: `4838032b64df98f39b0d0d5f7e5105f6403ce8d3f43a57ee7e22691a29df9c9d`;
- migrated C (complete metadata validation): `caa2bf0c950db4d7860d0a412365a66f2b9824bf3eb0586e3c3176f7f01338ef`;
- pinned H baseline: `84f52e3ca850f62be7382dbfefb13a13780744559fac3782975bf19811b4dab4`;
- migrated H (manifest publication, durability, and cleanup build): `7eaab0707a9f224212d8d5f57429f4dad3d519ad59aab556562f0f4aa1029682`.

Ten alternating warm-cache pairs on Centaurus GCC 15 (`-T 1`, fixed seeds) produced these application-time medians (migrated/baseline ratios). These timings were collected before the final tie/durability/cleanup fixes and are retained as performance evidence, not claimed as a final post-fix timing pass; raw runs remain under `review-1/attempt-1-evidence/final-postall/`.

| scenario | baseline median | migrated median | ratio |
| --- | ---: | ---: | ---: |
| C Federalist L1 memory A→B | 101.639 ms | 114.154 ms | 1.123 |
| C Federalist L1 memory B→A | 101.106 ms | 110.859 ms | 1.096 |
| C Federalist L1 memory both | 199.445 ms | 221.007 ms | 1.108 |
| C Federalist L1 disk A→B | 116.389 ms | 145.887 ms | 1.253 |
| C Federalist L1 disk B→A | 106.779 ms | 138.181 ms | 1.294 |
| C Federalist L1 disk both | 219.365 ms | 282.187 ms | 1.286 |
| C Federalist L2 memory A→B | 61.088 ms | 59.903 ms | 0.981 |
| C Federalist L2 memory B→A | 61.724 ms | 60.276 ms | 0.977 |
| C Federalist L2 memory both | 120.606 ms | 118.678 ms | 0.984 |
| C Federalist L2 disk A→B | 68.952 ms | 84.482 ms | 1.225 |
| C Federalist L2 disk B→A | 63.992 ms | 82.849 ms | 1.295 |
| C Federalist L2 disk both | 129.871 ms | 166.260 ms | 1.280 |
| H toy default k=1 | 1.166 ms/query | 1.351 ms/query | 1.158 |
| H toy default k=100 | 1.642 ms/query | 2.058 ms/query | 1.253 |
| H toy complete-radius k=1 | 2.676 ms/query | 3.114 ms/query | 1.163 |
| H toy complete-radius k=100 | 3.103 ms/query | 4.311 ms/query | 1.390 |

C estimates and relative errors matched the pinned baseline at printed precision for every listed norm/direction/mode. H fixed-seed results were default k=1 `1.113364` / `1.00%`, default k=100 `1.070133` / `7.02%`, complete k=1 `1.011630` / `47.00%`, and complete k=100 `1.021711` / `30.62%`; migrated runs reported zero partial queries. H's default k=100 ratio is `1.070133` versus pinned baseline `1.069267`, a query-quality regression, not an approved accuracy difference. Parent review 2 requires correction; permission to use immediate evaluation does not waive the accuracy gate. The post-atomic Docker query reproduced the same ratios/recalls with zero partial queries.

The available C/H timing evidence is not a performance pass: the proposed 5% margin is exceeded in most L1/disk/H scenarios, and no numeric slowdown allowance has been approved. C L2 memory is within the proposed margin in that pre-final-fix run. MNIST rows were not rerun in this reported matrix, but the parent has confirmed the canonical MNIST A/B files remain available on Centaurus. Larger H real-data availability still needs investigation. Post-fix fixed-seed H control output is archived under `final-atomic/docker/h-query-final-round.out`; it confirms quality/partial-query behavior but is not a replacement timing matrix.

## Final acceptance-review finding closure

The previous child produced internal review-labeled notes and made additional fixes. Those were not parent review 3 or parent-authored fixes. Parent review 2 independently verified several corrected cases and identified remaining failures, recorded in `review-2/findings.md`. The reported bounded-L2 and H generation controls below are child self-test evidence, subject to the parent's review.

## Builds and tests

Final validation on Centaurus after the latest library/consumer changes:

- qalsh-lib source-tree Release CTest: `1/1` passed, including the bounded-L2 equal-distance/tie-ID regression.
- qalsh-lib source-tree ASan/UBSan CTest: `1/1` passed.
- qalsh-lib source-tree GCC 15 `-Wall -Wextra -Werror` CTest: `1/1` passed.
- qalsh4c source-tree GCC 15 Release CTest: `23/23` passed, including migrated dataset-overwrite paths and complete library-index metadata checks.
- qalsh4c source-tree GCC 15 ASan/UBSan CTest: `23/23` passed.
- qalsh-h source-tree GCC 15 Release CTest: `1/1` passed, including config/index mismatch, manifest generation selection over stale legacy artifacts, and concurrent same-searcher checks.
- H CLI rebuild/query control: a fresh toy generation published `.qalsh-current` plus one versioned index/config pair without direct compatibility files; a legacy-direct control left direct files untouched, rebuilding twice left one active versioned pair, and a rejected rebuild left the active manifest/artifacts unchanged; fixed-seed default/complete k=1/100 and linear queries all completed with zero partial queries.
- qalsh-h source-tree GCC 15 ASan/UBSan CTest: `1/1` passed.
- qalsh-h source-tree GCC 15 `-Wall -Wextra -Werror` build and CTest: `1/1` passed.
- Linux Docker GCC 15 bind-mounted-toolchain Release CTest: qalsh-lib `1/1`, qalsh4c `23/23`, and qalsh-h `1/1` passed after the tie, durability, cleanup, and metadata-validation fixes.
- Installed qalsh-lib package consumer validation after the latest library/consumer changes: qalsh4c `23/23` and qalsh-h `1/1` passed.
- C benchmark workflow scripts now recognize migrated index paths `index/qalsh/l1|l2/a|b/{meta.json,index.qalsh}`; all six current local benchmark workflows pass Python syntax compilation.
- Local final checks before cleanup: qalsh-lib Release `1/1` and ASan/UBSan `1/1` passed.

Representative commands:

```bash
cmake -S qalsh-lib -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DQALSH_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
cmake -S archive/qalsh4c -B build-q4 -DQALSH_LIB_SOURCE_DIR=/path/to/qalsh-lib -DQALSH4C_ENABLE_CUDA=OFF -DBUILD_TESTING=ON
cmake -S archive/qalsh-h -B build-h -DQALSH_LIB_SOURCE_DIR=/path/to/qalsh-lib -DQALSH_H_THIRD_PARTY_DIR=/path/to/third_party -DBUILD_TESTING=ON
```

## Targeted fix attempt 1 validation status

The library source-tree Release test now uses always-active checks and covers
same-process and separate-process B+ reopening, no-replace publication,
malformed/truncated files, cursor ownership, custom candidate/termination
replacement, candidate budgets, deferred duplicate evaluation, tied bounded
L2 pruning, non-finite input, and concurrent independent queries. The H
strategy test uses always-active checks and compares both QALSH modes with the
exact control on a deterministic fixture. Both consumer branches still have
uncommitted changes and no final Git revision pin.

Local commands run for bounded library/fixture checks were:

```text
cmake --build build/quick -j2
ctest --test-dir build/quick --output-on-failure
cmake --build /Users/jonathanhu237/code/archive/qalsh-h/build -j2
ctest --test-dir /Users/jonathanhu237/code/archive/qalsh-h/build --output-on-failure
```

The bounded local fixture runs are not acceptance evidence for the restricted C
non-MNIST workflow, but the required Centaurus and Linux Docker reruns have now
been completed for the available Federalist/toy matrix. Exact projection/data
and build hashes plus raw output are archived in
`review-1/attempt-1-evidence/final-atomic/`. The H failed-rebuild control (invalid page size, exit 1) also verified that a
rejected rebuild leaves the prior manifest/direct config/index hashes unchanged. No immutable final library Git revision exists because the working
tree remains uncommitted; no acceptance claim is made from these intermediate
hashes.

## Open coverage/acceptance items

- The required performance decision is open because the proposed budget was not approved and several real-data QALSH rows regress substantially. The remaining hotspot is library traversal/dispatch (and, for H, the B+ traversal/strategy integration); no quality weakening was used to improve it.
- MNIST has no frozen B→A exact truth, so that direction is evidence only, not an independent truth gate.
- The full spec matrix's unaffected C controls (large exact scan, uniform sampling, and quadtree) are covered by the consumer CTest suite but were not included in the ten-pair QALSH timing table. H's larger external benchmark datasets were unavailable in the validation checkout; the required toy workload was run.
- Parent review 2 supersedes any child-authored review/approval claims. Remaining issues include default top-k budgeting, live strategy statistics, H scheduling/accuracy, malformed-tree routing validation, index lifetime, extension-surface complexity, and incomplete performance evidence; these are not merely user-owned scope decisions. See `review-2/findings.md`.
