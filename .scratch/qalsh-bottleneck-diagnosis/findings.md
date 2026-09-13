# Bottleneck diagnosis — current evidence (not performance acceptance)

Production remains 722ecfb. No optimized production variant has been created in this stage. Counter instrumentation has completed, and parent inspected the probe patch and equivalence results. This is a completed focused diagnostic, not a completed optimization or full-matrix quality/performance acceptance.

## 1. The prior timing environment was not isolated

Centaurus is a Ryzen 9 5900X with SMT: CPU20 shares physical core8 with CPU8. Binding to 20 does not reserve its sibling. An existing long-running old_algo process is permitted on all CPUs 0–23; no process was stopped or re-affinitized. Other services are also present. This does not establish which process caused a particular overlap.

Three fixed alternating original/722ecfb pairs were run on CPU20 using the identical repeat harness, full Federalist L1 AB memory population (1880 queries, 30 repetitions). Production headers/source hashes match 722ecfb. Consumer flags include QALSH4C_USE_OPENMP, -O3 and -fopenmp; library flags include QALSH_USE_OPENMP_SIMD and -fopenmp-simd. Source/build.ninja, binary/harness/data hashes and raw observations are retained under evidence/.

| pair | original ms/population | 722ecfb ms/population | CPU8 busy during original | CPU8 busy during 722ecfb |
| --- | ---: | ---: | ---: | ---: |
| 0 | 93.980 | 92.333 | 3.46% | 9.47% |
| 1 | 100.901 | 111.681 | 21.09% | 53.82% |
| 2 | 93.954 | 89.908 | 2.43% | 1.44% |

Same 722ecfb executable varies by roughly 24% from lowest to highest time, while its instruction counts are approximately 30.784–30.790 billion. High sibling activity coincides with the slowdown. This is strong evidence of environmental interference, not proof that every earlier reported regression was noise. All pairs are retained, none discarded. No performance acceptance or confidence interval is inferred from three diagnostic pairs.

Raw counters cover the entire process (including initialization/output); query timing excludes these. Repetitions amortize setup, not remove it. Median ratios (722ecfb/original) are: instructions 0.8485, cycles 0.9824, branch misses 1.2828; IPC 2.47 vs 2.86. Thus this workload does NOT support the blanket hypothesis that migration simply executes more instructions. Branch behavior and per-instruction cost require investigation.

## 2. Current C hotspots reconfirmed

Fresh cycles profile of 722ecfb (not the earlier pre-boundary-check-fix build):

| symbol | cycle sample share |
| --- | ---: |
| InMemoryIndex::scan_impl | 40.04% |
| DeliverRange<DefaultQalshStrategy> | 27.33% |
| InMemoryIndex::reset_cursor | 11.59% |
| DefaultQalshStrategy::next | 5.50% |
| search_impl_typed | 5.36% |
| ProjectPoint | 3.89% |

The original monolithic QalshAnnSearcher::Search contains 98.74% of its samples, so symbol shares alone do not provide a like-for-like internal cost breakdown. A second branch-misses profile of 722ecfb locates 39.68% in scan_impl, 27.66% in DeliverRange and 16.43% in reset_cursor. Sampling skid means individual instruction percentages are not exact instruction latencies.

Disassembly shows scanner samples concentrated around ClampToWindow endpoint checks/binary search, not exclusively function-entry boilerplate. DeliverRange samples concentrate around collision-count load/test/decrement paths. The default path has no per-hit virtual strategy call and no per-hit full snapshot deep copy. Current ClampToWindow is endpoint plus binary search, not a second linear scan of all hits.

All repeated outputs within each variant match IDs/counts/float32 distances. Original-vs-722ecfb has the known 158 distance-only changed queries and no ID/order/count changes. Matching answers is not proof of identical work or full quality acceptance.

## 3. H corroboration: different hotspot, same need to target hit processing

One corroborating full first-100 GIST query profile per variant, k100 complete-radius, original/migrated B+ layouts. This is not repeated timing acceptance. Coordinate/query/truth/metadata SHA-256 values match across data directories. Existing matched baseline binaries were reused; the head build points to frozen 722ecfb source.

722ecfb sample shares: H typed DeliverRange 67.75%, distance kernel 6.43%, EvaluateCandidate 3.87%, scanner 2.77%, ValidatePoint 2.54%, ClampToWindow 1.29%. Several libc samples account for copying. Original has unresolved samples; its internal shares cannot be compared confidently without symbol resolution.

