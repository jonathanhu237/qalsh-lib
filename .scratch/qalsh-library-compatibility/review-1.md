# Review 1 — initial implementation

Fixed point: pre-implementation-manifest.json source snapshots, including uncommitted files. Reviewed source: current-source-fingerprint.json. Parent direct review under implement-loop; no reviewer agents. No commits were made.

## Standards

No blocking documented-standard violation found in the new changes. The public TableScanSchedule removes generic queue/exhaustion bookkeeping from H while leaving likelihood state with H. The persistent factory and shared search driver cover concrete requirements rather than speculative extension points. Format-specific validation remains encapsulated. This is a source review, not a claim of complete acceptance.

## Spec

1. **R1 / P1 — unresolved common-engine L1 memory regression.** Spec Accuracy and performance requirements requires no regression per scenario; Consumer integration and performance recovery requires profiling shared costs. Post-change Federalist ab L1 pilot is 93.052→102.502 ms at T1 and 25.130→27.559 ms at T4. Earlier ba also regressed. These pilots are not final statistical evidence, but the consistent slowdown needs diagnosis and recovery. Profile setup, traversal, strategy, distance, result maintenance; preserve effort/stopping semantics. Validate on fresh alternating paired optimized builds with source and binary hashes.

2. **R2 / P1 — incomplete current-source acceptance evidence.** Spec Comparison protocol requires a frozen representative matrix, >=10 alternating/randomized pairs, actual seeded projections, independent quality, consumer workflows, resources, and current-source provenance. Present artifacts are partial pilots; GIST, full C controls/directions, array MNIST, package checks and post-optimization full C suite are not established. Freeze a concrete matrix and protocol before further timings, run required coverage, and explicitly retain failures/inconclusive rows. Do not reuse prior-source results as current evidence or interpret numeric discrepancies as approved. Quality gates precede speed conclusions.

3. **R3 / P2 — new sorted-array lifecycle/strategy coverage is partial.** Spec Functional coverage 1, 4, 6 asks for default build selection, delayed work after radius growth/exhaustion, and integrity on both persistent formats. The factory loop in tests/public_test.cc explicitly selects each layout and only calls CheckSearch/CheckQuantumOne; deferred and strategy-control checks remain memory/B+ only, and truncation testing calls BPlusTreeIndex directly. Extend public seam tests to exercise omitted default options and both formats for deferred controls and meaningful malformed/truncated file rejection; preserve separate-process reopen. Run full applicable tests and installed/source package consumer validation on Centaurus.

## Pending user decision

N1: baseline and new outputs have equal IDs/order in tested cases but floating distances differ (up to 8 ULP observed). Independent examples favor the new mathematical result. The deferred numerical policy remains unanswered; no tolerance or tie exemption is approved. This prevents unconditional acceptance but is not authorization to replace the numerical policy during fixes.

Result: Standards 0 blocking findings; Spec 3 findings, worst P1. Formal review 1/3 complete. R1/R2/R3 enter delegated fix attempt 1.
