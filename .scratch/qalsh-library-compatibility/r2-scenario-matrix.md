# R2 representative scenario matrix — implementation-loop record

The first section records the fix-attempt-1 matrix retained for historical
comparison.  The final bounded execution record below supersedes its pending
rows where it has current-source evidence.  A row is marked **pass** only
when its command was run against the current source fingerprint
`current-source-fingerprint-fix2.json`.  Historical or pre-fix artifacts stay
listed as context and do not satisfy a current-source row.  Timing rows use
query-only `query_ms`; opening, index construction, output serialization, and
resource observations are separate.

The required Linux validation environment is Centaurus, Docker image
`qalsh-cxx15-deps:latest`
(`sha256:c78838db4d5f8aa1c52328c13847a377d5c25658316192eecab64b5e3fe24e7c`),
GCC 15, and the host `/usr` toolchain mount.  The unrelated process
`/tmp/old_algo` (PID 416377) was still consuming approximately one CPU during
the Federalist timing rows; it is recorded as noise and was not terminated.

## Current-source rows

| ID | Required control and workload | Current command/evidence | Result |
| --- | --- | --- | --- |
| L1 | Library default build, omitted options, search, deferred work, strategy controls, B+ and sorted-array reopen, no-replace, tail truncation, and array-body integrity | Docker CTest in `implementation-evidence/r3-docker/latest-library.log`, build `implementation-builds/lib-r1-r3-latest`; assertions live in `tests/public_test.cc` | **Pass 4/4**. The multipage fixture has 130 points, page 512, 3+ physical regions, equal projection keys, both `quantum` and `range` scopes, post-exhaustion radius advance, and one deferred evaluation. |
| L2 | C consumer full unit/CLI suite and library linkage | Docker CTest in `implementation-evidence/r3-docker/combined.log`, build `implementation-builds/c-r1-r3` | **Pass 23/23**. Includes in-memory/disk CLI, directions, weights, sampling, dataset and malformed-input checks. |
| L3 | H consumer Linux Docker suite and library linkage | Docker CTest in `implementation-evidence/r3-docker/combined.log`, build `implementation-builds/h-r1-r3` | **Pass 1/1**. Includes default and complete-radius strategy controls and both persistent layout fixtures in the public H test. |
| P1 | Source-tree package consumer using exported `qalsh::qalsh` target, default factory, B+, sorted-array, and search | `implementation-evidence/r3-package/combined-rerun.log`, build `implementation-builds/package-source-r1-rerun` | **Pass 1/1**. |
| P2 | Installed package consumer using `find_package(qalsh CONFIG)` and installed target | Same log, install prefix `implementation-prefix-r1-rerun`, build `implementation-builds/package-installed-r1-rerun` | **Pass 1/1**. |
| C1 | Federalist, L1, A→B, in-memory, one thread; same seeded query population; strict per-query comparison; ten alternating pairs | `implementation-evidence/r1-paired-ef7f2e82/summary-t1.json` and `compare/` | **Quality: unresolved**. 1,880/1,880 rows and IDs/counts agree; 158 distance-only changes, max 2 ULP. **Timing: slowdown**; geometric current/baseline 1.07030, bootstrap 95% interval [1.06491, 1.07626]. |
| C2 | Same Federalist workload at four OpenMP threads | `implementation-evidence/r1-paired-ef7f2e82/summary-t4.json` and `compare/` | **Quality: unresolved**. Same 158 distance-only changes and max 2 ULP. **Timing: slowdown**; geometric ratio 1.07553, interval [1.06185, 1.08658]. |
| C3 | Current-source binary and data provenance for C1/C2 | `implementation-evidence/r1-paired-ef7f2e82/provenance-sha256.txt`, `pairs-t{1,4}.json`, `artifact-sha256.txt` | **Pass provenance**. Baseline executable is frozen `c-query-baseline`; current is `c-query-r1-id`; harness source is the frozen `tools/c_query.cc`; Federalist A/B and metadata hashes match between baseline and current directories. |

## Required rows retained as pending

These rows are deliberately named so the next review can extend the matrix
without changing the protocol or silently treating older pilots as current
acceptance evidence.