Within the H driver, 23.59% + 23.10% of local-period samples land immediately after visited-byte loads in forward/reverse paths (~31.6% of whole-process cycle samples). Assembly already delays projection subtraction until AFTER checking visited, so 'avoid computing projection differences for visited points' is NOT a new optimization opportunity in this build. The visited flag is embedded in 16-byte HPointState entries; for 1M points that state array spans ~16MB. A compact visited representation or better latency overlap is a hypothesis, not yet a proven speedup, and belongs partly to the H consumer strategy rather than generic library traversal. Cannot label the conditional branch itself as the sole cause: dependent-memory stalls and misprediction must be distinguished.

An independently identified linear separator scan in current B+ reset versus binary search in old H is real source-level work, but B+ reset is not a dominant symbol in this GIST profile. It is not automatically the first optimization target merely because its asymptotic form is worse.

## 4. Next evidence / stop boundary

The separate local-source/remote-run instrumentation probe completed: work-probes/report.md and raw.json. Timings from instrumented builds are not performance evidence. Both variants deliver exactly 37,722,223 hits, update collision state 37,636,159 times, evaluate 1,881 candidates, and execute 7,003 radius rounds across 1,880 queries. Both perform 156,040 resets and 1,600,867 lower-bound comparisons. Current performs three fewer empty scans (582,779 total scans vs582,782). These aggregate counters strongly establish comparable major work volumes in this one scenario, not identical per-query event order for all workloads.

Current makes 1,114,661 ClampToWindow calls, of which 927,698 (83.23%) enter binary search, 85,233 (7.65%) yield an empty range, and 101,730 (9.13%) take the full-range/empty-input fast return. Available range length averages117.8, accepted length33.8 with median17; binary searches average6.826 iterations, totaling6,332,792. Thus the source comment that the far-endpoint-in-window condition is the 'usual case' is contradicted by this measured workload: partial windows dominate. Original uses38,778,213 per-entry window comparisons; current uses8,460,384 window probes. Fewer comparisons did not eliminate control-flow/latency cost.

Probe outputs were independently compared to each variant's uninstrumented outputs: all1,880 IDs/counts/float32 distances match in both comparisons. This verifies instrumentation transparency at the output surface; it does not waive historical original-vs-library distance differences or establish recall for untested workloads.

Next candidate selection must (a) preserve parameters and event/quality semantics, (b) address a measured high-frequency cost, (c) explicitly control or disclose physical-core sibling interference, (d) avoid repeating rejected branchless/top-k/source-layout experiments. Real physical-core isolation requires coordination or authority to reserve resources; this stage does not silently alter other processes. No final optimization acceptance claim is made.

## 5. Evidence-backed next hypothesis

First candidate for a subsequent implementation experiment: a vectorized contiguous-prefix window search in ClampToWindow, preserving the exact existing abs(query - projection) <= bound predicate, both traversal directions, logical range boundaries, and all hit/candidate ordering. Compare against the scalar endpoint/binary algorithm using the measured short/partial/full-range distribution; do not infer benefit just from comparison counts. Small SIMD blocks may overlap projection loads/comparisons and avoid the serial unpredictable binary-search chain, but may also do more memory work or lose on sparse/full windows. This is deliberately a hypothesis, not an implementation instruction already executed.

Required falsification checks: exhaustive short ranges and fixed-seed randomized sorted projections, duplicates, +/-0, adjacent representable values at the boundary, finite extreme coordinates and infinite bound, both directions, tails, and allowed unaligned entry storage. Compare prefix length and delivered event sequence exactly; then compare complete real-data output against722ecfb before paired timing. Do not replace the predicate with q±bound without separately proving float32 equivalence. Keep an architecture-portable fallback and avoid an API redesign or changing search parameters.

H state-locality is a secondary, separate hypothesis: the existing compiler already skips projection arithmetic on visited hits, so data layout/prefetching must target dependent state access instead. It affects the consumer-owned strategy and needs a separately scoped change; it is not automatically solved by optimizing the library window search.

This first stage has produced a verified cost/volume explanation and one prioritized falsifiable library hypothesis. Full dual-baseline improvement remains unachieved.
