# Implementation loop review ledger

Status: three-review loop complete; not accepted

## Workflow and fixed point

- User authorized implementation with implement-loop on 2026-09-12.
- Luna Max executes implement-without-review and self-tests. The parent performs Standards and Spec review directly, without reviewer agents, as required by implement-loop.
- Maximum three parent reviews. Findings return to the same implementation agent. If an issue survives two delegated fix attempts, the parent fixes and verifies it directly before reporting.
- No commits, pushes, or merges are authorized by this workflow.
- The fixed point is the actual source at the start of this implementation turn, including the existing uncommitted migration, not merely Git HEAD. Source snapshots and hashes are recorded in `pre-implementation-manifest.json`. Diff those snapshots against current source for review. Historical Git baselines remain the comparison targets for migration quality/performance.
- This avoids both reviewing unrelated pre-existing changes as newly introduced and hiding new files behind an empty committed diff.

## Review state

- Formal parent reviews completed: 3/3.
- Reviews: review-1.md, review-2.md, review-3.md. R3 resolved. R1 (L1 memory) and R2 (acceptance evidence) remain after two delegated fix attempts and are now owned directly by the parent for repair/verification. N1 numerical policy remains pending user input.
- Current implementation is not yet accepted.

## Acceptance audit notes

- Source of truth: `spec.md` in this effort, with ADR 0001 and existing domain/agent instructions.
- Existing historical comparison scripts have hardcoded output directories, resume behavior, and older float/tie allowances. They cannot be reused as acceptance policy unchanged.
- New measurements require fresh source/binary/harness fingerprints and output directories. Report exact result differences; do not silently treat the old abs/rel tolerance or tie rules as approved.
- Main timing endpoint is repeated queries after one index open. Full-command, construction, opening, storage, and memory measurements remain separate.
- Centaurus connectivity and the old validation directories were confirmed before implementation. Other user services are running there; no service disruption or global cache dropping is permitted.
- At the first remote build checkpoint, an older `/tmp/old_algo` process (PID 416377, started Sep 11 03:59:47 server time) had consumed one CPU for about 31 hours; its output had not changed since Sep 11 04:28. The parent did not stop this pre-existing process. Benchmark records must account for host competition and affinity rather than assuming an idle server.
- The implementation agent initially ran macOS smoke tests, including H. Those do not satisfy the required Centaurus/Linux Docker workflow and are not accepted as the required test evidence; the parent requested reruns in the correct environment. Remote Docker compilation was subsequently observed under the new compatibility workspace.
- Remote Release logs subsequently show library 4/4, C with Federalist fixtures 23/23, and H 1/1 tests passed in Linux Docker. This is correctness evidence, not a performance acceptance decision.
- The first H toy pilot reports identical ordered results between current B+ and array layouts. Compared with the frozen H baseline, IDs/counts match but reported distances differ by up to 1 ULP (k=1) or 2 ULP (k=100). The parent independently checked query 7 / point 8724: old printed distance 13102.7197, current 13102.7207, independent double distance 13102.720417368213, float32-rounded 13102.720703125. The current result is closer in this example; this does not establish all-case acceptance.
- The parent asked the user to resolve the deferred accuracy policy with a concrete choice: at most 2 ULP with identical IDs/order and independent distance validation, or bitwise distance identity. This question is pending; no tolerance is assumed. Other implementation and validation continue independently.
- The later C Federalist pilot found identical IDs/order but up to 7–8 ULP distance differences. The parent checked L2 ab query 227 / point 1081: old 3.82254934, current 3.82255101, independent double 3.8225509862437175, float32-rounded 3.8225510120391846 (current). The initial H-only 2 ULP question was explicitly superseded with a project-level choice: identical IDs/order plus independent mathematical distance validation, allowing legacy rounding differences, versus reproducing legacy distances bitwise. The replacement question is still pending; no numerical exception is approved.
- The parent's independent returned-distance validator is available under `parent-tools/check_returned_distances.py`. It checks distances for returned IDs, not exhaustive ANN truth, and leaves exact output comparison separate.
- Federalist memory pilot raw logs also show unresolved L1 slowdowns (ab: 93.188 to 105.098 ms at T1, 24.978 to 28.197 ms at T4; ba: 85.831 to 91.356 ms at T1). These single-run observations justify targeted profiling but are not final paired results. The parent directed the implementation agent to address them rather than report only favorable disk timings.

- During delegated fix attempt 1, parent audited all returned distances in 40 Federalist and 12 toy pilot TSVs independently. Every current distance equals the float32-rounded double reference; baseline discrepancies are quantified in parent-distance-audit/summary.json. Coordinate hashes matched the remote inputs. This supports legacy-rounding diagnosis, not a numerical-policy waiver or final-source acceptance.

- During fix attempt2, parent prepared and smoke-validated a Linux resource helper. Direct Python child rusage imposed an approximately25MiB launcher RSS floor on /bin/true; those smoke results are retained as invalid resource methodology. Updated parent-tools/measure_resources.py uses GNU time for executable peakRSS and samples the timed executable via /proc. Corrected /bin/true peak1480KiB; sampled /bin/sleep demonstrates separate anonymous/file RSS. Resource runs remain separate from uninstrumented primary timings.

## Parent takeover outcome

The parent directly repaired the focused R1 regression by validating external
index IDs at the adapter and removing redundant checks only from the private
built-in handler. A failing malformed-external-hit regression passed after the
fix; final library/C/H/package suites passed. Ten alternating pairs at T1/T4
now show ratios0.96855/0.97212 with upper95%bounds below1.0. R1 is resolved
for its declared focused scenario, not a universal performance claim.

Parent directly extended R2 to42final-source scenarios /84successful commands,
including full MNIST reverse, strict H toy overrides and GIST B+ controls. All
requested result counts are present. R2 remains open for the unmeasured full
paired matrix and listed build/layout/control gaps. N1 includes float-rounding
and concrete equal-distance ordering/membership differences; no waiver was
adopted. See parent-verification.md for final source hashes and evidence.

Three formal reviews and required direct parent repair/verification are complete.
No fourth review or additional delegation was performed. No commit/push/merge.
