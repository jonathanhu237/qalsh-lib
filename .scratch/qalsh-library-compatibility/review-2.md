# Review 2 — after delegated fix attempt 1

Parent direct Standards/Spec review under implement-loop. Source pinned by current-source-fingerprint-fix1.json. All recorded source hashes and original requirement hashes independently verified unchanged. Parent independently read the Docker/package logs and recomputed all 20 paired summaries from raw query logs; all 20 exact comparisons were recomputed from TSVs.

## Standards

No blocking documented-standard violation found. The performance changes are confined to the standard typed strategy's ID decoding and scheduler inlining; custom strategies retain projection observations. Candidate ID validation and logical range consumption remain present. The common driver still owns search state; no consumer-specific mode was introduced. Minor duplicated decoding scaffolding is a maintainability tradeoff, not a blocking rule violation.

## Spec

- **R1 / P1 remains after fix attempt 1.** Federalist ab L1 memory, ten pairs per thread count: T1 geometric ratio 1.0702995, 95% paired-log bootstrap [1.0649058,1.0762589]; T4 1.0755331 [1.0618461,1.0865833]. All pairs preserve IDs/counts, but no numerical-policy waiver is approved. Both timing intervals show a residual slowdown. Continue targeted shared-loop profiling/optimization without changing search effort or stop timing, then rerun fresh paired evidence.
- **R2 / P1 remains partial after fix attempt 1.** Applicable correctness checks now pass (library4/4, C23/23, H1/1; source and installed consumers1/1 each), and r2-scenario-matrix.md declares pending scenarios. However, declaration is not execution: C4–C7, H1–H4, actual seeded-projection verification on final source, and measured resource/command rows remain incomplete. Matrix should explicitly retain MNIST reverse and real exact-control cases as required coverage; C6 currently names only A→B. Complete representative rows with provenance, preserving numerical/quality failures and inconclusive timing, and retain required gaps visibly rather than asserting acceptance.
- **R3 closed.** Public factory defaults, both persistent layouts, separate-process reopen, deferred/strategy controls, quantum and range multi-page event equivalence, post-exhaustion radius growth before exactly-once evaluation, valid-header tail truncation, and malformed array entry checks are now present. Parent checked the corruption mutation targets the actual data_offset and preserves envelope/file length. Latest library Docker test passes4/4; ordinary package consumption also passes.

## Pending decision and next attempt

N1 remains unanswered: tested IDs/order match but legacy floating distances differ. Independent references favor current mathematics; this is not permission to adopt a new numerical policy.

Result: Standards 0 blocking findings; Spec 2 open P1 findings, 1 resolved. Formal review2/3 complete. R1 and R2 enter delegated fix attempt2. If either remains after that attempt, parent takes over that issue directly and verifies work before reporting, as required by implement-loop.
