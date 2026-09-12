# Fixed comparison protocol for subsequent runs

Status: protocol frozen before delegated-fix paired measurements; scenario inventory owned by implementation agent.

This protocol does not retroactively turn initial single-run pilots into acceptance. Every case must identify dataset coordinates/truth/query-population hashes, effective parameters and projection hashes, layout, mode, thread count, current source/binary/library/harness hashes and frozen original revision. Changing any source or compiler setting starts a fresh output directory and a new case fingerprint.

## Timing and uncertainty

- Optimize both binaries with the same compiler/version, release flags and relevant feature macros. Confirm actual OpenMP concurrency. Record container image ID, host CPU, affinity, competing workload, and cache conditions.
- Open once per measured process, prepare queries and worker team before query timing, then measure the complete declared query population with existing harness. Opening and output serialization stay outside query_ms. Retain separate end-to-end command/resource rows.
- Warm both binaries once, then run at least ten pairs. Alternate order AB/BA deterministically; do not discard an inconvenient pair. Repeat population within a batch when wall time is too short, using the identical repetition factor for both binaries. Never use a smaller effort/candidate budget to improve time.
- Primary statistic: per-pair current query_ms / baseline query_ms; report all individual times and ratios, medians and paired geometric mean ratio.
- Uncertainty method selected in advance: percentile paired bootstrap on log ratios, 20,000 resamples, deterministic RNG seed 20260912, 95% interval exponentiated back to ratios. Report this interval as measurement uncertainty, not an approved equivalence margin.
- An interval wholly above 1 is evidence of slowdown; wholly below 1 supports improvement for the measured population. Overlap with 1 remains inconclusive. No positive slowdown allowance is approved. Increase duration/repetitions if noise dominates; document any change before collecting replacement measurements and preserve original runs.
- Compare current B+ versus current array in separately identified pairs too. Those ratios do not replace comparisons with original consumers.

## Quality and resources

Preserve full precision per-query IDs/order/distances and application outputs, plus projection verification through real seeded construction. Check exact output differences and independent numerical/exhaustive references where appropriate before interpreting speed. N1 numerical policy remains pending; timing during that period is diagnostic, never unconditional acceptance.

Measure build/open/full-command separately, disk bytes and peak memory with file-backed accounting. Validation during open touches the index: the resulting query run is not cold. Do not equate logical read counters with physical I/O. Missing runs, failed gates, and uncertain rows remain visible.

The scenario inventory must map every required control to an actual command/query case before broad runs: C memory/disk, L1/L2, ab/ba/both, T1/T4, weights/sampling; H default/complete-radius, k1/k100, parameter overrides/exact control; Federalist/MNIST and toy/GIST with both persistent options. Avoid an unnecessary full Cartesian product, but justify representative selections and label any restricted query population explicitly.
