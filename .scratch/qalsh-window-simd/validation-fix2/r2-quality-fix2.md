# R2 quality-validator fix attempt 2

Status: implementation-only evidence. This is the second and final delegated R2
fix attempt; no production SIMD code, timing result, CPU setting, spec, or
baseline was changed. No commit/push was performed.

## Mechanism and numerical basis

`stage_b_quality_compare.cc` no longer uses absolute `1e-9`, `1e-4`, or ratio
constants. The independent L1 reference promotes the finite binary32 inputs to
`long double` and uses a Neumaier compensated sum. It reports an error bound
from the standard

```
u = numeric_limits<T>::epsilon()/2
gamma_n = (n*u)/(1-n*u)
```

forward-error form. The independent accumulator's `n` is the exact count of
its four per-coordinate arithmetic operations plus the final correction add
(`4*dimensions+1`). The bound for a possible historical float32 distance path
uses the three per-coordinate operations plus the final add (`3*dimensions+1`).
Both bounds scale with the actual sum of absolute coordinate differences and
are rejected as unusable if they are not finite; there is no arbitrary absolute
floor.

A mathematically zero best distance is exact only when a returned distance is
also zero. For nonzero distances, an exact-hit flag is used only when the two
independently computed uncertainty intervals can overlap. Candidate-vs-baseline
selected distance uses disjoint interval ordering. Approximation ratio is
compared **unconditionally**, including when both rows carry an uncertainty
based exact flag: a candidate ratio is a failure when its lower ratio bound is
strictly above the baseline upper ratio bound. Thus a nonzero answer can never
be hidden behind a zero-distance tie or a `both-exact` shortcut.

Reported float32 distances are checked against the rounding cell of the actual
reported float (midpoints with adjacent finite floats), intersected with the
independent long-double error plus the derived float32 arithmetic bound. This
allows the existing original-C accumulation/encoding error while rejecting an
arbitrary `9e-5` encoding for an exact zero. Finite IDs, complete unique query
rows, finite input data, and same-file/canonical-alias rejection remain fail
closed.

## Focused fixtures

The rerunnable sources are retained in:

- `.scratch/qalsh-window-simd/test_stage_b_quality_compare.py`
- `.scratch/qalsh-window-simd/parent_quality_edge_repro.py`

The expanded fixture run is in `quality-fixtures-r2fix2.log` and covers exact
and nonzero ties, zero-to-nonzero regression, adjacent normal float32 values,
subnormals, finite extreme coordinates, a known float32 accumulation discrepancy,
both-nonexact ratio regression, recall loss, malformed/missing/duplicate/
out-of-range/non-finite rows and data, wrong reported distances, and same-file
and symlink-alias rejection. Both parent-review negative fixtures now return
exit **5** (`quality_failures=1`):

- `zero_to_nonzero`: candidate exact `0`, ratio `inf`, failures include
  `recall_loss`, `selected_distance_regression`, and
  `approximation_regression`.
- `wrong_zero_distance_encoding`: candidate actual distance `0`, reported
  `9.000000136438757e-05`, failure `candidate_reported_distance_mismatch`.

Remote g++-15 Werror compilation and fixture execution are recorded in
`validation-commands.log`; the remote checker SHA-256 is
`b54e341a259557bdf9d13b498565e6b82138e3f1e26fe6189e99e52558f8abfa`. The
updated provenance map is `source-fingerprint.json`.

## Existing Federalist quality evidence (not a performance run)

The checker was synchronized one-way to the isolated Centaurus directory
`/tmp/qalsh-window-simd-r2fix2` and run against existing, separately produced
triplet-01-ABC result files. Baseline and candidate paths were distinct (the
same-file guard was not bypassed). Both comparisons returned exit **0** with
per-query statuses for all 1,880 rows:

```
original C vs candidate:  original_hits=1025 candidate_hits=1025 both_hits=1025
                         original_only=0 candidate_only=0 quality_failures=0
                         max_original_reported_error=7.62939453125e-06
                         max_candidate_reported_error=0
722ecfb vs candidate:     original_hits=1025 candidate_hits=1025 both_hits=1025
                         original_only=0 candidate_only=0 quality_failures=0
                         max_original_reported_error=0
                         max_candidate_reported_error=0
```

The existing result SHA-256 values remain audited original C
`dfeb3c2c217c9974f0624fede9f762624cefb6822b28280a3d750adc939302a4` and both
unmodified 722ecfb/candidate `48695f1a09f283e9ec8991cc4d04e94f0e8735a14cccc6b0df90ef8c780c370e`.
Full per-query logs are `fed-original-vs-candidate.log` and
`fed-722ecfb-vs-candidate.log`.

## Verification and remaining status

- Local Clang C++20 `-Wall -Wextra -Wpedantic -Werror` compile: pass.
- Local expanded fixture run: pass.
- Local ASan/UBSan parent negative fixtures (`detect_leaks=0`): both exit 5 as expected.
- Centaurus g++-15 `-Wall -Wextra -Wpedantic -Werror` compile and expanded
  fixtures: pass.
- Centaurus existing Federalist checker comparisons: both exit 0 with 1,880/1,880
  per-query quality statuses passing.
- Fixed spec SHA-256 remains
  `fa8a00a2104d341054531f018792e925183d346b2b3f80caabab4889428ee5f2` and
  comparison baseline remains `722ecfbf8b426a3148d30f9cd6b393018bf31fab`.
- E1 is unchanged: Stage B is environment-blocked by sibling occupancy. No
  Stage-B or Stage-C performance test, CPU adjustment, governor change, or old
  timing rebinding was performed. Parent review3 and overall performance
  acceptance remain the parent agent's responsibility.
