# Validation notes

Validation was performed in three explicitly separate environments. No hosted
GitHub Actions run or real tag/release publication was attempted.

- **Local macOS:** Python focused helper tests (including mocked API/subprocess
  behavior), Python bytecode compilation, `git diff --check`, and `actionlint
  1.7.12` for `.github/workflows/release.yml` passed.
- **Centaurus:** One-way rsync synchronized the source to
  `/tmp/jonathanhu237-qalsh-lib-release-automation-20260913`; substantial
  validation ran from that dedicated directory on `centaurus` (CMake 4.2.3,
  GCC 15.2.0, Python 3.13.13). Focused tests passed (14/14), the Release build
  passed, the full CTest suite passed (7/7), installation passed, and the
  independent installed consumer configured, built, and passed its query test
  (1/1). Remote `actionlint@1.7.12` also accepted the workflow.
- **Hosted GitHub Actions:** not executed here. The workflow is configured for
  hosted `ubuntu-24.04`; its publication path was not exercised against GitHub.
- The `gh api` subprocess seam and all tag/release decisions were exercised
  only with temporary fakes/in-memory fixtures, so no network or GitHub write
  is part of this evidence.
