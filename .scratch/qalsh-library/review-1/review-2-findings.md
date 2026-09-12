# Direct cumulative review — round 2 targeted fixes

Status: **approved for the reviewed areas**; overall acceptance remains open pending the user's performance/scope decision and final revision pinning.

Reviewer: targeted external review agent.

## Scope

Reviewed the latest uncommitted library and consumer changes after round-1 fixes, focusing on:

- `scan_impl` progress with `scan_quantum == 1` and overflow-safe `max_entries` splitting in both index backends and checked/trusted paths;
- B+ leaf-chain ordering validation and local cursor-state synchronization;
- the scalar `SearchStrategy::on_projection_hit_fast_path` overload and projection-values callback integration, including the external H override;
- mmap entry decoding and checked/trusted path equivalence.

## Result

No remaining concrete correctness or standards issues were found in this scope. The trusted mapped-entry path uses `memcpy` decoding rather than typed mapped reads, avoiding alignment, object-lifetime, and strict-aliasing dependencies.

## Added regression coverage

`tests/public_test.cc` now exercises quantum-one checked and trusted scans, the default point-ID fast path, a maximum `size_t` checked scan budget, and rejection of a reciprocal-but-reordered B+ leaf chain. The public test passes with assertions active in local and Centaurus Release/ASan/UBSan builds.

## Validation

- Local `qalsh-lib` Release public test: passed.
- Local `qalsh-lib` ASan/UBSan public test: passed (`detect_leaks=0` on macOS, where leak sanitizer is unsupported).
- Centaurus GCC 15 `qalsh-lib` Release CTest: 1/1 passed.
- Centaurus GCC 15 `qalsh-lib` ASan/UBSan CTest: 1/1 passed.
- Centaurus GCC 15 `qalsh4c` Release CTest: 23/23 passed.
- Centaurus GCC 15 `qalsh-h` Release CTest: 1/1 passed.
- Linux Docker GCC 15 bind-mounted toolchain validation: library 1/1, qalsh4c 23/23, and qalsh-h 1/1 passed; fixed-seed H default, k=100, complete-radius, and exact control commands ran successfully.

This approval does not close the separately documented performance regressions, missing immutable library revision, or user's acceptance decision.
