# User-authorized performance continuation after stale benchmark cleanup

Status: predeclared continuation, not performance accepted.
User explicitly requested continuing the unfinished task after review3. Original fixed SIMD Spec, production candidate, baselines, quality policy and contamination thresholds remain unchanged. No new production optimization or fourth code-review round is implied.

## Material environment intervention

Only verified PID416377, owned by the user, was sent SIGTERM via pidfd after confirming exact QALSH command-line identity, orphaned parent, deleted executable, deleted stdout and absence of the referenced /tmp/qalsh-acceptance input directory. It had consumed a core for ~46h. No service, other process affinity, governor, SMT or cache configuration was changed. Identity/intervention log: continuation-stale-benchmark-pidfd.log. An initial tool compatibility failure sent no signal and is retained separately.

This is a changed test environment, not another attempt to select a lucky sample from the earlier state. Keep both previous contaminated batches and this continuation separately visible.

## Frozen new batch

Exactly one new Stage-B batch: twelve triplets in ABC,ACB,BAC,BCA,CAB,CBA order twice, original C /722ecfb/candidate, Federalist L1 AB memory T1, all1880 queries, one untimed warm population and30 timed full populations. Reuse verified unchanged matched binaries/harness. New output tag `stageb-cleanup-20260913`; refuse existing output directory. No sample removal, no changing CPUs after seeing timings, no additional batch if results are unfavorable.

Preflight before timing: three1-second utilization intervals for all CPU sibling pairs; only one logical CPU per physical core is eligible, choose the lower-numbered sibling. Minimize the maximum busy fraction across all three intervals and both siblings (tie-break physical representative number). Save topology and all raw samples. This selects a currently quiet core but does not reserve it or guarantee exclusivity.

All-sample paired-log bootstrap20,000 resamples seed20260912,95% intervals; both candidate/722ecfb and candidate/original must be below1 for Stage B to pass. Same contamination rules: sibling>5% or effective-frequency spread>5% flags a triplet; more than20% contaminated blocks performance acceptance even if its interval is favorable. Compute sibling occupancy by excluding the selected CPU, not by assuming a list position. Record all counts and raw times; no conditioned clean-subset result is primary.

Before/after binary hashes must match frozen source-fingerprint.json; dataset and harness identities must match prior Stage B. Verify per-run output SHA/checksum stability and candidate/722ecfb equality. Do not call this run full-project quality acceptance.

If Stage B passes, continue the fixed Stage-C16 cases with equivalent controlled measurement and independent quality gates. If it fails statistically, report the candidate has not demonstrated its required primary win; do not expand the expensive matrix. If still contaminated, stop and ask for an actually reserved core/window rather than trying more CPUs or disturbing services.
