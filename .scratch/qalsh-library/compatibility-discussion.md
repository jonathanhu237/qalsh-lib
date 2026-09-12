# QALSH library compatibility discussion

Status: discussion complete for specification synthesis; implementation not authorized.

Published specification: [QALSH library compatibility](../qalsh-library-compatibility/spec.md), status `ready-for-agent`. The user confirmed the public API and real consumer workflows as the test seams. Numerical accuracy details remain explicitly deferred in that specification.

## Confirmed requirements (2026-09-12)

- The primary reuse objective is eliminating substantial duplicated QALSH code.
  The library must provide complete standard QALSH behavior, including
  collision-threshold candidate selection; supplying only low-level primitives
  is insufficient.
- QALSH-H must reuse the shared infrastructure and customize its likelihood-based
  candidate search through a small, stable extension contract. Requiring many
  exposed internal interfaces is unacceptable. The user has not approved
  separate consumer execution loops as the solution.
- The user requires exactly matching accuracy under the same random seed.
  The observable definition of accuracy (aggregate metrics, per-query results,
  IDs/ties, or bitwise distances) remains to be clarified. The user explicitly
  deferred this question (Q5); settle the architecture discussion first.
- The performance target is no regression. The user recognizes this may be
  difficult, but has not approved any slowdown allowance, including 5%.
- Prefer retaining the planned B+ tree migration for C disk queries. The user
  recalls that the original QALSH used B+ trees and that switching to sorted
  arrays did not produce a large speedup. Investigate feasibility first; adding
  a persistent flat-array backend is not the selected direction. The recalled
  speed comparison has not been independently verified.
- Subsequent proposal: the user reopened the storage choice and suggested
  offering both B+ tree and persistent sorted-array indexes. This supersedes
  treating B+ tree as the only preferred disk option; inclusion and default
  backend selection remain under discussion, not implementation authorization.
- Resolved Q8: provide both persistent backends, defaulting to B+ trees for
  callers rather than choosing defaults by consumer. A construction option
  selects a sorted-array file. See `docs/adr/0001-selectable-persistent-index-layout.md`.
- Actual use opens an index once and then performs many queries. Steady-state
  query performance is therefore the primary timing concern; open time and
  memory remain separately reported costs, not implicitly waived requirements.

## Evidence and design implications

- `review-3/progress.md` is a later checkpoint than historical acceptance tables.
  Its diagnostic measurements are not final acceptance of current source.
- Matching a seed alone does not establish matching projection vectors: existing
  consumer generation pipelines differ; see `docs/spec.md` baseline protocol.
- Existing scope allows some H evaluation-order/tie changes and changes C disk
  storage from flat arrays to B+ trees. These choices must be reconciled with
  the newly stated exact-accuracy requirement before being treated as settled.
- No architecture change or relaxation of acceptance gates has been approved.
- Proposed direction, not yet a selected implementation: retain a complete
  default QALSH and provide reusable orchestration for stateful extensions.
  A per-hit boolean candidate rule alone cannot express H's deferred candidates
  becoming eligible after radius growth without a new projection hit.
- Original author source confirms a B+ tree per projection table, built from
  sorted projections, with initial tree descent and resumable leaf traversal:
  https://github.com/HuangQiang/QALSH/blob/master/methods/qalsh.h
  Its leaf representation stores IDs with sampled projection keys, rather than
  precisely matching this library's per-entry projection-value/ID layout:
  https://github.com/HuangQiang/QALSH/blob/master/methods/b_node.cc
  Consequently, B+ tree provenance supports feasibility, not identical search
  behavior or a demonstrated no-regression result.
- Current `src/qalsh.cc` already uses mmap-backed page views, descends at cursor
  reset, and resumes scans across linked leaves. It does not perform a fresh
  root descent for every hit or radius expansion. Performance investigation
  should separate query setup, scan/strategy work, page crossings, and full
  validation during index opening.

## Open frontier

- Inspect actual H customization and distinguish necessary algorithm-specific
  state/progression from duplicated generic orchestration. Evaluate extension
  usability by the code H must write, not merely public method count.
- Deferred: define exact accuracy with a concrete per-query and equal-distance
  example when the user is ready to revisit Q5.
- Preserve complete standard QALSH and compact H customization across both
  selected persistent backends; adding a backend does not itself solve generic
  engine overhead. Remaining discussion includes extension behavior and the
  deferred exact-accuracy definition.
