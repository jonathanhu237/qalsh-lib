# R2 coverage and provenance report

Date: 2026-09-13. Baseline: `722ecfbf8b426a3148d30f9cd6b393018bf31fab`.
Final source: `final-source-fingerprint.json`.

## Acceptance interpretation

R2 is **unresolved**. A historical artifact is not counted as final coverage
unless its result directory binds the final source and binary, dataset/query/
truth, harness, and all effective parameters, and independently checks the
result against both the frozen original consumer and `722ecfb`. The final
production source is now byte-identical to `722ecfb`; the prior optimized
source was withdrawn. Therefore the old optimized-current artifacts cannot be
relabelled as final-current evidence.

Legend: `H-partial` means a historical artifact exists but has a stated gap;
`—` means no qualifying artifact; `NFB` means the artifact is not final-bound.
The “original” and “722ecfb” columns are quality comparisons, not claims that
the final source passed them.

## Scenario × baseline quality coverage

| family / requested scenarios | original quality | 722ecfb quality | independent reference | final-bound status |
|---|---|---|---|---|
| C Federalist: L1/L2 × AB/BA × T1/T4 × memory/B+/array | H-partial: 4 T1-memory rows (L1/L2 × AB/BA) | H-partial: same 4 rows | H-partial: straight-line double comparator on those 4 rows | NFB; T4, disk, array quality absent |
| C MNIST: L1/L2 × AB/BA × T1/T4 × memory/B+/array | H-partial: strict output rows only; no qualifying exhaustive dual-baseline quality | H-partial: strict output rows only | H-partial: returned-distance checks only (8 memory rows), not top-k dual quality | NFB; disk/array and dual independent quality absent |
| H toy: k=1/100 × complete=0/1 × B+/array | H-partial: B+ truth checks for 8 rows; array original quality absent | H-partial: strict HEAD checks for 8 rows | H-partial: B+ only | NFB; array independent dual quality absent |
| H toy overrides (m/beta) | H-partial: 2 B+ rows | H-partial: strict HEAD rows | H-partial: B+ truth checks | NFB; no final-bound timing/resources |
| H toy exact linear-scan control | H-partial: strict output only | H-partial: strict output only | — | NFB |
| H GIST: k=1/100 × complete=0/1 × B+/array | H-partial: B+ truth checks for 4 rows; array check is absent/invalid | H-partial: strict HEAD checks | H-partial: B+ only | NFB; array truth artifact compares the same current path to itself |

The detailed machine-readable inventory is `r2-coverage.json`.

## Timing coverage

The required protocol is at least ten alternating pairs per frozen scenario,
with both original and `722ecfb` comparisons where applicable. The following
are the only primary HEAD/current observations cited by review-1:

| scenario | historical timing artifact | pairs | status |
|---|---|---:|---|
| C Federalist L1 AB T1 memory | `evidence/paired-final-head-current-fed` | 10 | NFB; no original/current pair and no complete matrix |
| C MNIST L2 AB T1 memory | `evidence/paired-final-head-current-mnist` | 10 | NFB; no original/current pair and no complete matrix |
| H toy k=100 complete=1 B+ | `evidence/paired-h-toy-head-current` | 10 | NFB; k=1/complete=0 absent; no original/current pair |
| H toy k=100 complete=1 array | `evidence/paired-h-toy-array-head-current` | 10 | NFB; other k/modes absent; no original/current pair |
| H GIST first 100, k=100 complete=1 B+ | `evidence/paired-h-gist-head-current` | 10 | NFB; array/k=1/complete=0 absent; no original/current pair |

The older `paired-final-*` and layout-control directories do not repair this:
several lack complete plans/provenance, and B+ versus array is a layout
control rather than an implementation-versus-both-baselines comparison. No
complete frozen timing matrix or final-bound timing scenario exists.

For context only, the five historical HEAD/current ratios were 1.01238
(Federalist), 1.05816 (MNIST), 1.04649 (toy B+), 1.04247 (toy array), and
1.00973 (GIST B+); the first four are slowdowns and GIST is inconclusive.
They are not final-source measurements.

## Resource coverage

The existing resource artifacts record useful wall/open/query/RSS details,
but only for the following historical HEAD/current controls:

| family | historical resource rows | missing |
|---|---|---|
| C Federalist | L1 AB T1 memory/B+/array | original baseline, L2, BA, T4 |
| C MNIST | L2 AB/BA T1 memory/B+/array | original baseline, L1, T4 |
| H toy | k=100 complete=1 B+/array | original baseline, k=1, complete=0 |
| H GIST | k=100 complete=1 B+/array | original baseline, k=1, complete=0 |

No resource row is final-bound because the source/binary/data/parameter
fingerprints are incomplete or point at the withdrawn optimized build.

## Provenance audit and blockers

* `evidence/paired-*/*/provenance.json` records the withdrawn optimized
  library hashes (`bfd733a8...` and `c2052a21...`) rather than the final
  baseline hashes. Its fingerprint list does not include every dataset,
  query, truth, or effective-parameter hash required by the spec.
* Most `final-rerun-*` and `final-resources-*` directories have no
  per-directory `provenance.json`; their logs/TSVs alone cannot establish the
  final source/binary binding.
* Several strict-comparison summaries contain only IDs/counts or formatted
  float output. Strict equality is not independent tie-aware quality, and
  historical float-format differences remain unchanged historical evidence.
* `final-rerun-h-gist-array2` truth JSONs list the same current TSV as both
  runs. Their zero differences are a self-comparison, not baseline evidence.
* The final source fingerprint is now baseline-bound; the stale optimized
  fingerprint was preserved as
  `failed-optimization-source-fingerprint.json`. No old result is claimed as
  final after this rebinding.
* The initial Centaurus check found a long-running prior `/tmp/old_algo`
  process (PID 416377). It was not killed or restarted. Since production was
  rolled back to the fixed baseline, no additional resource-intensive full
  matrix was started merely to produce a non-discriminating baseline-versus-
  itself run.

## R2 disposition

R2 remains **unresolved**, with explicit gaps rather than an acceptance claim.
The final-bound counts are zero for real-data quality baseline pairs, timing
scenario pairs, and resource scenario rows. The only final-bound validation
added in this attempt is the focused R3 comparator self-test; it does not
substitute for the missing real-data R2 matrix.
