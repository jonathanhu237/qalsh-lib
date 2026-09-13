# Focused bottleneck diagnosis (not an optimization acceptance round)

Production baseline stays 722ecfbf8b426a3148d30f9cd6b393018bf31fab. No production edits or automatic commits. Original C/H and library baseline remain the comparison targets; approved precision policy is unchanged.

Stage 1 asks whether migration changes logical work or cost per unit, and identifies the largest verified avoidable cost. Do not repeat the prior full acceptance matrix while no promising candidate exists.

Initial bounded diagnostic: Federalist C L1 AB memory T1, complete 1880-query population repeated 30 times on one open using the existing same-source repeat harness. Baseline library binaries and original consumer repeat binaries are inspected and hashed before use. Three alternating original/722ecfb pairs with perf counters, then one sampling profile each. These are diagnostic repetitions, not ten-pair acceptance or claims about all workloads. Per-process counters include initialization/output; 30 repetitions amortize those costs but do not eliminate them. Query stdout timings remain separate.

Host: Centaurus Ryzen 5900X, pin to logical CPU20 (physical core8; sibling CPU8); report sibling occupancy, involuntary context switches, effective cycle/time ratio and competing workload. Do not stop/re-affinitize unrelated work, change governors or drop caches. The old_algo process is unrestricted and active, so the machine is NOT isolated. Samples with activity are retained and labeled; instruction counts and stable work counts help distinguish scheduling noise from algorithmic work. Do not retroactively select favorable runs.

Source/flags/harness/data hashes, perf raw counters, stdout, result TSVs, machine observations and reports are retained. Verify output IDs/counts between repeat runs; distance differences against original are historical and require independent quality evaluation before accepting any future optimization. Instrumented work counts and uninstrumented timings are separate; do not infer equivalent work just from matching final answers.

After this case, use existing H/GIST and C/MNIST profiles to decide one bounded corroboration rather than automatically starting a large matrix. A successful diagnosis yields a cost breakdown, uncertainty/limitations and at most a few falsifiable candidate hypotheses, not a claim of improved performance.
