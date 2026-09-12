# Reuse audit (review-1 source plus ongoing fixes)

The objective is removal of major repeated responsibilities, not minimizing the combined line count of a safer library plus two consumers.

| Responsibility | Current owner | Consumer role |
| --- | --- | --- |
| Projection generation and sorted projection storage | qalsh-lib | C/H supply their effective parameters and compatible seeded projections |
| Persistent construction, format validation and outward cursor traversal | qalsh-lib | Choose default B+ or array via options; reopen via factory |
| Generic table queue, logical completion, exhaustion and radius requeue | TableScanSchedule | H calls scheduler and owns algorithm-specific timing decisions |
| Candidate distance computation, bounded pruning, deduplication and ordered top-k | SearchEngine | Provide point accessor and request candidate evaluations |
| Standard collision threshold and standard termination | DefaultQalshStrategy | C disables the base candidate budget for its existing incumbent workflow |
| Likelihood evidence, deferred candidate eligibility, complete-radius mode | H strategy | Remains H-specific |
| Dataset files, estimator/sampling and command workflows | C/H | Remains application-specific |

H no longer contains its old b_plus_tree.cc/.h and buffer.cc/.h (601 raw lines at the frozen original baseline). Its strategy does not read tree nodes or mutate cursors. C's ann_searcher.cc decreased from 1032 to 490 raw lines relative to its frozen original. These are illustrative file counts, not a claim that every removed line was duplicated or that the new implementation is already accepted.

Across all .cc/.h/.cu/.cpp files under src, raw line counts at this checkpoint are C 6893→6031 and H 1767→1627. H's smaller net reduction reflects new persistence-generation consistency, validation and mapped-point support alongside removal of old tree code. Report responsibility movement and actual integration paths rather than presenting these aggregate counts as a quality/performance gate.
