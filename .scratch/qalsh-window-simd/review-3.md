# Direct review 3 — final bounded candidate review

Status: code/test findings addressed in reviewed scope; PERFORMANCE NOT ACCEPTED (E1).
Fixed baseline722ecfbf8b426a3148d30f9cd6b393018bf31fab.
Fixed spec hash fa8a00a2104d341054531f018792e925183d346b2b3f80caabab4889428ee5f2.

## Standards

No hard documented-standard breach found in the cumulative production changes. The private helper remains baseline-ISA SSE2 with scalar fallback; no installed API, search parameter, persistent format or allocation change. Previous optional duplicate scalar-tail heuristic remains non-blocking; production was not changed cosmetically between measurements. git diff --check passes. All candidate files listed in source-fingerprint.json and the fixed Spec hash were checked directly.

## Spec

R1 closed in the reviewed scope: separate full SIMD/forced-scalar library builds, independent scalar-reference traces with nonempty partial SIMD-sized ranges, both directions and all backends, explicit exhaustion/deferred events and unique temporary directories. Parent reran both full CTest suites on Centaurus:7/7 each. Their library hashes match the recorded optimized/scalar artifacts. Prior ASan/UBSan2/2 and consumer C23/23, H1/1, installed consumer smoke evidence remains explicitly prior execution rather than a new parent run.

R2 concrete false-pass defects closed by parent execution: the checker now uses compensated higher-precision accumulation, magnitude-dependent error estimates, exact handling of zero, unconditional ratio comparison and reported-float rounding cells. Parent compiled the final checker with g++-15 -O2 -Wall -Wextra -Wpedantic -Werror and reran the expanded positive/negative fixtures. Both parent counterexamples now exit5 and identify the correct failure reasons. No arbitrary absolute floor can relabel the tested zero/nonzero case as a tie. This remains a numerical reference with uncertainty bounds, not a formal rational-arithmetic proof for every conceivable dataset; its interval classification must not be generalized into uncovered full-matrix acceptance.

Parent inspected both existing final-checker Federalist logs:1880 per-query passes each, no quality failures; observed original/candidate selected IDs match and candidate/722ecfb results are identical. Parent did not rerun the exhaustive real-data reference during this final review; it verified logs/source/binary binding and independently reran bounded checker fixtures.

E1 remains blocking: primary timing had3/12 contaminated triplets, adjustment9/12. The fixed threshold disallows performance acceptance. Favorable primary ratios0.9784/0.9580 do not override contamination. No additional Stage-B/Stage-C timing or CPU adjustment was run. Stage C and broader double-baseline quality/performance matrix remain open, so the user's overall performance objective is NOT achieved.

## Parent final verification

Commands executed on Centaurus after local tool edits were rsynced to `/tmp/qalsh-window-parent-final`:

- g++-15 -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror stage_b_quality_compare.cc -o quality
- python3 test_stage_b_quality_compare.py
- python3 parent_quality_edge_repro.py ./quality
- ctest --test-dir /home/jonathanhu237/code/qalsh-window-simd-fix1/build-optimized --output-on-failure
- ctest --test-dir /home/jonathanhu237/code/qalsh-window-simd-fix1/build-forcedscalar --output-on-failure
- sha256sum of final checker source/binary and both library archives.

Full output: parent-final-validation.log. Final checker source564d56bcad5ace42672e4894d4e7a10e0f0464d7e3d18cac4dcbc9cac7cf82b9; binary b54e341a259557bdf9d13b498565e6b82138e3f1e26fe6189e99e52558f8abfa. Optimized library d2a15e985f00bb336c6c10ab38c1b131461352b0bd339f26ad2ba8054d6736c4; forced-scalar e7a055e907c1343c7ca2c628dfcbda0c6995f5b29543f4924e7a144a81a75902.

## Prompt-to-artifact audit

| Explicit requirement | Evidence surface | Disposition |
| --- | --- | --- |
| Publish Spec, use fresh Luna Max implementing without final review/commit | spec.md, review-ledger.md, implementation/fix handoffs | Done; fixed baseline/spec unchanged |
| Narrow private SIMD window prefix, scalar fallback, unchanged API/parameters/format | src/window_clamp.h, six call sites in src/qalsh.cc, CMakeLists.txt | Implemented; no unrelated production changes |
| Exact float32 predicate/prefix, directions/tails/alignment/extremes | tests/window_clamp_test.cc and independent oracle; sanitizer logs | Covered by focused corpus, not a universal formal proof |
| Independent public traces, fallback full build, partial/exhausted/deferred behavior | tests/window_trace_test.cc; parent7/7 +7/7 runs | R1 fixed and verified |
| Fail-closed precise quality check, ties, zero, malformed evidence | final comparator, rerunnable fixtures, parent counterexamples | R2 fixes verified within tested scope |
| Stage A explicit distribution and adverse populations | stage-a-20260913.txt | Recorded: includes short/tail/long regressions; only screening |
| Stage B all-sample paired dual-baseline timing and noise rules | both raw-stageb directories, stats, parent review1 recomputation | Environment-blocked, not accepted |
| Stage C16-case matrix; global original/head quality and overall speed | Spec6, explicit Stage-C disposition | NOT RUN because Stage B failed its environment gate |
| Build/open/RSS costs | initial resource-smoke/report | Focused observations only; broader coverage incomplete |
| Preserve provenance, local edits/remote verification, no automatic commits | fingerprints, parent remote log, unchanged HEAD | Done for retained work; no commit/push |
| Direct review, maximum3 reviews, fresh fixes | review-1/2/3; R1attempt1, R2attempts1/2 | Three reviews complete; no further delegation |

## Stop condition / next input

Retain the uncommitted candidate and all evidence as a correctness-tested, performance-unaccepted implementation. It has NOT been shown conclusively faster and is not rejected as conclusively slower. Further performance acceptance needs a user-authorized reserved physical-core/SMT-sibling window or another sufficiently controlled host. Do not kill or re-affinitize unrelated tasks, alter governors, retry batches until favorable, or discard contaminated runs without that input and a predeclared continuation protocol. Full acceptance still requires Stage C and the broader outstanding gates. No commit/push while blocked.
