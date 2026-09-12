# Direct cumulative review — round 1

Status: changes-required
Reviewer: parent assistant, directly (no reviewer subagents).

Frozen spec SHA-256: ea1a6a7c83bb850f3847557aef84f9116da38a2b5ad1f3c6937c3a4a81cc6dc4.
Baselines remain library 89797d839e56f9198489ad880231bf68d858bb88, C 266118b6851a30f5dfc7ba80b00760e887578f47, H a8c602605661ebc64e19e535f19424982e58e2c0. Reviewed cumulative working-tree changes and all new library/test sources, not only the empty committed three-dot diffs.

## Standards

- **S1 — possible Duplicated Code / Repeated Switches (judgment call):** `SearchEngine::search`, `FastEvaluate`, both generic/fast index scanners and `SearchFastPoint` duplicate semantics. Fast-default special cases and downcasts spill through the index API, including public `scan_default(void*)` whose context type is private. Prefer shared semantics with efficient specialization, not two independently maintained query algorithms. This is particularly relevant to the spec's small coherent extension boundary.
- **S2 — hard workflow breach in reported validation:** the manifest reports local C 23/23 tests, despite its AGENTS restriction on local non-MNIST dataset testing; H's prescribed Linux Docker execution is not evidenced. Future verification must use Centaurus and Linux Docker for H, with exact environment commands recorded. Historical runs cannot be undone or retroactively described as compliant.

## Spec

- **F01 P1 — performance gate fails:** reported C MNIST ratios 1.129/1.163 and H ratios 1.684–2.485 are material regressions. Profile and fix without weakening quality; rerun against the correct baselines.
- **F02 P1 — wrong H reference and incomplete evidence:** measured H source differs algorithmically from pinned a8c6026. Required real-data, query-only, concurrency and downstream coverage is incomplete; raw commands/source snapshots are not reproducibly archived.
- **F03 P1 — Release tests lose assertions:** both new test programs use `assert`, while Release defines NDEBUG. Passing 1/1 does not establish the asserted behaviors. Add always-active checks and the missing public contract cases.
- **F04 P1 — fast path bypasses configuration/safety:** ignores constructor threshold and max_steps; scan_quantum=1 never scans the left side and hangs. Reject invalid inputs and keep optimized/general semantics equivalent.
- **F05 P1 — default/replaceable termination incorrect:** default candidate budget is absent; a supplied termination predicate still has a separate budget forced ahead of it. Implement standard defaults and genuinely complete replacement.
- **F06 P1 — decision timing/order differs:** custom stop-after-first evaluates all four fixture points. Generic traversal changes side/table order; H delays checks until a window ends and loses deferred-flush readiness. Preserve explicit check boundaries and approved deviations only.
- **F07 P1 — valid B+ build cannot reopen:** 2401 points, one dimension/table, 512-byte pages produces a singleton internal node rejected by Open.
- **F08 P1 — equal L2 bound prunes a tie:** sqrt(2) squared rounds below 2; bounded evaluation rejects an equal-distance point, violating ID tie ordering.
- **F09 P1 — unsafe index/input validation:** unchecked layout arithmetic, graph cycles/links, and fast leaf accesses allow malformed indexes to hang or read invalid memory; derived integer conversions can overflow.
- **F10 P2 — non-atomic overwrite/no-clobber:** unlink-before-rename loses atomic replacement; access-then-rename can overwrite a concurrently created destination; PID-only temporary paths collide.
- **F11 P2 — unrelated H exact-search regression:** replaces the pinned O(k)-space heap with storing/sorting all n distances and changes k=0 handling, despite unrelated baseline rewrites being out of scope.
- **F12 P2 — C profiling/clone contract weakened:** block/cache knobs no longer affect behavior, corresponding counters remain zero, and disk Clone reopens/revalidates instead of sharing immutable index data.

Standards: 2 findings (S1 judgment call; S2 hard workflow breach). Spec: 12 findings; highest severity P1. Not accepted.

## Reproduction and evidence details

### Parent-executed public API reproducer

Local source: `review-1/repro.cc` (review-only diagnostic, not a production edit).
Synced library header/source and this file one-way into `/tmp/qalsh-parent-review1` on Centaurus, compiled with `g++ -std=c++20 -O1`, ran with a 5-second timeout. Output:

    equal-L2-bound: exact=0 distance=inf
    unary: QALSH internal page is invalid
    stop-after-first: evaluated=4 (expected 1)
    invalid-threshold: accepted; evaluated=1

Exit status 124: final valid scan_quantum=1/default-search case hangs. Specifically, fast scanners assign left_budget=max_entries/2=0, leave an active left cursor untouched, and the inner active-table loop requeues it forever. The existing max_steps safeguard applies only to the other loop.

F07: 512-byte leaf capacity is 60 entries; internal grouping capacity is 40. 2401 points produce 41 leaves, grouped into internal nodes with 40 and 1 children. The builder writes the latter while Open requires at least 2.

