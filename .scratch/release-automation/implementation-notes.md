# Implementation notes

- Added `.github/workflows/release.yml` with read-only push/PR verification and a serialized, job-scoped `contents: write` publication job.
- Added `.github/release/release_automation.py`, which classifies the exact checked-out commit, checks the CMake version, creates/reuses a commit-resolving annotated tag, and safely resumes owned draft/generated-notes releases through `gh api` argument vectors. Draft discovery fails closed at its pagination safety bound; releases request GitHub's `make_latest: legacy` policy.
- Added focused no-network helper tests and the standalone installed-package consumer fixture under `.github/release/`. Publication concurrency is scoped per validated version, not the verification workflow.
- Documented the exact title/version workflow, verification gate, tip-only trigger, recovery, draft ownership, and repeat-safe behavior in `README.md`.

No production C++ files, tags, commits, pushes, or GitHub objects were changed.
