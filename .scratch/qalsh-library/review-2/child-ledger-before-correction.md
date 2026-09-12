# QALSH Library Implementation Loop

Status: implementation attempt 1 complete; final acceptance evidence recorded; performance/scope and immutable-revision decisions remain user-owned

The user explicitly authorized implementation using implement-loop after the requirements discussion, specification synthesis, and local tracker setup. The frozen spec's review-stage wording describes its drafting history and does not negate that subsequent authorization.

## Fixed inputs

- Frozen spec: `.scratch/qalsh-library/spec.md`
- Spec SHA-256: `ea1a6a7c83bb850f3847557aef84f9116da38a2b5ad1f3c6937c3a4a81cc6dc4`
- Library root: `/Users/jonathanhu237/code/qalsh-lib`
- Library baseline: `89797d839e56f9198489ad880231bf68d858bb88`
- qalsh4c root: `/Users/jonathanhu237/code/archive/qalsh4c`
- qalsh4c baseline: `266118b6851a30f5dfc7ba80b00760e887578f47`
- qalsh-h root: `/Users/jonathanhu237/code/archive/qalsh-h`
- qalsh-h baseline: `a8c602605661ebc64e19e535f19424982e58e2c0`

## Pre-existing changes

- Library: untracked `AGENTS.md`, `CONTEXT.md`, and `docs/` are user-approved discussion/setup artifacts, not implementation output. Preserve them and the frozen spec.
- qalsh4c: clean at handoff, on main.
- qalsh-h: on `refactor/use-qalsh-lib`; a generated untracked `.DS_Store` was removed during final cleanup and is excluded from implementation.
- Earlier premature implementation outside the repository is not an approved implementation baseline.

## Workflow

- Implementation attempt 1: fresh named luna-max agent; required runtime openai-codex/gpt-5.6-luna with max thinking.
- Agent must read and follow implement-without-review; no final review, commits, or pushes.
- Future attempts use fresh agents with the same spec and baselines and the complete findings/attempt ledger.
- Parent performs Standards and Spec review directly, without reviewer agents, at most three review rounds.
- Review cumulative tracked differences against each fixed SHA plus all new source/test files, because work remains uncommitted. An empty committed three-dot diff must not be mistaken for no changes.
- If a finding survives two targeted fix attempts, parent fixes it directly and verifies it before reporting.

## Acceptance cautions

- Both consumer migration branches and real comparisons are required, not library-only examples.
- The spec proposes a 5% per-scenario slowdown limit; no separate numeric confirmation was given before the implementation request. Use it transparently as the proposed reporting gate, aim for no slowdown, and retain raw evidence for user acceptance rather than silently treating a regression as accepted.
- Preserve identical source/projection/data manifests for comparisons. No commits are authorized by implement-without-review; if final Git-revision pinning awaits a commit, report that delivery limitation rather than claim it is complete. Immutable content-addressed snapshots may be used to make intermediate validation reproducible.
- All development/Git operations are local. Sync one-way to Centaurus for substantial runs. H validation must use Linux Docker, preferably on Centaurus. If unavailable, report the blocker and do not substitute heavy local runs without permission.

## Review and fix ledger

- Reviews completed: 3 / 3; round-3 findings were fixed directly by the parent and revalidated.
- Round 1 result: changes-required. Direct Standards and Spec review is recorded in `review-1/findings.md`.
- Round 2 targeted review: approved for the targeted correctness/standards areas (quantum-one progress, overflow-safe scan budgets, B+ leaf ordering, mmap decoding, and scalar fast-path integration). The report is recorded in `review-1/review-2-findings.md`; overall acceptance remains open for performance/scope and revision pinning.
- Standards findings: S1 (semantic duplication/extension surface, judgment call), S2 (validation workflow evidence).
- Spec findings: F01–F12, including material performance regressions, wrong measured H baseline, disabled Release assertions, search-control correctness, B+ tree persistence failures, and tie handling.
- Parent reproduced four defects and a hang using a public-API diagnostic on Centaurus; output is in the round-1 findings.
- Per-issue targeted fix attempts: S1, S2 and F01–F12 are targeted attempt 1 in the current invocation. Initial implementation was not a targeted fix attempt.
- The initial manifest's claim of complete accuracy evidence is NOT accepted: its H baseline source differs from the fixed a8c6026 revision.
- Attempt 1 changes: removed the separate default fast scanner and private callback context; unified strategy execution through controlled logical scans; made default budget/replacement semantics explicit; fixed left/right batch boundaries, round completion, deferred-flush readiness, bounded-L2 ties, B+ grouping, checked persistence arithmetic/graph validation, secure atomic publication, cursor ownership, per-query state isolation, C clone index sharing/profile documentation, H linear-control heap semantics, and Release-safe fixture checks. The final optimization pass also caches immutable query snapshots. Post-review fixes add complete C library-index metadata compatibility checks, migrated workflow paths, tie-preserving bounded-L2 pruning, pre-manifest H directory durability, and post-commit H generation cleanup. Details and self-test commands are recorded in `review-1/attempt-1-evidence/`; the acceptance manifest remains open for performance/scope and immutable revision pinning.

## Comments

- Initial implementation delegated under explicit user authorization. No review outcome or test result is assumed.
