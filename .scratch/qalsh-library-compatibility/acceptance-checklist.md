# Parent acceptance checklist

Status: three-review loop complete; not accepted

Latest source/evidence: [parent-verification.md](parent-verification.md).
Historical review logs are preserved; their earlier pending states are not the
final evidence inventory.

| Obligation | Final evidence and state |
| --- | --- |
| Default persistent B+ | Implemented; public omitted-option lifecycle test passes. |
| Optional array file | Implemented as contiguous tables with distinct magic; public lifecycle/query/corruption tests pass. |
| Auto-detect | Both layouts reopen in separate processes; invalid/truncated formats rejected. |
| Common search | Standard/custom engine paths and dispatch traces covered across memory/B+/array. |
| H-specific code only | H uses shared TableScanSchedule; likelihood/deferred state stays H-owned. Parent reuse audit recorded. |
| Deferred work | Multi-page public tests produce deferred evaluation after radius advancement and exhausted scans. |
| Logical boundaries | Both persistent layouts share matching logical event traces; H legacy capacity translation retained. |
| Persistence integrity | Format identity, projections, no-replace/atomic publication and valid-header corruption cases covered. Historical seeded consumer projection audit retained with original provenance. |
| Real consumers | C/H production integrations and CLI layout options implemented; final C23/23 and H1/1 pass in Linux Docker. |
| Strict accuracy | **Not accepted.** Full MNIST L1/L2 ab/ba exact; Federalist distance rounding differs. H also exhibits equal-distance ordering and boundary membership differences. No numerical/tie exception approved. |
| Focused performance R1 | **Repaired for measured case.** Federalist L1 ab memory10pairs each: T1ratio0.96855, T4ratio0.97212; both95%intervals below1. |
| Full performance/coverage R2 | **Incomplete.**42fresh quality/resource cases do not replace the full repeated paired matrix. Missing layouts/controls/build measurements remain listed in parent-verification.md. |
| Resources | Native executable peakRSS and sampled anonymous/fileRSS for84fresh commands, separate open/query stdout; no cold-I/O or complete build-resource claim. |
| Existing contracts | Library4/4; source/installed package1/1 each; borrowed access, concurrency, bounded distance and errors covered. Malformed external hits now rejected even if the custom strategy declines evaluation. |
| Workflow | Local edits, one-way source sync, Centaurus/Linux Docker validation; no commits/pushes/merges. Three parent reviews plus direct takeover repair completed. |

No positive slowdown allowance or tie exemption was adopted. Improved focused
results do not certify all workloads. Exact discrepancy reports remain visible,
and the unresolved gates prevent unconditional acceptance.
