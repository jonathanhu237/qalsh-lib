# Controlled Stage-B result under temporary cgroup isolation

Status: ISOLATION VERIFIED AND RESTORED; SIMD CANDIDATE PERFORMANCE GATE NOT PASSED.

## User authorization and executed isolation

The user approved a runtime-only core exclusion with services kept running and automatic rollback. Centaurus supports cgroup v2/systemd259 and noninteractive sudo. An independent10-minute rollback timer was activated before modifying masks. system.slice and user.slice were restricted to0-5,7-17,19-23, while the benchmark ran in its own top-level transient slice/service with effective CPUs6,18 and per-process affinity6. No service was stopped/restarted; no governor, IRQ or SMT setting was changed. The benchmark service also had a480-second maximum runtime and control-group cleanup.

Both slice masks were restored and verified after approximately102 seconds. Original and final AllowedCPUs are inherited/empty; EffectiveCPUs are0-23. Restoration recorded no errors. The benchmark service/slice and rollback timer are all inactive. See isolation-controller.log, isolation-context-20260913.json and isolation-restoration.log. This verifies actual cgroup exclusion, not merely taskset of the benchmark.

## Frozen test and results

Production SIMD/helper, audited original-C binary,722ecfb binary, dataset and harness were unchanged and hashed before/after. The run used the predeclared12 triplets (six variant permutations twice), full1880 Federalist L1 AB memory queries, T1, one untimed warm population and30 measured populations per process. No slow run or outlier was removed. Raw data: raw-stageb-isolated-20260913/; statistics: isolation-stageb-stats.json.

| comparison | baseline median ms/population | candidate median | geometric ratio | paired95% interval | result |
| --- | ---: | ---: | ---: | --- | --- |
| candidate /722ecfb |89.101830|89.257272|0.998264|[0.990642,1.004038]|inconclusive, required win NOT shown|
| candidate /original C |92.917403|89.257272|0.960319|[0.957166,0.963196]|faster in this focused case|

Bootstrap:20,000 paired log resamples, deterministic seed20260912. All12 triplets meet the declared contamination rule; maximum sampled sibling occupancy is0.361%, with zero contaminated triplets. Thus E1 no longer explains away the absence of a demonstrated candidate/722ecfb win in this batch.

The approximately4% advantage over original C cannot be credited to this new SIMD change alone:722ecfb already has essentially the same advantage. Candidate/722ecfb is statistically indistinguishable from parity in the declared screen, and its median is slightly slower. This is NOT proof the candidate is universally slower or that all SIMD approaches are exhausted; it is sufficient to stop this candidate's acceptance progression under the fixed Spec.

## Correctness and disposition

All36 result files are stable. Candidate outputs are byte-identical to722ecfb and have the same output/data hashes as the independently validated Federalist quality records; original output hashes also match. Production and test code were not modified during isolation.

Stage B requires intervals below1 against BOTH baselines. It did not pass, so Stage C was NOT started. Full-project query-performance acceptance remains unachieved. Previous noisy batches remain preserved; no selective replacement of unfavorable data or repetition-until-success occurred. This controlled run is a separately predeclared, user-authorized environment intervention.

Keep the candidate explicitly unaccepted and uncommitted pending the user's disposition; do not merge it as a performance win. Do not continue repeating this same primary benchmark or expand the costly matrix without a new evidence-backed candidate. A new candidate requires a separately bounded hypothesis/spec, not retroactively altering this one's acceptance rules.

No commit/push. Normal CPU allocation fully restored. E1 mitigation is now demonstrated for a shortT1 window; any future tests needing more cores or longer exclusion must separately preserve bounded rollback and resource authorization.
