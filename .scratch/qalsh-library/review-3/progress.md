# Parent review 3 — post-v2 checkpoint

**Current status: not accepted, review count remains 2/3. No formal matrix is running.**

- `paired-v2` exited1 at the reverse quality gate. The C query harness linked OpenMP but omitted `QALSH4C_USE_OPENMP`; corrected baseline/migrated MNIST L1 ba results match all67,000 rows exactly. Earlier query-only comparisons are not authoritative. Later source changes also obsolete v2 for final acceptance. Preserve v1/v2 raw evidence.
- Transport is now `ssh -S /tmp/qalsh-review3-reconn centaurus` (not the old socket below).
- Retained post-v2 core changes: completed-table counters, empty-window shortcut, SIMD finite validation, cached range geometry, default collision countdown, scalar/lazy hit statistics for typed default dispatch. The latter keeps a full live snapshot for arbitrary strategies and materializes exact hit counts before every evaluation callback.
- Private owning-backend scans now rely on factory/reset-validated cursors and engine-validated numeric bounds. Public scans retain foreign cursor, quantum and bound validation; scan-scope validation remains in the common scanner.
- New construction-only `IndexConfig::build_threads` (default1, max1024) sorts independent tables using standard threads. Accessors remain serial. C forwards its configured OpenMP thread count. Package exports now resolve standard `Threads::Threads`. Parallel-build tests verify serial scratch access and byte-identical disk output.
- H now shares a read-only mapping of caller-owned point data, copying requested rows into per-thread scratch. Lifetime/truncation tests passed Release before newest core changes. Dataset mutation/truncation while mapped is unsupported. Measured mapped GIST RSS was4,043,196KiB, zero major faults/file inputs; this is a resource finding, not evidence of low RSS. README documents file-backed accounting.
- Source archive `post-v2-source-1.tgz`, SHA256 `13fc44c33f9360edad1f9003bca9ff5b16132a6926f545e8a70b8f9356067c80`, preserves current library/C/H source and harness inputs. `post-v2-validation-1/all.done` records: core4/4 Release+ASan/UBSan+Werror; C23/23 Release+ASan/UBSan; H1/1 Docker Release+ASan/UBSan+Werror. Installed-package/installed-consumer reruns remain outstanding. Full C Werror was not rerun or claimed green: unchanged baseline test initializer warnings remain.
- Diagnostic trace copies `c-trace-{baseline,migrated}` show identical per-query Federalist work:1,880 queries,37,722,223 hits,1,881 evaluations, zero differing rows.
- `micro-v3` is diagnostic, NOT the formal final matrix. Ten alternating pairs, old close-binding protocol, before owned-cursor/parallel-sort changes: Federalist L1 CLI ab T1 ratio1.0881 CI[1.0806,1.0999], ab T4 ratio1.2418 CI[1.1732,1.3033], both T4 ratio1.2494 CI[1.2238,1.2714]; MNIST L1 ba query ratio1.1058 CI[1.1007,1.1172]. Genuine regressions remain; no approved5% margin.
- Later owned-cursor single exploratory run: MNIST ba11,176.76ms, Federalist ab98.79ms. Latest parallel-sort exploratory T4 open/query ms (baseline→migrated): close binding4.607/24.718→5.645/34.605; no binding4.109/27.359→4.890/28.298. Single observations are not acceptance. Standard child threads inherit the calling thread's affinity; this interaction with OpenMP binding needs investigation, not silent protocol changes.
- Rejected/reverted experiments: validated-ID range tag/unchecked default hits; query-level custom-rule driver specialization; borrowed snapshot references. Scalar/lazy default counters are retained; earlier full-snapshot lazy counting was a separate rejected experiment.
- Before any formal v3 launch: freeze/archive actual source and binaries, rerun complete validations and quality/projection fingerprints, choose a fresh output directory (paired.py still targets v2), then complete all seven phases and review resource/quality findings. Do not overwrite profiled binaries before preserving their symbols.
- Rsync warning: `--exclude 'build*'` also excludes `tests/build_test.cc`; that file was subsequently synchronized explicitly. Future exclusions should be root-directory-specific.

## Historical v2 launch checkpoint (superseded)


**Not accepted. Actual parent reviews completed: 2/3. Review3 remains in progress.** Exhausted delegated findings remain parent-owned; no more agents, commits or pushes.

## Running job

Centaurus connection: `ssh -S /tmp/qalsh-review3-conn centaurus`.

- Container `qalsh-parent-paired-v2`, ID `0ccb55c6f57d9232052a4d7e25abff75765b8acd228f5172dd70baee500c79be`.
- Docker `qalsh-cxx15-deps:latest`, `/usr` read-only, network none, CPU quota4.
- SSD `/home/jonathanhu237/code/qalsh-parent-validation3`, mounted `/work` and original absolute path.
- Local `run_final.sh` runs fingerprints, then `c h reverse sampling controls build cold`, then ending fingerprints/all.done.176 scenarios, excluded warmup plus10 alternating pairs each, seeded bootstrap20k.
- Results `/work/paired-v2`; per-case raw outputs, commands, TSVs and summaries. Query warmups are quality-gated before measured pairs. Sampling cache vectors are archived. Cold-index checks advise only singly-linked task-owned index files, verify mincore residency, and time **open+query**, never mislabel post-validation queries as cold.
- No concurrent builds/heavy validation while the job runs. All H execution remains Linux Docker. No global cache drops or service disruption.

Version1 was deliberately stopped after newly found completion-state bugs. Its raw evidence/fingerprints remain intact and are not final-source evidence.

## Parent fixes since review2

