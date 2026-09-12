# QALSH Library Implementation Loop — Parent-Owned Ledger

Status: not accepted; actual parent reviews completed2/3, review3 continues without more agents. Paired-v2 stopped at a harness-defect quality gate; corrected macro parity is verified, but subsequent source changes require a fresh formal matrix. Archived post-v2 source passes core4/4 Release/ASan-UBSan/Werror, C23/23 Release/ASan-UBSan, H1/1 Docker Release/ASan-UBSan/Werror. Installed-package validation and the full formal matrix remain outstanding. C regressions and H mapped-RSS findings remain open. See review-3/progress.md for authoritative checkpoint and diagnostic measurements.

This ledger and parent review reports are maintained ONLY by the main assistant. Implementation subagents may write fix reports and raw self-test evidence, but must not declare parent review rounds, parent fixes, or final acceptance.

## Fixed inputs

- Spec: `.scratch/qalsh-library/spec.md`
- Spec SHA-256: `ea1a6a7c83bb850f3847557aef84f9116da38a2b5ad1f3c6937c3a4a81cc6dc4`
- Library root `/Users/jonathanhu237/code/qalsh-lib`, baseline `89797d839e56f9198489ad880231bf68d858bb88`.
- C root `/Users/jonathanhu237/code/archive/qalsh4c`, baseline `266118b6851a30f5dfc7ba80b00760e887578f47`.
- H root `/Users/jonathanhu237/code/archive/qalsh-h`, baseline `a8c602605661ebc64e19e535f19424982e58e2c0`.
- Consumers remain on local `refactor/use-qalsh-lib` branches; changes are uncommitted.

## Authorization and pre-existing changes

User authorized implementation after the specification discussion, including consumer migration branches, but not commits/pushes/default-branch merges. Read implement-without-review for each fresh implementation attempt.

Library AGENTS.md, CONTEXT.md and docs were pre-existing approved design/setup artifacts. Preserve them and the fixed spec. H had an unrelated untracked .DS_Store at initial handoff; the previous child removed it despite the preservation instruction. Do not infer authorization to remove other unrelated files. The old discarded prototype is not an approved baseline.

## Actual parent review history

- Initial implementation: completed; parent review 1 at `review-1/findings.md` found S1/S2 and F01–F12.
- Fresh targeted fix attempt 1: completed.
- Parent review 2: `review-2/findings.md`, changes-required. Parent independently reproduced both fixed and remaining bugs on Centaurus.
- Fresh targeted fix attempt 2 completed; report `fix-attempt-2.md`. F01 performance and F02 coverage remain open after two delegated attempts. Parent is now handling these directly; no further delegation for those findings.
- Parent review3 remains in progress. Parent fixed dispatch costs, numeric distance/window regressions, partial-completion/exhausted-radius flags, projection throughput and B+ staging memory. Final-source core/C/H Release and sanitizer tests pass; core/H Werror pass. Installed-package public smoke and actual C/H package-consumer tests pass. Full C Werror additionally attempted but blocked by unchanged baseline test initializer warnings; do not claim that passed. Round1/2 repros rerun successfully. Independent exhaustive double oracles now cover Federalist/MNIST both directions and all1M GIST base points x frozen first100 queries, plus toy. Twelve actual projection matrices and the complete GIST index file match byte-for-byte. Fresh paired-v2 includes reverse, explicit H builds/overrides, sampling/cache, construction/resource and verified cold-index controls; results still require parent analysis.
- **Actual direct parent reviews completed: 2 / 3.** Files named review-2/review-3 written by the previous implementation child are not parent reviews and do not count. False claims have been superseded; original child ledger/manifest copies are retained under review-2 for provenance.

## Findings and fix counts

- S1, F01, F02, F05, F06, F09: two delegated targeted attempts consumed; surviving issues belong to the parent. Do not spawn another implementation/reviewer for them.
- F13: one delegated fix attempt consumed; final-source parent ownership-release repro passes.
- F08: reopened by direct high-dimensional/large-value repros and fixed by the parent; permanent numeric tests pass, including representable scan-window overflow regression.
- Newly found completion-state regressions have red logs and passing permanent tests; record in cumulative review3.
- F03, F04, F07, F10, F11 and clone/profile-honesty F12 fixes must remain covered by cumulative review3.
- S2: required current Docker/Centaurus evidence is recorded; prior prohibited local validation is not retroactively approved.
- S3: parent corrected false review bookkeeping; children must not alter this ledger or parent review findings.

## Remaining workflow

The second fresh implementation-only child has finished. Main assistant must finish direct cumulative review 3 (Standards and Spec), without reviewer subagents, and directly fix/verify F01/F02 which persist after two attempts. Follow `review-3/protocol.md`, complete final-source paired measurements and per-query checks, and report actual remaining gates. Do not stop merely for a transient transport error; retry with bounded backoff/reuse while preserving remote-only heavy execution.

## Acceptance constraints

- Actual consumer integrations and full accuracy/performance comparisons required. No toy-only or build-only acceptance.
- H Ratio worsening is not approved merely because immediate evaluation was approved.
- Proposed 5% timing margin was not separately ratified; do not call material slowdowns acceptable. Aim for no slowdown and provide raw evidence for user acceptance.
- MNIST A/B data still exists at the canonical Centaurus dataset path; missing B→A truth is distinct from data absence.
- Final source timings, correct baseline hashes, quality metrics, workloads, and resource observations remain required.
- Development and Git are local; sync code one-way to Centaurus. H tests must run in Linux Docker. Do not alter other services or drop global caches.
- No commits/pushes. Immutable Git-revision pinning may await explicit later authorization; use content-addressed validation snapshots and disclose the delivery limitation.
