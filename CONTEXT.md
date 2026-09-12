# QALSH Library

Vocabulary for reusable QALSH indexing and search.

## Language

**Standard QALSH**:
The default search algorithm whose candidate selection uses a collision threshold across projections. It includes search progression and termination, not only the candidate rule.

**QALSH-H**:
A QALSH variant that uses likelihood-based candidate selection and may reconsider deferred points as the search radius grows. Its algorithm-specific decisions are distinct from the shared projection indexing and search mechanics.

**Original point**:
A dataset vector identified by a point ID. Its coordinates are distinct from its scalar projections.

**Projection index**:
The collection of projection vectors and ordered projected values associated with point IDs. It is distinct from the original point dataset.

**Point access**:
Retrieval of an original point's coordinates by its point ID.

**Projection hit**:
An occurrence of an original point within a query's current window on one projection. Hits from different projections may contribute evidence about the same point.

**Candidate**:
A point selected by the candidate rule for distance evaluation against the query. A projection hit alone does not make a point a candidate.

**Search radius**:
The scale of the current search round. It determines projection-window bounds and may participate in a strategy's distance-based termination rule; it is not itself a measured point distance.

**Candidate budget**:
A threshold on candidate distance evaluations used by a termination rule. Whether it is a strict cap or may be exceeded depends on the strategy's check timing.

**Search strategy**:
The algorithm-specific decisions and accumulated evidence governing candidate selection, search progression, and termination. A candidate rule or termination rule describes only one part of a search strategy.

**Scan exhaustion**:
The absence of further projection entries to visit. It does not imply that a search strategy has no remaining candidate work.

**Termination rule**:
The condition deciding whether a search should stop. It is distinct from the stage at which that condition is checked.