- Scalar sentinel HitDecision plus one private range scanner/driver per backend removed optional ABI/per-hit dispatch overhead without H-specific modes or public fast callbacks. Typed/erased/external dispatch remains tested, including virtual inheritance and live counters.
- Unified bounded/unbounded public/engine distance calculation: block64 double intermediates, conservative bounds, consistent reduction and explicit float32 overflow behavior. Original red repro now prints `51.7198 / 51.7198 exact1` and finite `1e20`.
- Corrected partial results incorrectly marked complete, and exhausted-table round completion being cleared on radius advance. Red logs retained; permanent tests green.
- Window width*radius/2 now uses a wide intermediate: representable windows no longer fail from intermediate multiplication overflow. Regression searches a point at2e38.
- Grouped independent projection tables overlap reductions while preserving each table's arithmetic; tested against single-table projection at dimensions1/3/4/5/63/64/65/300/784/960. Query and both builders share ProjectPoint.
- B+ build leaf plans now borrow sorted entry tables (one retained entry copy), with exact page-plan reservation. This supersedes the earlier transfer-and-release staging fix.
- Entire GIST serialized index SHA256 remains unchanged after grouped projection and borrowed leaf plans, not merely coefficient vectors.
- Unused H dot/normal/PDF/RNG helpers removed; exact L2 and H CDF remain. C compatibility float parameter regularization remains explicit.

## Final-source validation completed

- Core3/3 Release,3/3 ASan/UBSan,3/3 Werror.
- C23/23 Release and23/23 ASan/UBSan.
- H1/1 Docker Release,1/1 ASan/UBSan,1/1 Werror.
- Installed public consumer smoke: typed strategy, memory/disk. Actual C installed-package build/tests23/23; H installed-package build/tests1/1.
- Parent round1/round2 repros rerun against installed final archive: equal-bound exact, unary construction/open, immediate stop, invalid threshold, quantum1, budget100/k101 evaluates200, released index ownership, malformed root routing rejection all correct. Raw `/work/repros`.
- Strict full C build was additionally attempted and **failed** on existing missing-field-initializer warnings in dataset_loader_test.cc/dataset_bad_bmp_test.cc. Do not claim full C Werror passed or clean unrelated files. Both files hash-match frozen266118b exactly (491694cf… and15718dd0… respectively); no unrelated initializer cleanup made.
- Local syntax-only Clang Werror core and three git diff checks passed. Frozen spec SHA remains `ea1a6a7c83bb850f3847557aef84f9116da38a2b5ad1f3c6937c3a4a81cc6dc4`; consumer branches remain `refactor/use-qalsh-lib`.
- Twelve actual projection groups byte-identical: eight C Federalist/MNIST L1/L2 A/B including memory/disk equivalence; H toy/default, toy m32/B1024, GIST/default, GIST m32/default page. H point inputs are hash-checked across variants too.
- Independent C double oracles include full MNIST reverse. Independent H exhaustive double top100 now covers all toy queries and GIST's entire1M base x frozen first100 queries. Tools `exact_h.cc`, `prepare_h_oracles.py`; `/work/oracles/*-l2-top100.tsv`. Official-vs-exhaustive raw ID recall is1.0 for toy and0.9997 for GIST (3/10000 IDs differ); boundary-tie attribution still needed.

## Exploratory—not acceptance—observations

- Corrected GIST build with grouping/borrowed leaf plans:10.69s, maxRSS565868KiB; earlier frozen baseline11.56s/553092KiB. Formal paired build supersedes this single comparison.
- Prior corrected GIST k100 query13.12s vs baseline17.60s; raw recall both.0914 and no beyond-tolerance distance changes. Rank-ID164 changes need set/tie attribution.
- GIST open remains roughly525ms versus baseline0.9ms, and mapped RSS increases markedly. Must report independently of query improvements.
- A completed v2 Federalist L2 both/memory/T4 command row currently shows ratio1.12294, CI[1.10449,1.14265]. This is a real remaining performance finding to investigate, not average away with disk wins. Keep measuring the frozen source to locate all remaining regressions before further changes.
- No approved5% margin; nonsignificance is not equivalence.

## Paths and remaining work

Library `/tmp/qalsh-parent-review3/qalsh-lib`; production C `/tmp/qalsh-parent-review3/c` linked source `/tmp/qalsh-fix-attempt2/qalsh4c`; production H source `/tmp/qalsh-parent-review3/qalsh-h`, build `h-docker`.
Frozen C `/work/c-baseline` revision266118b6851a30f5dfc7ba80b00760e887578f47; H `/work/h-baseline` revisiona8c602605661ebc64e19e535f19424982e58e2c0.
C datasets `/tmp/qalsh-fix-attempt2/{federalist,mnist}-{baseline,migrated}`. H toy old `/tmp/qalsh-fix-attempt2/h-toy-pinned-baseline`, new `/tmp/qalsh-parent-review3/h-data`; explicit toy `/work/h-explicit-{baseline,migrated}`; GIST `/work/gist-{baseline,migrated}`, explicit GIST `/work/gist-explicit-{baseline,migrated}`.

1. Observe actual job completion/failure; continue fixing tool/measurement failures, preserving raw observations. Do not assume success from container launch.
2. Analyze every quality artifact, per-query recall/ties and returned sets, estimates/sample counts/cache vectors, controls and resource endpoints. No hidden cross-case averaging.
3. Finish cumulative Standards/Spec review3 and update findings/parent ledger/acceptance manifest. Current completed count remains2.
4. Copy evidence locally. Source hashes are interim provenance, not immutable Git pins; commit authorization and user acceptance remain pending.
