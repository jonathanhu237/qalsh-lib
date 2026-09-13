# C bottleneck diagnosis: logical work probes

**Status:** complete; instrumentation-only diagnosis, not an optimization or performance-acceptance run.

## Scope and controls

- Fixed comparison point: `722ecfbf8b426a3148d30f9cd6b393018bf31fab` (`722ecfb`).
- Workload: Federalist, C consumer, L1, A→B, in-memory, one worker/thread, complete 1,880-query population, one measured repetition (`T1`). Data has 1,178 base points, 300 dimensions.
- Both variants were copied into the independent local tree `work-probes/`, synchronized one-way to Centaurus under `/home/jonathanhu237/code/qalsh-bottleneck-diagnosis/work-probes-v1`, built in the GCC 15 Docker environment, and run with `taskset -c 20`, `OMP_NUM_THREADS=1`, `OMP_PROC_BIND=true`, `OMP_PLACES='{20}'`.
- Instrumentation counters are reset after index initialization and OpenMP warm-up. They therefore describe the measured query repetition, not initialization. Timing fields/stdout were retained only for harness compatibility and are not evidence here.
- No production source, production build, optimization, or Git commit was changed. The probe-only edits are preserved as `original-source.patch`, `current-library.patch`, and `harness.patch`.

Builds used `Release`, `/usr/bin/x86_64-linux-gnu-g++-15` 15.2.0, C++23, `-O3 -DNDEBUG`, and one OpenMP worker. The current library was compiled with `QALSH_USE_OPENMP_SIMD`/`-fopenmp-simd`; the C consumer with `QALSH4C_USE_OPENMP`/`-fopenmp`. Exact configure/build commands are in `../original-build-v2.log`, `../current-build-v1.log`, and `build-metadata/*.commands`.

## Output validation

The validation criterion is instrumentation transparency: for each implementation, compare its instrumented TSV with the corresponding uninstrumented baseline TSV by query index, returned count, point ID, and distance. Distance is parsed and quantized to float32; timing is ignored.

| variant | baseline | instrumented | rows | ID/count/distance |
| --- | --- | --- | ---: | --- |
| original C | `../evidence/0-original.tsv` | `runs-v1/original.tsv` | 1,880 / 1,880 | **identical** |
| `722ecfb` library + C consumer | `../evidence/0-head.tsv` | `runs-v1/current.tsv` | 1,880 / 1,880 | **identical** |

This passes in `validation.json`. As a separate cross-implementation observation, the two instrumented variants have identical query order/count/point IDs, but retain the known 158 float32-distance-only differences (maximum absolute difference `7.62939453125e-06`); that is not an instrumentation failure and is not reclassified as a quality result here.

## Measured logical work

All values below are totals over 1,880 queries; `/q` is the total divided by 1,880. `N/A` means the event does not exist in that implementation, not zero work.

| probe / event | original C | `/q` | `722ecfb` | `/q` | comparability |
| --- | ---: | ---: | ---: | ---: | --- |
| query starts | 1,880 | 1.000 | 1,880 | 1.000 | direct |
| projection-table starts | 156,040 | 83.000 | 156,040 | 83.000 | direct |
| cursor resets / `InitCursor` | 156,040 | 83.000 | 156,040 | 83.000 | direct |
| cursor allocations | N/A | — | 83 | 0.044 | implementation-specific |
| lower-bound key comparisons | 1,600,867 | 851.525 | 1,600,867 | 851.525 | direct |
| scan calls | 582,782 | 309.990 | 582,779 | 309.989 | same logical schedule; call boundary differs by 3 |
| empty scans (`entries_visited == 0`) | 23,485 | 12.492 | 23,482 | 12.490 | direct |
| scan entries visited / delivered hits | 37,722,223 | 20,065.012 | 37,722,223 | 20,065.012 | direct total |
| left-half starts | 582,782 | 309.990 | 558,674 | 297.167 | implementation-specific |
| right-half starts | 582,782 | 309.990 | 582,737 | 309.966 | implementation-specific |
| collision state updates | 37,636,159 increments | 20,019.234 | 37,636,159 decrements | 20,019.234 | same event count, opposite representation |
| processed ignored hits | 3 | 0.002 | 3 | 0.002 | direct |
| post-termination ignored hits | 86,061 | 45.777 | 86,061 | 45.777 | direct |
| threshold-reached events | 1,881 | 1.001 | 1,881 | 1.001 | direct |
| candidate distance evaluations | 1,881 | 1.001 | 1,881 | 1.001 | direct |
| exact candidate distances | 1,881 | 1.001 | 1,881 | 1.001 | direct |
| pruned candidate distances | 0 | 0 | 0 | 0 | direct |
| radius rounds | 7,003 | 3.725 | 7,003 | 3.725 | direct |
| radius advances | 5,123 | 2.725 | 5,123 | 2.725 | direct |

