# Performance continuation result after stale-job cleanup

Status: E1 remains environment-blocked; no optimization acceptance.
Production candidate, Spec, baselines and quality data are unchanged. User authorized continuing the unfinished task. No new production changes/commit/push.

## Intervention completed

Confirmed and stopped only PID416377, an orphaned user-owned QALSH `old_algo` benchmark that had consumed one core for ~46h. Its executable and stdout had been deleted and its input directory no longer existed. Exact command/ownership/orphan/deletion checks were performed before sending SIGTERM with pidfd. Both it and its orphaned wrapper exited. Initial older-Python compatibility failure sent no signal and is retained. No other process was stopped or re-affinitized, and no governor/service setting was changed.

Evidence: continuation-stale-benchmark-pidfd.log (and initial compatibility log).

## Predeclared continuation test

The new continuation-protocol.md was written before timing. Three1-second preflight samples selected CPU6/sibling18 with at most2% busy during preflight. This was quiet at selection time, NOT a reserved physical core. Exactly12 triplets used the fixed six orders twice; complete1880-query Federalist population with one warmup and30 measured repetitions. All36 results were retained. Before/after binaries/harness and dataset hashes matched the frozen Stage-B provenance.

All-sample results (ms per complete query population):

| comparison | baseline median | candidate median | geometric ratio | paired bootstrap95% |
| --- | ---: | ---: | ---: | --- |
| candidate/722ecfb |90.34094|89.81657|0.983676|[0.954652,1.009280]|
| candidate/original C |93.35284|89.81657|0.957460|[0.947447,0.966252]|

Candidate/722ecfb is statistically inconclusive even before the environment gate.8/12 triplets (66.7%) exceeded the5% sibling-busy threshold, far above the allowed20% fraction. Effective frequencies were stable (~4.49–4.50GHz); sibling activity was the flag source. For example baseline B in triplet7 experienced35.03% sibling occupancy, zero CPU migrations, and3.22s task-clock versus typical2.78s. Selected CPU6 remained ~100% busy, ruling out simple migration of the benchmark itself to its sibling as the cause of the extra recorded occupancy.

Thus stale-job cleanup removed one real interferer but did not reserve the core against bursts from the remaining shared system workload. No unsupported claim is made about which remaining process caused each overlap. No more CPU/batch retries were attempted.

## Correctness and evidence preservation

All candidate result files exactly match722ecfb. All original/Candidate hashes match the independently validated previous output hashes for the same data and settings (original dfeb3c2c..., head/candidate48695f1a...). Per-process warm/final hashes also agree. Existing final-checker Federalist quality evidence is reusable for these identical outputs/data; this is not new full-project quality coverage.

Artifacts: raw-stageb-cleanup-20260913/, continuation-stageb-stats.json, continuation-stageb-run.log, continuation-preflight.json, continuation-before.sha256, continuation-after.txt and the executable analysis script continuation-stats.py. Earlier two contaminated batches remain unchanged. All statistics include contaminated samples.

## Required next input

Stage B still does not pass, so Stage C was not started. Another quiet-core preflight alone is insufficient. Before more acceptance timing, obtain permission for temporary scheduling isolation: exclude other user/service workloads from one physical core and its SMT sibling while leaving the services running, with exact settings saved/restored; alternatively obtain a genuinely quiet/dedicated host/window. No such service-affinity/cgroup/global-scheduler adjustment was made under the stale-job cleanup authorization. Do not promise that isolation will make this candidate faster: it will make the comparison adjudicable.
