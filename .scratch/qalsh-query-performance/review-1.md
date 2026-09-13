# Review 1 — cumulative changes against 722ecfb

Status: changes requested

Fixed baseline: 722ecfbf8b426a3148d30f9cd6b393018bf31fab
Fixed spec: .scratch/qalsh-query-performance/spec.md
Review surface: git diff 722ecfb -- (tracked working changes), new experiment tools/spec/report and sampled evidence. Review performed directly by parent, not delegated. No new benchmark executions during review.

## Standards

No concrete documented coding-standard breach identified in the small library diff. The extra projection-only cache duplicates immutable values intentionally for locality, not automatically a duplication smell. Its resource/performance justification belongs to the Spec gate below. git diff --check passes. Current hashes match the reported implementation except the Markdown test-file hash omits its final `d` (actual fa63d2b10b0d5d37fd6ecb57a5e92689b2f2fa47c36aac39d84eb0545d28635d); correct the report when updating provenance.

## Spec

### R1 — blocking: requested overall speed improvement not established; retained changes regress

Spec objective is faster queries, user requires overall faster than both original C/H and 722ecfb. implementation.md's five HEAD/current primary cases show ratios 1.01238, 1.05816, 1.04649, 1.04247 and 1.00973; none demonstrates improvement, four have slowdown intervals. Inspected paired-final-head-current-fed/summary.json confirms ten pairs, ratio 1.0123813 and CI [1.0080528,1.0168517]. No predeclared complete-matrix geometric aggregation against both baselines is supplied. Layout comparisons between current B+/array do not establish implementation improvement. Revise or remove detrimental changes based on controlled component measurements; find an actual semantics-preserving improvement rather than leave an acknowledged slower optimization as the final result. If no defensible path remains, report it as blocked/unmet instead of claiming success. Local regression alone is not a failure if the agreed fixed-matrix overall dual-baseline improvement is demonstrated.

### R2 — high: quality/coverage acceptance claims exceed evidence

Spec requires every scenario compared against BOTH frozen original C/H and 722ecfb using independent reference, plus frozen timing matrix. The report calls quality and compatibility gates passed and implementation fully self-tested, but enumerates only four Federalist HEAD/current independent comparisons under independent-final. MNIST returned-distance validation alone does not prove recall/approximation quality against both baselines; H truth checks alone likewise do not prove relative non-regression. Historical original outputs may be reused only when their provenance, parameters, populations and reference comparisons actually cover the final matrix. Provide an explicit scenario x baseline x quality/timing/resources evidence map with final fingerprints; inspect actual validator fields, not file names/summary pass flags. Mark omitted cases and unsupported baseline controls explicitly, and avoid broad passing claims until covered. Complete the predeclared paired timings/overall comparison or report concrete blockers, without post-hoc selecting favorable scenarios.

### R3 — high: quality comparator contradicts equivalent-tie policy

exhaustive_quality.cc NoWorse around lines139-148 rejects current.ordinary_hits < baseline.ordinary_hits in addition to tie-aware hits. User and spec explicitly permit equivalent cutoff ties and require ordinary ID overlap to be reported separately. A valid alternative tied neighbor can thus be rejected even when tie-aware recall and distance quality are unchanged. Remove ordinary overlap as an acceptance predicate, retain it as a diagnostic, and add small regression tests: equal-distance cutoff substitution passes; genuinely worse non-tied answer fails; malformed counts/duplicates and incorrect/non-finite distances fail as appropriate. Validate tolerance and partial-result handling at these seams rather than silently adjust the frozen policy.

## Counts and next round

Standards: 0 blocking findings, 1 provenance typo note.
Spec: 3 findings, worst R1 blocking.
R1/R2/R3 fix attempts completed: 0 each. Next fresh implementation is fix attempt 1 for each. Review count: 1 of maximum 3. Keep original spec/baseline fixed; no commits/push.