F08: L2Distance({1,1},{0,0}) is sqrt(2) rounded to float; BoundedDistanceValue with that bound returns exact=false/infinity for the identical vector pair. An equal-distance smaller ID therefore cannot replace an incumbent.

### F02 measured baseline mismatch

Expected a8c6026 source SHA-256 values were produced LOCALLY with git show and compared with the remote baseline files used in the manifest:

| file | expected a8c6026 | measured baseline directory |
| --- | --- | --- |
| H ann_searcher.cc | 241a1a8b0b0892a065b3e5df35038061fe500233a9766c81bc01b40693cf9053 | d9c6f15febed6f987a326f6787c4e143b80681fe495afa3ad826f8328a4f5840 |
| H command.cc | 96c803976bc9bbf28d33c800e1319a03e38515098ddd90caccbc66483a2de225 | 3924fbdca411a55ae91ec578fac45bcc42b5bbee92222ec2b89e7f5d3d16f241 |

The measured directory is `/home/jonathanhu237/code/qalsh-h-baseline-94be153`. Source copies in `review-1/h-expected-ann.cc` and `review-1/h-measured-ann.cc` show substantive differences: old vector/nth_element candidate management and deferred radius arithmetic instead of pinned heap/squared-radius behavior. This invalidates claims of accuracy matching the required H revision, not merely naming in the manifest. Rebuild clean archives from the pinned revisions locally, sync one-way, and hash all measured source/executables/projections. Check C similarly. The report's huge C disk size comparison also needs verification: its alleged MNIST B flat-table payload is smaller than even one table for 67000 points, suggesting wrong data/file accounting.

### F04/F05/F06 source specifics

- Fast path skips `strategy.start`, takes threshold from metadata rather than the passed DefaultQalshStrategy, ignores `max_steps`, and always checks at radius boundaries even when that timing is disabled.
- `can_use_fast_path()` and public `scan_default(void*)` make optimization alter semantics rather than only dispatch cost. Parameter/control updates must not switch traversal ordering invisibly.
- Default candidate_budget_ initializes to nullopt. Thus the advertised default QALSH actually behaves like C's budget-free variation. C should explicitly customize a genuine default, not define it accidentally.
- `should_terminate` tests candidate_budget before the user predicate, so the user cannot fully replace termination. Choose a documented default/composition model satisfying complete replacement, rather than silently imposing both.
- Generic evaluation does not invoke the default termination predicate after each candidate; it waits until next() after a whole scan. `runtime.current_round_complete` is assigned all-table exhaustion rather than current-window completion.
- Default and H table scheduling push an unfinished table onto the same stack and immediately rescan it, unlike the pinned round-robin incremental traversal. Generic scanners alternate left/right entries instead of the pinned left segment then right segment. H's boundary callback returns early on an unfinished window and therefore suppresses the required per-incremental-scan stop check.
- H `next()` calls FlushDeferred each time; after it submits one pending Evaluate, the next FlushDeferred with no new entries resets deferred_flush_ready_ before the pending sequence is finished. It also runs at ordinary scan boundaries rather than only the required radius event. Add fixed-projection traces against pinned H and document only the user-approved immediate-evaluation/tie deviations.

### F09/F10 persistence and input details

- Header offset/size multiplications and additions are unchecked before allocation/mapping. ValidateConfig does not validate metric when explicit projections take the early return; MakeProjectionVectors loops on vector.capacity rather than the required count.
- Open checks local page headers only, not internal child ownership/cycles, link reciprocity/type, valid nonempty leaf contents/IDs/sorted keys, or root reachability. reset_cursor can loop through a self-referential internal node; fast scan trusts linked page counts/types and performs unchecked mapped entry access.
- Public cursor checks test only concrete cursor type, not the originating index. A cursor with a table ID from another same-backend index can index outside tables_. Either keep such operations internal or validate/document ownership safely.
- DeriveQalshParameters converts ceil(float) to uint32_t without representability validation; near-one c can make the table count unrepresentable.
- Atomic publication: unique securely created temporary files, checked close/fsync and atomic replace or no-replace behavior are needed. Do not unlink an existing valid index before a failing publication step.

### F12 C migration details

Disk Clone calls Init, opening the full index again; Init itself opens it once through ValidateIndex and again for use. Existing immutable index sharing is lost. Profiling output still advertises table block/cache counters that are never incremented and accepts cache/block settings that have no effect. Preserve meaningful logical telemetry/settings where compatible, or explicitly surface unsupported/changed semantics rather than printing misleading zeros. Do not mislabel mmap page faults as explicit page reads.

## Fix-attempt ledger

Initial implementation is not a targeted fix attempt. S1, S2 and F01–F12 currently have 0 targeted fix attempts. The next fresh Luna invocation is targeted attempt 1 for these findings. Keep this review and the frozen spec unchanged across future rounds.
