# Query-performance attempt 2 — candidate screen

Date: 2026-09-13. Fixed baseline: `722ecfbf8b426a3148d30f9cd6b393018bf31fab`.
No candidate from this screen was retained. All experiments used temporary
copies under `/tmp/qalsh-attempt2-candidates` on Centaurus; no consumer source,
frozen evidence, or production file was changed.

## Built-in early-stop candidate

The profile-supported `DefaultQalshStrategy` range path was changed temporarily
to stop ID decoding after an evaluation requested finish. The complete logical
range was still counted. Candidate header SHA-256:
`3e5959aa326e01662a75990c6812d4052de94a3afa481b31879cadaf50f19be6`.
The baseline source was otherwise unchanged. The fixed one-dimensional
`bench_default` fixture used `n=20000`, 64 tables, 100 repetitions, and a
large range; both variants returned checksum `107335500`.

Centaurus command (GNU optimized build, pinned to CPU 20):

```text
perf stat -e cycles,instructions,branches,branch-misses -r 5 \
  bench_default 20000 64 100
```

Stable instruction counts were 1,660,458,858 for baseline and 1,718,852,353
for the candidate (+3.52%). Branch misses were 561,496 and 577,069. Reported
elapsed time was 72.27 ms for baseline and 168.11 ms for the candidate, but
the latter had high frequency variance; neither the timing nor the counters
showed a defensible gain. The candidate was rejected before real-data quality
or matrix execution.

## In-memory lower-bound candidate

The profile-supported `InMemoryIndex::reset_cursor` lower-bound branch was
replaced temporarily with equivalent conditional-index arithmetic. Candidate
source SHA-256:
`0a5bfca5d9c77b248ce6c6c165463518064247f526823f2de837217bb2d7f943`.
The reset-only fixture used `n=67000`, 64 tables, and 100,000 repetitions;
its `FinishImmediately` strategy still exercises all cursor resets and both
variants returned checksum `0`.

The same pinned Centaurus `perf stat -r 5` command measured:

| counter | baseline | candidate |
|---|---:|---:|
| cycles | 2,107,834,742 (6.28%) | 2,404,175,815 (2.44%) |
| instructions | 2,782,503,815 | 2,866,239,374 |
| branches | 792,496,091 | 792,549,090 |
| branch misses | 2,885,169 | 2,883,406 |
| elapsed seconds | 0.46134 (6.27%) | 0.52617 (2.42%) |

The candidate increased instructions by 3.01% and cycles by 14.06%; the
compiler retained a conditional branch, so this did not address the measured
branch behavior. A forced mask/branchless variant was worse still (3.944B
instructions and 5.420B cycles versus 2.783B and 2.108B). It was also rejected
without full-matrix execution.

## Decision

The scout's range-traversal outlining idea was not promoted to an implementation:
there was no evidence-backed benefit after the two focused profile candidates
were rejected, and an extra per-range callback would require another screen.
The required R1 comparison against both original consumers and baseline
therefore remains unmet. R2's missing final-bound dual-baseline coverage also
remains unresolved. The production source stays byte-identical to the fixed
baseline; see `implementation.md` and `final-source-fingerprint.json`.
