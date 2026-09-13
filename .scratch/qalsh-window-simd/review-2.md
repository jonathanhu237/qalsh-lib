# Direct review 2 — SIMD window-prefix candidate

Status: changes requested; performance still environment-blocked.
Fixed code baseline722ecfb; unchanged spec fa8a00a2104d341054531f018792e925183d346b2b3f80caabab4889428ee5f2.
Parent reviewed new trace coverage assertions, full forced-scalar CMake build wiring, updated checker and logs; independently built and ran two checker negative fixtures on Centaurus. No performance retry.

## Standards

No hard documented-standard finding. Previous optional scalar-tail duplication note is unchanged and not a reason to invalidate measured production code.

## Spec

R1 resolved at source/log inspection: traces now use quantum15 and explicitly count nonempty partial ranges with available>=6 in both directions; assert partial, exhaustion and deferred events; exercise memory/B+/array against independent linear prefix reference. Full forced-scalar production build is recorded, including a distinct library hash and 7/7 full tests. Unique temporary directories address test collisions. No performance benefit follows merely from these passes.

### R2 remains high — quality checker still grants false passes near zero

The fail-closed rewrite addresses gross regressions/malformed IDs, but absolute tie/quality tolerances1e-9 collapse genuinely different very-small distances into an exact tie. CompareQuery then skips ratio regression when both are labeled exact. Parent reproduction using query[0], base[0,5e-10], original ID0/distance0, candidate ID1/distance5e-10 returns exit0 with candidate_exact1, candidate_ratio=inf and quality_failures0. This violates the independent exact-hit/quality policy; it is not an encoding roundoff tie. The fixed1e-4 encoding tolerance also accepts reported9e-5 for a mathematically exact zero distance, another false pass.

Evidence: parent_quality_edge_repro.py and parent-quality-review2.log; source was compiled with g++-15 -std=c++20 -O2 in /tmp/qalsh-window-review2, no library code invoked by checker. Both expected-rejection cases instead exited0. Fix tie/quality comparison to distinguish actual ties and justified numerical uncertainty using magnitude/error-aware rules; zero-distance references must not silently admit nonzero answers or unrelated nonzero encodings. Do not hide approximation regression behind tolerance-derived 'both exact'. Retain equal ties and valid final float32 rounding; avoid replacing this with another unreasoned absolute constant. Save rerunnable fixture source, not only a one-line success log. Verify adjacent float32/tiny/subnormal and normal-scale cases as well as existing negative fixtures.

This is not an observed regression in the1880 Federalist rows: those selected IDs match and checks passed. It is a verifier defect that prevents trusting the claimed general quality gate.

E1 remains environment-blocked. No more CPU adjustments/performance runs until a reserved physical-core/time window is authorized. Stage C remains intentionally unrun. Do not drop unfavorable timings or claim full objective achieved.

## Counts

Standards0 hard findings. Spec1 unresolved fixable finding (R2 high); R1 closed; E1 external blocker.
Review2/3. R2 completed fix attempts1; next fresh Luna is its second and last delegated attempt. R1 completed1, resolved. Keep production SIMD helper unchanged absent an independently found bug. No commit/push.
