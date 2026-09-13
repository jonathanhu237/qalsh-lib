#!/usr/bin/env python3
"""Bounded positive/negative fixtures for the fail-closed Stage-B comparator."""
from __future__ import annotations

import math
import os
import pathlib
import struct
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parent
SOURCE = ROOT / "stage_b_quality_compare.cc"


def write_floats(path: pathlib.Path, values: list[float]) -> None:
    path.write_bytes(struct.pack("<" + "f" * len(values), *values))


def write_results(path: pathlib.Path, rows: list[tuple[int, int, float]]) -> None:
    path.write_text("\n".join(f"{qid} {point} {distance:.9g}" for qid, point, distance in rows) + "\n")


def run(binary: pathlib.Path, query: pathlib.Path, base: pathlib.Path, nq: int, nb: int,
        dimensions: int, baseline: pathlib.Path, candidate: pathlib.Path, expected: int) -> str:
    command = [str(binary), str(query), str(base), str(nq), str(nb), str(dimensions),
               str(baseline), str(candidate)]
    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    if completed.returncode != expected:
        raise RuntimeError(
            f"expected return {expected}, got {completed.returncode}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    return completed.stdout + completed.stderr


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="qalsh-quality-compare-") as raw:
        directory = pathlib.Path(raw)
        binary = directory / "stage_b_quality_compare"
        compile_command = [
            "g++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Wpedantic",
            str(SOURCE), "-o", str(binary),
        ]
        subprocess.run(compile_command, check=True)

        query = directory / "query.bin"
        base = directory / "base.bin"
        write_floats(query, [0.0, 10.0])
        write_floats(base, [0.0, 0.0, 10.0])
        baseline = directory / "baseline.tsv"
        tie_candidate = directory / "tie-candidate.tsv"
        write_results(baseline, [(0, 0, 0.0), (1, 2, 0.0)])
        write_results(tie_candidate, [(0, 1, 0.0), (1, 2, 0.0)])
        positive = run(binary, query, base, 2, 3, 1, baseline, tie_candidate, 0)
        if "summary queries 2" not in positive or "query 0 status pass" not in positive:
            raise RuntimeError(f"positive comparator output omitted per-query status:\n{positive}")

        recall_loss = directory / "recall-loss.tsv"
        write_results(recall_loss, [(0, 2, 10.0), (1, 2, 0.0)])
        recall_output = run(binary, query, base, 2, 3, 1, baseline, recall_loss, 5)
        if "failure recall_loss" not in recall_output:
            raise RuntimeError(f"recall-loss fixture did not fail closed:\n{recall_output}")

        nonexact_query = directory / "nonexact-query.bin"
        nonexact_base = directory / "nonexact-base.bin"
        write_floats(nonexact_query, [0.0])
        write_floats(nonexact_base, [1.0, 2.0])
        nonexact_baseline = directory / "nonexact-baseline.tsv"
        nonexact_candidate = directory / "nonexact-candidate.tsv"
        write_results(nonexact_baseline, [(0, 0, 1.0)])
        write_results(nonexact_candidate, [(0, 1, 2.0)])
        nonexact_output = run(binary, nonexact_query, nonexact_base, 1, 2, 1,
                              nonexact_baseline, nonexact_candidate, 5)
        if "approximation_regression" not in nonexact_output:
            raise RuntimeError(f"non-exact farther fixture did not fail closed:\n{nonexact_output}")
        if "baseline_exact 1" not in nonexact_output or "candidate_exact 0" not in nonexact_output:
            raise RuntimeError(f"non-exact fixture did not expose the exact classification:\n{nonexact_output}")

        # The two parent-review repros must fail even though their distances
        # are much smaller than the old absolute 1e-9 tie tolerance.
        zero_query = directory / "zero-query.bin"
        zero_base = directory / "zero-base.bin"
        write_floats(zero_query, [0.0])
        write_floats(zero_base, [0.0, 5.0e-10])
        zero_baseline = directory / "zero-baseline.tsv"
        zero_candidate = directory / "zero-candidate.tsv"
        write_results(zero_baseline, [(0, 0, 0.0)])
        write_results(zero_candidate, [(0, 1, 5.0e-10)])
        zero_output = run(binary, zero_query, zero_base, 1, 2, 1,
                          zero_baseline, zero_candidate, 5)
        if ("candidate_exact 0" not in zero_output or
                "approximation_regression" not in zero_output):
            raise RuntimeError(f"zero-to-nonzero fixture was accepted:\n{zero_output}")

        wrong_zero = directory / "wrong-zero.tsv"
        write_results(wrong_zero, [(0, 0, 9.0e-5)])
        wrong_zero_output = run(binary, zero_query, zero_base, 1, 2, 1,
                                zero_baseline, wrong_zero, 5)
        if "candidate_reported_distance_mismatch" not in wrong_zero_output:
            raise RuntimeError(f"wrong zero encoding fixture was accepted:\n{wrong_zero_output}")

        # A nonzero tie at ordinary scale remains valid, while an adjacent
        # float32 distance is a real quality loss rather than an exact tie.
        tie_query = directory / "scale-tie-query.bin"
        tie_base = directory / "scale-tie-base.bin"
        write_floats(tie_query, [10.0])
        write_floats(tie_base, [1.0, 1.0])
        tie_baseline = directory / "scale-tie-baseline.tsv"
        tie_candidate = directory / "scale-tie-candidate.tsv"
        write_results(tie_baseline, [(0, 0, 9.0)])
        write_results(tie_candidate, [(0, 1, 9.0)])
        run(binary, tie_query, tie_base, 1, 2, 1,
            tie_baseline, tie_candidate, 0)

        adjacent_query = directory / "adjacent-query.bin"
        adjacent_base = directory / "adjacent-base.bin"
        one_bits = struct.unpack("<I", struct.pack("<f", 1.0))[0]
        adjacent = struct.unpack("<f", struct.pack("<I", one_bits + 1))[0]
        write_floats(adjacent_query, [0.0])
        write_floats(adjacent_base, [1.0, adjacent])
        adjacent_baseline = directory / "adjacent-baseline.tsv"
        adjacent_candidate = directory / "adjacent-candidate.tsv"
        write_results(adjacent_baseline, [(0, 0, 1.0)])
        write_results(adjacent_candidate, [(0, 1, adjacent)])
        adjacent_output = run(binary, adjacent_query, adjacent_base, 1, 2, 1,
                              adjacent_baseline, adjacent_candidate, 5)
        if "recall_loss" not in adjacent_output or "approximation_regression" not in adjacent_output:
            raise RuntimeError(f"adjacent float32 regression was not rejected:\n{adjacent_output}")

        # Both rows can be non-exact; ratio checking must still reject a
        # farther candidate and not rely on an exact-hit branch.
        both_nonexact_query = directory / "both-nonexact-query.bin"
        both_nonexact_base = directory / "both-nonexact-base.bin"
        write_floats(both_nonexact_query, [0.0])
        write_floats(both_nonexact_base, [1.0, 2.0, 3.0])
        both_nonexact_baseline = directory / "both-nonexact-baseline.tsv"
        both_nonexact_candidate = directory / "both-nonexact-candidate.tsv"
        write_results(both_nonexact_baseline, [(0, 1, 2.0)])
        write_results(both_nonexact_candidate, [(0, 2, 3.0)])
        both_nonexact_output = run(binary, both_nonexact_query, both_nonexact_base, 1, 3, 1,
                                   both_nonexact_baseline, both_nonexact_candidate, 5)
        if ("baseline_exact 0" not in both_nonexact_output or
                "candidate_exact 0" not in both_nonexact_output or
                "approximation_regression" not in both_nonexact_output):
            raise RuntimeError(f"both-nonexact ratio regression was accepted:\n{both_nonexact_output}")

        # Retain an existing float32 accumulation discrepancy: the independent
        # exact sum is 225.582542..., while a float32 running sum reports the
        # nearby 225.582535.  The scale-aware arithmetic bound accepts it.
        rounding_query = directory / "rounding-query.bin"
        rounding_base = directory / "rounding-base.bin"
        rounding_terms = [89.84093475341797, 98.08934020996094, 37.65226745605469]
        write_floats(rounding_query, [0.0, 0.0, 0.0])
        write_floats(rounding_base, rounding_terms)
        rounding_baseline = directory / "rounding-baseline.tsv"
        rounding_candidate = directory / "rounding-candidate.tsv"
        write_results(rounding_baseline, [(0, 0, 225.58253479003906)])
        write_results(rounding_candidate, [(0, 0, 225.58253479003906)])
        run(binary, rounding_query, rounding_base, 1, 1, 3,
            rounding_baseline, rounding_candidate, 0)

        # Adjacent subnormal values are not collapsed into zero-distance ties.
        min_subnormal = struct.unpack("<f", struct.pack("<f", math.ldexp(1.0, -149)))[0]
        subnormal_query = directory / "subnormal-query.bin"
        subnormal_base = directory / "subnormal-base.bin"
        write_floats(subnormal_query, [0.0])
        write_floats(subnormal_base, [0.0, min_subnormal])
        subnormal_baseline = directory / "subnormal-baseline.tsv"
        subnormal_candidate = directory / "subnormal-candidate.tsv"
        write_results(subnormal_baseline, [(0, 0, 0.0)])
        write_results(subnormal_candidate, [(0, 1, min_subnormal)])
        subnormal_output = run(binary, subnormal_query, subnormal_base, 1, 2, 1,
                               subnormal_baseline, subnormal_candidate, 5)
        if "candidate_exact 0" not in subnormal_output:
            raise RuntimeError(f"subnormal nonzero was treated as exact:\n{subnormal_output}")

        # Finite extreme coordinates must be handled without float32
        # subtraction overflow in the independent reference.
        max_float = struct.unpack("<f", struct.pack("<f", 3.4028234663852886e38))[0]
        extreme_query = directory / "extreme-query.bin"
        extreme_base = directory / "extreme-base.bin"
        write_floats(extreme_query, [max_float])
        write_floats(extreme_base, [-max_float, 0.0])
        extreme_baseline = directory / "extreme-baseline.tsv"
        extreme_candidate = directory / "extreme-candidate.tsv"
        write_results(extreme_baseline, [(0, 1, max_float)])
        write_results(extreme_candidate, [(0, 1, max_float)])
        run(binary, extreme_query, extreme_base, 1, 2, 1,
            extreme_baseline, extreme_candidate, 0)

        nonfinite_data = directory / "nonfinite-data.bin"
        nonfinite_data.write_bytes(struct.pack("<f", math.nan))
        run(binary, nonfinite_data, zero_base, 1, 2, 1,
            zero_baseline, zero_candidate, 3)

        missing = directory / "missing.tsv"
        missing.write_text("0 0 0\n")
        run(binary, query, base, 2, 3, 1, baseline, missing, 3)

        duplicate = directory / "duplicate.tsv"
        duplicate.write_text("0 0 0\n0 1 0\n1 2 0\n")
        run(binary, query, base, 2, 3, 1, baseline, duplicate, 3)

        out_of_range = directory / "out-of-range.tsv"
        write_results(out_of_range, [(0, 99, 0.0), (1, 2, 0.0)])
        run(binary, query, base, 2, 3, 1, baseline, out_of_range, 3)

        nonfinite = directory / "nonfinite.tsv"
        nonfinite.write_text("0 0 nan\n1 2 0\n")
        run(binary, query, base, 2, 3, 1, baseline, nonfinite, 3)

        wrong_distance = directory / "wrong-distance.tsv"
        write_results(wrong_distance, [(0, 1, 1.0), (1, 2, 0.0)])
        wrong_output = run(binary, query, base, 2, 3, 1, baseline, wrong_distance, 5)
        if "candidate_reported_distance_mismatch" not in wrong_output:
            raise RuntimeError(f"wrong-distance fixture did not fail closed:\n{wrong_output}")

        same_file = run(binary, query, base, 2, 3, 1, baseline, baseline, 4)
        if "same file" not in same_file:
            raise RuntimeError(f"same-file fixture did not reject evidence:\n{same_file}")
        alias = directory / "baseline-alias.tsv"
        os.symlink(baseline, alias)
        alias_output = run(binary, query, base, 2, 3, 1, baseline, alias, 4)
        if "same file" not in alias_output:
            raise RuntimeError(f"same-file alias fixture did not reject evidence:\n{alias_output}")

        print("quality comparator fixtures: exact/near ties, zero/nonzero, adjacent float32, "
              "subnormal/extreme values, float32 rounding, both-nonexact regression, "
              "malformed rows/data, and same-file rejection passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
