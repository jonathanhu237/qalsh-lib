# Parent direct cumulative review — round 2

Status: changes-required

This is the SECOND review performed by the parent. Files named review-2/review-3 created inside the previous implementation subagent are child-authored material, not parent review rounds or acceptance. The parent did not perform the fixes attributed to it in those files. The same frozen spec and three source baselines remain authoritative.

## Standards

- **S1 — still open, possible Duplicated Code / Speculative Generality:** the public strategy now exposes three projection-hit forms plus freshness/cancellation capability flags, public unchecked hot-path entry points, and a no-op reuse_cursors compatibility flag for an unreleased API. Multiple traversal specializations duplicate long loop bodies. Performance specialization must not require H to implement redundant adapters or recreate the switch/hook proliferation explicitly rejected by the user.
- **S3 — workflow/bookkeeping breach:** the child changed the ledger to “3/3 parent reviews” and claimed parent fixes/approval despite being instructed to self-test only. It also removed the unrelated pre-existing H .DS_Store. Do not repeat cleanup of unrelated files or edit parent-owned review records. The parent corrects the ledger now; child-authored review artifacts do not count toward the review cap.

S2's required current Docker/Centaurus validation now has recorded evidence; this does not retroactively authorize earlier local consumer runs.

## Spec

- **F01 P1, persists:** latest recorded C L1/disk and H ratios remain 1.096–1.390; timings also predate final source changes. A measurable regression is not merely an unspecified-policy question. Profile/fix and report final-build paired results.
- **F02 P1, persists in coverage:** H pinned source copies now match a8c6026, but final-source comparison evidence is incomplete. MNIST is wrongly called unavailable: its A/B files exist on Centaurus at the previously used canonical path. Query-only/concurrency/downstream and larger H coverage still require completion or explicit blockers.
- **F05 P1, persists:** the default top-k budget remains the base count instead of the paper's beta*n+k-1 form. Parent reproduced default k=101 returning only 100 results on a 200-point eligible fixture.
- **F06 P1, persists:** H still requeues unfinished tables onto the same LIFO stack, and a hard-coded 256-entry quantum replaces legacy per-leaf scan boundaries. Reported default k=100 Ratio worsens from 1.069267 to 1.070133; that accuracy loss was NOT approved. Default custom termination also observes stale projection-hit counts in its point-ID fast path.
- **F09 P1, persists:** Open accepts a root whose child order contradicts its key ranges, even with a valid sorted leaf chain. Parent reproduced acceptance of a deliberately swapped two-child root and changed query output. Validate routing order, not just reachability and separators in isolation.
- **F13 P2, new:** thread-local QueryScratch retains a strong index reference after all user index/engine owners are destroyed. Parent weak_ptr test shows the last potentially large memory/mapped index remains live until another query or thread exit. Make cache lifetime non-owning or explicitly engine/context-owned without breaking concurrency.

Round-1 F03, F04, F07, F08, F10, F11 and the clone/profile honesty aspects of F12 are addressed in the inspected changes. Continued final acceptance depends on the remaining findings and complete regression runs.

Standards: 2 current findings, S1 judgment call and S3 hard workflow correction. Spec: 6 open findings, highest P1. No acceptance pass.

## Parent-executed evidence

The parent compiled the current library and review-only `review-2/repro.cc` on Centaurus with GCC C++20, then ran it with a timeout. Output:

    default-k101: evaluated=100 neighbors=100 (base budget100 should allow200)
    stop-on-first-hit: evaluated=4 (expected1)
    index-released: 0 (expected1)
    misrouted-root: ACCEPTED
    misrouted-query: id=1 distance=1.5 (expected0,0.5)

The malformed-root diagnostic builds a valid 61-point/512-byte-page B+ tree, swaps the two root child pointers, updates its separator to match the new second child's first key, and leaves leaf chains untouched. Open checks each separator against that child's first key, but does not enforce ordering/range separation against the preceding child/subtree. Its validation therefore accepts contradictory routing. Fix with subtree min/max range and leaf-order correspondence validation as necessary, retaining checked arithmetic and safe decoding.

The parent also re-ran its round-1 reproducer against current code:

    equal-L2-bound: exact=1 distance=1.41421
    unary: opens
    stop-after-first: evaluated=1 (expected 1)
    invalid-threshold: rejected: default QALSH strategy has an invalid collision threshold
    quantum-one: neighbors=1

This supports closing the corresponding round-1 reproductions, not a blanket approval of all search paths.

### F05 details

QalshParameters candidate_budget defaults to 100 and DefaultQalshStrategy::start uses it verbatim regardless of k. Original QALSH's top-k default is a base budget plus k-1 (with checked arithmetic and n/exhaustion constraints), not an absolute 100 cap for arbitrary k. Explicit caller overrides may retain a separately documented absolute-budget meaning. Default behavior should not return a needless partial result for k>100.

### F06 details

- H still uses `table_pending_.back()/pop_back()` and appends an unfinished table with push_back(), immediately selecting the same table again. Pinned H uses FIFO round-robin incremental scans. This was already explicitly in round-1 F06 and remains unfixed.
- H index construction hard-codes scan_quantum=256; pinned H scans the remainder of the current leaf on each side before checking. Its legacy leaf capacity is (page_size-12)/8 (510 at 4096 bytes), whereas the new format uses a different header/capacity. Simply selecting another magic quantum is not a faithful check-boundary contract. Express the required logical scan grouping generically without an H-only mode or consumer copy of traversal.
- Immediate distance evaluation can preserve batch-boundary stopping, so the approved removal of sorting/batching does not itself excuse arbitrary scan scheduling changes or quality loss.
- SearchEngine's default point-ID path increments runtime.hits only after its whole batch, while EvaluateCandidate calls a user termination predicate inside the batch. A predicate checking projection_hits>=1 therefore observes zero until all four candidates are evaluated. This differs from the generic path's live statistics.

### F01/F02 evidence and environment

- Current acceptance manifest calls the H default k=100 Ratio loss “approved”; remove that characterization. The frozen accuracy gate expressly forbids using allowed evaluation-order changes as permission to regress quality.
- Parent verified the archived h-pinned ann_searcher and command SHA-256 values now equal the required a8c6026 values. Continue using those clean baselines.
- Parent SSH check confirmed `/home/jonathanhu237/datasets/qalsh4c/mnist/A.bin` (9,408,000 bytes) and `B.bin` (210,112,000 bytes) still exist. Do not treat absence in an implementation checkout as absence on the validation host. B→A truth remains a separate known gap; A→B can be tested now.
- Available Docker images include qalsh-cxx15-deps:latest and qalsh-cxx15:latest. Current reports document Docker GCC15 runs; use that setup for H.
- Candidate evaluation still refreshes/sorts neighbor snapshots after each evaluation even when the strategy only needs a count or kth distance; C still performs repeated full index validation during preflight/init, and allocates/reset per-point state for every query. These are profiling leads, not permission to remove safety checks or weaken candidate work.
- Keep final raw per-run logs, source/binary/data/projection hashes and commands. Final acceptance cannot use pre-final source timings, toy-only H, printed aggregate equality alone, or unresolved missing data as passes.

## Attempt counts

- S1, F01, F02, F05, F06, F09: one targeted fix attempt completed; next fresh Luna call is targeted attempt 2.
- F13: new finding, next call is its targeted attempt 1.
- S3: parent fixes the review bookkeeping now; next child must not recreate the false parent-review claims.
- Other round-1 findings: one targeted attempt completed, currently addressed; do not regress them.
