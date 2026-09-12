# Parent final-source comparison protocol (declared before paired measurements)

No acceptance inference from the exploratory single timings. Frozen original spec and baselines remain unchanged. The proposed 5% margin is not user-approved. Report scenario results, including regressions or inconclusive rows; no cross-scenario averaging.

## Matrix

- C Federalist: both norms, ab/ba/both, memory/disk, T=1 and T=4.
- C MNIST: both norms, ab, memory/disk, T=1 and T=4. Full ba query sets and ba/both command controls for L1-memory-T1, L2-memory-T1 and L2-disk-T4; reverse truth is independently generated rather than calling the data unavailable.
- C estimators/weights: Federalist both norms/directions, memory/disk, explicit and automatic sample sizing, cold/miss/hit caches; include exact/uniform/quadtree controls. Query-only harness results are separate from command timers that include building indexes or weights.
- H toy: k=1/100, default/complete-radius; default stored tables with query m32/beta0.02, and explicit build m32/page1024 with query m16/beta0.02.
- H GIST-1M-first100: all 1,000,000 supplied base vectors and first100 supplied queries, k=1/100, default/complete-radius. Query subset fixed by original order, not performance/quality selection. Ground truth uses the supplied first100 official top100 ID lists with distances recomputed independently in double precision from the original vectors. Build default parameters and explicit m32 (default page size), with valid m16/beta0.02 query overrides on the latter, all crossed with k and check mode. Independently exhaustively recompute top100 over every base point for these100 queries (and for all toy queries), not just distances at the supplied official IDs.

## Timing

Release GCC15 with matched flags/dependencies. C T=1/4 explicit; H single-thread inside Linux Docker. No concurrent benchmark/build/truth generation. Warmup pair excluded, then10 measured paired runs, alternating baseline/migrated order by pair index. Capture command time separately from query-only harness time, raw outputs, source/binary/data/projection hashes, resource observations. Primary cache protocol is warm OS cache; new process does not mean cold cache. Any separate disk-cache experiment may evict only task-owned files with POSIX_FADV_DONTNEED, never global caches or services.

Additional rows: one paired GIST default index-build/resource comparison; cold-index/warm-point comparisons for MNIST-L2-ab-disk-T1 and GIST-k100-default. Cold preparation advises only singly-linked task-owned index files, verifies at least99% nonresident OS pages with mincore, and never evicts original point data. The cold endpoint is **open plus query**, with both raw stages retained: migrated open deliberately validates/touches the entire index, so query-only time after open must not be called a cold-query result. Device caches are not controlled.

C query-only timing prewarms the OpenMP team before its timer; command timings retain actual application startup/setup behavior. Docker CPU quota4 and `OMP_DYNAMIC=FALSE, OMP_PROC_BIND=close, OMP_PLACES=cores` are held fixed. H remains one thread.

Statistic: median of paired migrated/baseline ratios; 95% percentile bootstrap interval using20,000 resamples of the10 pairs with fixed seed20260911. This method and seed are declared before the paired results. Keep small/noisy rows inconclusive rather than claiming a pass from nonsignificance.

## Accuracy

Compare complete frozen query sets to baseline and exact oracle. Numerical tolerance is abs1e-5 + rel1e-5 for float32 distances (before result inspection); it is NOT a tolerance for lost recall or substantive approximation changes. IDs may differ only under approved ties/order/backend rules, with per-query/rank changes and quality metrics reported. Official GIST top-k IDs are independently given, not inferred from approximate output. Tiny zero-distance/partial/duplicate fixtures use independent safe checks instead of aggregate division by zero.

Warmup TSVs pass per-query/rank quality checks before measured pairs begin. Any raw per-query recall loss must be independently attributable to substitutions with exactly equal float32-rounded distances from original coordinates; the loose distance tolerance is not used for this attribution. Preserve raw recall and the affected query IDs. Worse-than-tolerance distances, material weight changes or unattributed losses stop the job for parent investigation. Cached weight vectors are archived per sampling scenario.

## Execution and evidence

Large data/builds go under `/home/jonathanhu237/code/qalsh-parent-validation3` on SSD, not `/tmp` tmpfs. All code is written/exported locally then synced one-way. H builds/tests/queries run only in Docker. Baselines are fresh local git archives of the frozen SHAs, not directories trusted by name. Raw evidence is copied back locally. No commits/pushes or third-review pass implied by this protocol.

Version1 was deliberately interrupted after new completion-reporting regressions were found. Its raw observations and fingerprints remain under `paired-v1` and are not final-source evidence. Version2 uses the corrected completion/window state, unified numeric kernels and independently grouped projection arithmetic; its176 named scenarios are freshly measured. No old samples are relabeled as new-build measurements.
