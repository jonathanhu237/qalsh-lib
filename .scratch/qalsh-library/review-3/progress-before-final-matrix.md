# Parent review 3 / direct F01 fix — work in progress

This is NOT a completed review or acceptance report. Two parent reviews are complete; the third cumulative review is in progress. F01/F02 have exhausted two delegated fix attempts; further work is owned directly by the parent.

## Attempt-2 evidence inspected

Read `fix-attempt-2.md`; archived the child's top-level `.out` artifacts locally under `review-3/attempt-2-evidence/`. The report explicitly leaves performance/coverage open: C roughly 2.0–2.5x baseline, H roughly 1.5x. No acceptance. Frozen spec SHA-256 remains ea1a6a7c83bb850f3847557aef84f9116da38a2b5ad1f3c6937c3a4a81cc6dc4.

## Parent profiling / experiments

Centaurus, child final Release C executable, MNIST L2 A→B, one thread, fixed seed, c=2:

    perf record -q -F 199 -o /tmp/qalsh-parent-review3/c-before.perf \
      /tmp/qalsh-fix-attempt2/build-c-release/qalsh4c --use-fixed-seed -T 1 \
      estimate -p 2 -d /tmp/qalsh-fix-attempt2/mnist-migrated --direction ab ann qalsh -c 2

Parent observed 23257.473 ms, estimate3547537.5 / truth3172158 / error11.83%. perf self samples: 36.90% InMemoryIndex::scan, 33.36% DefaultQalshStrategy::accept_projection_hit, 23.79% SearchHitCallback, 2.23% index construction. Thus per-hit dispatch/traversal/state maintenance, not distance, is the principal hot path for this scenario.

A separate CMake IPO/LTO experiment (no permanent build setting change) was worse: 36627.496ms. Do not adopt LTO as the purported fix.

## Parent changes so far

Only `include/qalsh/qalsh.h` and `src/qalsh.cc` changed directly:

- Each built-in backend's public scanner and engine scanner now instantiate ONE private scanner implementation; no duplicated fast/generic traversal body or additional public strategy callback.
- Index/default-strategy types are selected outside the per-hit loop, with an ordinary type-erased fallback for external indexes/strategies.
- Default candidate helper remains private; inline its existing rule rather than implementing a second rule.
- Memory traversal keeps position local, commits it on scope exit, and removes impossible crossing tests: positions start on opposite sides of lower_bound and only move apart.
- Latest optimization keeps hit counts local to the scan and publishes them before every observable strategy/evaluation/termination callback and scan boundary. Generic strategies still receive current snapshots on every hit. This must be checked against live-statistics regressions before acceptance.

Earlier intermediate private-scanner versions compiled and passed all23 C tests remotely but did NOT fix performance (22957–28198ms in single exploratory runs). These were experiments, not performance evidence. Latest local-position/lazy-snapshot version has NOT yet been built or executed remotely.

## Validation interruption

Centaurus SSH became intermittent, then repeated direct, rsync and dedicated-control-connection attempts failed with `Connection closed by 42.194.236.126 port22` / `UNKNOWN port65535`. A single short reachability probe succeeded between failures, but source sync/build attempts did not. Reported this to user; did not move heavy validation locally.

Latest source passes ONLY lightweight local `c++ -std=c++20 -fsyntax-only -Iinclude src/qalsh.cc`. All3 working-tree `git diff --check` commands pass. This is NOT correctness/performance validation. No H host runs were performed by the parent.

## Resume

1. Re-establish Centaurus transport; resync current local library source to `/tmp/qalsh-parent-review3/qalsh-lib/` (do not assume last failed rsync succeeded).
2. Rebuild `/tmp/qalsh-parent-review3/c`; run core/C tests and both parent repros. Add permanent equivalence/live-snapshot cases across internal and external-index dispatch. Validate exception/cursor position semantics.
3. Reprofile latest version, compare actual baseline with matched flags/inputs, then continue direct F01 optimization; do not accept a cosmetic refactor with unchanged/worse speed.
4. Run H ONLY in Linux Docker and verify its quality again, along with cumulative review of pending changes.
5. Complete F02 matrix/evidence, final source hashes, and actual third review. Do not launch another Luna fix for F01/F02 or claim the third review already passed.

No commits, pushes, merges, baseline/spec changes, or acceptance claims.