### Window/range probes

The original scanner tests one projection boundary per entry attempt; its direct predicate counter is `projection_window_comparisons`. The migrated scanner emits bounded `HitRange`s and calls `ClampToWindow`; its endpoint/binary-search predicate counter is `inside_probes`. These are reported together for diagnosis, but they are **not identical units** and must not be converted into a timing claim.

| window/range event | original C | `/q` | `722ecfb` | `/q` |
| --- | ---: | ---: | ---: | ---: |
| projection-window predicate comparisons | 38,778,213 | 20,626.709 | N/A | — |
| `ClampToWindow` calls | N/A | — | 1,114,661 | 592.905 |
| ranges observed | N/A | — | 1,114,661 | 592.905 |
| empty ranges after clamp | N/A | — | 85,233 | 45.337 |
| `inside` projection probes | N/A | — | 8,460,384 | 4,500.204 |
| window binary searches | N/A | — | 927,698 | 493.456 |
| binary-search iterations | N/A | — | 6,332,792 | 3,368.506 |

For the migrated ranges, available length is 1–128 (mean 117.803, maximum 128); post-clamp length is 0–128 (mean 33.842, median 17, p90 113, p95 128). Binary searches average 6.826 iterations and have maximum 7. The raw per-bin distributions are in `raw.json` and each variant's `runs-v1/*.json`.

Scan-entry distributions are effectively identical: original and migrated histograms differ only in the empty bin (23,485 versus 23,482). Both range from 0 to 256 entries per scan call, with mean 64.728 entries/call, median 36, p90 179, p95 221, and p99 256.

## Diagnosis

1. **The dominant logical volume is hit processing, not final distance evaluation.** Both implementations deliver 37.722M projection hits (about 20,065/query), update collision state 37.636M times, and evaluate only 1,881 candidates (about one/query). The 86,061 post-termination hits are also identical. A top-k/distance-kernel change is therefore low leverage for this case unless it changes candidate admission semantics.
2. **Initialization is materially smaller in event count.** The two implementations perform exactly the same 156,040 table starts/resets and 1,600,867 lower-bound comparisons. This does not establish CPU cost, but it rules out a changed query/table-count workload as the explanation for this run.
3. **The migrated range path has substantial bounded-window bookkeeping.** It observes 1.115M ranges, 85,233 empty ranges, 8.460M endpoint/binary probes, and 6.333M binary iterations. The original's 38.778M direct projection-window comparisons are a different unit: original checks are per-entry attempts, while migrated probes operate on contiguous ranges. The counts identify scanner/window handling and collision-hit handling as the falsifiable high-frequency areas, without claiming which consumes more wall time.
4. **The three-scan-call difference is not a result-count difference.** Scan entries, delivered hits, collision updates, candidate evaluations, radius rounds, and output IDs/counts all agree. It is a boundary/scheduling representation difference between the two scanner implementations.

These are work-count findings only. No timing or perf result from the instrumented binaries is used, and no optimization is proposed or accepted by this report.

## Reproduction and artifacts

Run shape (from the isolated remote tree):

```text
export OMP_NUM_THREADS=1 OMP_PROC_BIND=true OMP_PLACES='{20}'
taskset -c 20 <instrumented-query> <data>/federalist 1 ab memory 1 <out.tsv> 1 <out.json> <variant>
```

The raw combined record is `raw.json` (SHA-256 `1b910b4810a73bf130bd11f7f069f7d1df516f88dd520d78a50026a2791da278`). `hashes.json` records source-set aggregates, probe patch hashes, baseline/instrumented binary hashes, data hashes, and build-artifact hashes. Key binary hashes:

```text
original baseline       e559efabca5fe787786e2895a4c0b1e3b5db49ef2153841ed8c4a966a533b6bd
722ecfb baseline        d9232c9a6db423bcf28a390de677ef701c33ca70892c5a58f5456dfd9a8e4983
original instrumented   4d1b52d71553081f6893b10a970f021c505cfff6c77b58b31b80ca8843b9f445
722ecfb instrumented    3f3000432a148389fb01e54a6127572dee35b8c821751ab0c7ebd8a3f59344bd
```

Important retained files:

- `raw.json`: combined raw counters, histograms, validation, and hash manifest.
- `runs-v1/original.json`, `runs-v1/current.json`: uncombined raw per-variant counters.
- `runs-v1/*.tsv`, `*.stdout`, `*.stderr`: run outputs; TSV timing is non-evidence.
- `validation.json`, `validate_and_manifest.py`: repeatable output-equivalence check and hash generation.
- `original-source.patch`, `current-library.patch`, `harness.patch`: probe-only edits.
- `remote-metadata.txt`, `build-metadata/`: host/compiler/container/cache/compile-command record.
