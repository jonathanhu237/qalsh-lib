# Direct review 1 — SIMD window-prefix candidate

Status: changes requested; performance acceptance environment-blocked.
Fixed baseline: 722ecfbf8b426a3148d30f9cd6b393018bf31fab.
Fixed spec SHA-256: fa8a00a2104d341054531f018792e925183d346b2b3f80caabab4889428ee5f2.

Parent reviewed cumulative tracked diff and untracked src/window_clamp.h, both new tests, Stage-B runner/stats/quality tools, implementation report and raw records. No delegated reviewers; no new performance trials.

## Standards

No documented production-standard breach identified. SIMD is private, baseline SSE2 guarded, scalar fallback exists, and loads extract only projection words before arithmetic. No new allocations/API/format changes. git diff --check passes.

Possible Duplicated Code (non-blocking heuristic): src/window_clamp.h repeats the exact scalar binary-search tail in the #else branch and after #endif; one shared tail suffices. Do not change production merely for this cosmetic note while performance evidence is blocked unless it is useful to another necessary fix.

## Spec

### R1 — high: integrated trace and fallback coverage is incomplete

Spec sections5.5/5.6 require independent SIMD/reference event traces, partial/exhausted/deferred behavior and optimized/forced-scalar builds. CompareSearchTraces always uses scan_quantum=3. Memory/array quantum ranges are at most2/1 entries and range-mode chunks at most3; the SIMD loop requires range.count >=6, so these strategy traces cannot exercise the SIMD path. Public scan tests with max_entries7 in range scope CAN exercise it, but do not replace strategy/metadata trace coverage. Expand quantum/range/fixture sizes to definitely enter SIMD for both directions/backend boundaries, and assert the relevant paths/events are actually reached. TraceStrategy stops after its second round while many table entries remain, so its all-exhausted/deferred Evaluate(0) branch is not exercised by that fixture; add a deliberate exhausted/deferred scenario instead of only having the branch in code.

The scalar CMake target defines QALSH_WINDOW_FORCE_SCALAR only on the standalone helper test, not on the linked production library. Provide a distinct full forced-scalar library build and run the integrated/full tests against it; record compile flags and hashes. Keep production unchanged if this is only a test/report repair. Use unique test temp directories rather than shared /tmp/qalsh-window-trace-*.qalsh to avoid cross-build test collisions.

### R2 — high: independent quality comparator is diagnostic, not fail-closed

stage_b_quality_compare.cc prints exact-hit counts and global maximum ratios, but returns success unconditionally; it does not reject per-query quality regression (including the both-non-exact case where recall stays0 but approximation ratio worsens). ReadIds also does not validate point IDs against nb before indexing coordinates in L1. Spec requires per-scenario/query quality gates rather than green report output. Add bounded positive/negative fixtures and explicit failure exits for recall loss, worse exact selected distance/approximation where relevant, missing/duplicate/out-of-range IDs, malformed/non-finite data as applicable. Preserve equivalent ties; do not relax tolerances. Reject same-file baseline/candidate evidence. Record per-query comparison status instead of relying on equality of two global maxima.

This is a verifier defect, NOT evidence of actual quality loss in measured candidate results: parent checked1880 original/candidate IDs match in the first triplet of each attempt, and candidate/722ecfb result hashes are identical. However, the checker needs to be trustworthy before extension to Stage C.

### E1 — environment blocker, not a production-code defect

Parent independently recomputed raw /proc/stat sibling occupancy excluding the selected CPU and confirmed primary contaminated triplets[1,3,12] (25%) and adjustment[1,2,3,4,5,7,8,9,10] (75%). Both exceed the frozen20% rule. Stage B remains blocked; Stage C correctly not started. Do NOT run more timing batches, change CPUs again, discard samples, or alter thresholds to get a pass. Need a user-authorized reserved physical-core/time window before further performance acceptance. Do not erase the candidate just because interference prevents adjudication.

## Disposition and fix counters

Standards:0 hard findings;1 optional duplication note.
Spec:2 fixable findings (R1/R2 high), plus E1 environment blocker.
Review count1/3. R1/R2 fixes completed0 each; next fresh Luna is attempt1. No performance retry is part of that fix. Parent confirmed all listed candidate source hashes and unchanged Spec match the fingerprint. No commit/push.