| ID | Required control/workload | Status and retained context |
| --- | --- | --- |
| C4 | Federalist current-source exact comparisons for L1/L2, both A→B and B→A, memory and both persistent layouts, T1/T4 | **Pending current-source rerun.** The pre-fix pilot under `implementation-evidence/c-federalist-pilot` covers these combinations and reports exact differences, but it predates the fix-attempt-1 search-path source. |
| C5 | Federalist `both` direction application workflow and current-source timing/resource rows | **Pending.** CTest exercises direction parsing and the individual directions; no post-fix full `both` data run was launched at this bounded handoff. |
| C6 | MNIST L1/L2 representative A→B memory/disk current-source comparisons and exact references | **Pending current-source rerun.** The earlier 3,000-query disk pilot is retained under `implementation-evidence/c-mnist-pilot` as pre-fix diagnostic evidence. |
| C7 | Current-source C index build/open byte size, peak/file-backed memory, and end-to-end rows for B+ and sorted-array | **Pending.** Existing build logs and pre-fix resource observations remain available; no resource result is promoted to current acceptance. |
| H1 | Toy current-source query matrix: default/complete-radius, k=1/k=100, B+/sorted-array, exact per-query comparison | **Pending current-source rerun.** The prior Docker query artifacts under `implementation-evidence/h-toy-queries` cover the controls but use the pre-fix query binary. |
| H2 | GIST representative default/complete-radius, k=1/k=100, B+/sorted-array, quality and resource rows | **Pending.** No post-fix GIST workload was launched in this bounded pass. |
| H3 | H parameter overrides (`beta`, hash-table count), deferred control on a multi-page real workload, and application output metrics | **Pending.** Public strategy tests cover the callback contract and deferred boundary; real-data override rows remain. |
| H4 | H base-point mapping peak/RSS and end-to-end open/query resources | **Pending.** The README documents the mapping model; measured GIST resource evidence still needs a current-source run. |
| R1 | Common-engine Federalist L1 memory recovery with no regression | **Failed diagnostic gate.** The focused built-in ID path/finish-check optimization reduced the earlier overhead but did not recover parity; C1/C2 remain clearly slower. Parent review 2 should target shared scanner/search-engine costs. |
| N1 | Numerical policy for changed float32 distances and any tie behavior | **Pending user decision.** No tolerance, tie exemption, or changed-math waiver was adopted. |

## Historical fix-attempt-1 provenance and interpretation

The current repository source map and per-file hashes are in
`current-source-fingerprint-fix1.json` (also refreshed at
`current-source-fingerprint.json`).  The library header, implementation, and
public-test hashes are respectively
`ef7f2e82e287cf449d43de2baaefa2bf16226d1e59b5d35e47d4f98df9e8eb28`,
`19ab474aa13f76ddc841cb80f48a6bb577cf89cb9587e73ae0127673e5a9feb3`, and
`c21c6e20e4c1148a5d529714f29fc285cad782928a79ee162299955a70d37f2c`.
The C/C++ query harness writes nine significant digits, while the strict
comparator checks the resulting float32 bit patterns, ordered IDs, counts,
and malformed rows.  The independent distance audit previously found every
current pilot distance to match double-coordinate arithmetic rounded to
float32; that evidence does not resolve N1 or ANN output identity.

## Fix-attempt-2 bounded execution record

This record covers the final delegated implementation pass after review 2.
The source fingerprint for these rows is
`current-source-fingerprint-fix2.json`, with library aggregate
`ab9266a2e091d0573b36fac0ee402e3e37115698d02b988c4ef7be44ab3d7452`, header
`432a85af02c2b37c0e293742b5076c79a141b8f781ffeeb91e78f5d8a8d718b0`, and
implementation `0ef1cec8587eae5bbfa242b71b77b2b5e09d05884ae5fa013476acd590a45ce3`.
All commands below ran on Centaurus in Docker image
`qalsh-cxx15-deps:latest`, digest
`sha256:c78838db4d5f8aa1c52328c13847a377d5c25658316192eecab64b5e3fe24e7c`,
with GCC 15 and the host `/usr` toolchain mount.  The unrelated long-lived
`/tmp/old_algo` process (PID 416377) remained active and is timing noise.

| ID | Current-source command/workload | Evidence | Result |
| --- | --- | --- | --- |
| A1 | Library CTest after the typed shared-loop dispatch | `implementation-evidence/lib-r2-dispatch` | **Pass 4/4**. |
| A2 | C consumer rebuild and full unit/CLI suite | `implementation-evidence/c-r2-full-config.log`, `c-r2-full-build.log`, `c-r2-full-ctest.log`; binary `implementation-builds/c-r2-full/qalsh4c` | **Pass 23/23**. Binary SHA-256 `8bc17799bb4f440d4883fbefc389e5de89a91d4b67fc5fd193538ed29b96046a`. |
| A3 | H consumer Linux Docker suite after the final library source | `implementation-builds/h-r2-dispatch/qalsh-h` and its CTest log | **Pass 1/1**. Binary SHA-256 `cc6632d7a65da37bb1980c99362ec4377c62ca18f25f40e905da2401da6d1872`. |
| A4 | Source-tree and installed package smoke consumers after the final header/template | `implementation-evidence/package-r2-source-ctest.log`, `package-r2-installed-ctest.log` | **Pass 1/1 each**. Both binaries SHA-256 `2ed531853db581dbeaadb02b0611b429c0dac4b6421581099f16004bff356adc`. |
| A5 | C Federalist `direction=both`, L1, fixed seed, memory and disk, B+ and sorted-array | `implementation-evidence/r2-c-federalist-both-v3/` and `r2-c-federalist-both-v3-array/` | **Pass workflow**. All four runs completed. Estimated distance `69645.984375`, ground truth `59591.914062`, relative error `16.87%`; B+ and array produced the same aggregate values. `/usr/bin/time` child RSS and fault reports are separate in each directory. |
| A6 | C Federalist seeded projection vectors, L1/L2, A/B, B+ and sorted-array | `implementation-evidence/r2-projection-hashes/` and `projection_hash_probe.cc` | **Pass provenance**. All eight extracted byte hashes match the frozen baseline projection files; metadata records seed `1608637542`, dimensions `300`, and the expected table counts. |
| A7 | C MNIST A→B disk, L1/L2, first 3,000 A queries, strict output comparison | `implementation-evidence/r2-mnist-ab-v2/quality-l1.json`, `quality-l2.json` | **Pass quality**. Both comparisons validate 3,000/3,000 rows with no count, ID, or reported-distance differences. Query-only times were baseline/current `17555.18/9640.44 ms` (L1) and `13361.37/6808.82 ms` (L2); these are single-run diagnostics, not paired acceptance timings. |
| A8 | C MNIST B→A disk, L1/L2, first 3,000 B queries, strict output comparison | `implementation-evidence/r2-mnist-ba-sample/quality-l1.json`, `quality-l2.json` | **Pass quality**. Both comparisons validate 3,000/3,000 rows exactly. Query-only times were baseline/current `658.68/577.66 ms` (L1) and `397.39/295.60 ms` (L2); child RSS/resource rows are in the same directory. |
| A9 | H toy CLI controls, k=1/k=100, default/complete-radius, B+ and sorted-array | `implementation-evidence/h-toy-r2-dispatch/` | **Pass CLI/resource coverage**. All 8 runs completed with zero partial queries and matching aggregate Ratio/Recall/Time output across layouts; separate resource files report native child peaks. This directory has aggregate CLI output only, so it is not a strict ordered-neighbor equality claim. |
| A10 | Federalist L1 memory paired performance after typed dispatch | `implementation-evidence/r1-r2-paired-0ef1cec8/summary.json` and `compare/` | **Failed performance diagnostic**. Ten alternating pairs each at T1/T4 retained identical IDs/counts; 158 queries had distance-only changes, max 2 ULP. Geometric current/baseline ratio is `1.05694669` (T1, bootstrap 95% `[1.05389317,1.06025532]`) and `1.06309160` (T4, `[1.05053815,1.07871325]`). |

The current-source R2 evidence is therefore useful and bounded rather than a
complete acceptance matrix.  Remaining rows are: a full current Federalist
Cartesian quality/performance matrix beyond the sampled C cases; repeated
paired timings for the other datasets and layouts; H strict per-query TSV
comparison and real GIST default/complete-radius/resource controls; and any
unmeasured full-data sorted-array C workload.  The ten-pair R1 gate remains
failed, and the numerical policy for the observed float32 distance changes
remains unresolved under N1.  The dispatch profile in
`implementation-evidence/r1-r2-dispatch-profile` contains only ten total
perf samples and is retained as symbol-presence evidence, not hotspot
attribution.

## Parent final-source superseding record

See [parent-verification.md](parent-verification.md) and
`parent-evidence/final-coverage/summary.json` for42fresh cases after the parent
repair. They supersede the earlier pending statements for full MNIST reverse,
Federalist24-case ordered comparison, H toy strict/override results and GIST
B+ default/complete k1/k100. The focused R1 paired timing gate is now improved;
N1 remains unresolved, including equal-distance H membership/order changes.
Unmeasured matrix items remain open, and single-run resources are not paired
timing acceptance.
