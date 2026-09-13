# Conventional-commit release automation

Status: implementation authorized

## Contract

A push to main runs CI. Only the push event's tip commit with exact subject `chore(release): vMAJOR.MINOR.PATCH` requests publication. Stable numeric SemVer only, no leading zeroes, no prereleases in this first version; reject malformed release-looking subjects explicitly. Normal commits do not publish. Use event commit SHA, never a floating main checkout or branch head. Require the declared CMake project version to match the requested version.

Before publication, compile Release build, run full existing CTest suite, install the library to an isolated prefix, and compile/run an independent consumer using only the installed package via find_package(qalsh CONFIG REQUIRED), linking qalsh::qalsh and exercising an actual query. Never connect CI to Centaurus; GitHub hosted Linux runners are the CI environment. Development verification uses one-way rsync to Centaurus, no heavy local builds.

After all verification passes, use job-scoped contents:write GITHUB_TOKEN to create an annotated vX.Y.Z tag with message chore(release): vX.Y.Z on the exact tested triggering commit, and publish a non-draft GitHub Release with generated notes. No manual source packaging, assets, release PR, PAT, semantic-release or second workflow triggered by tag creation. GitHub's default source archives suffice. Never overwrite/delete existing tags/releases. A tag already resolving to the expected commit is safe to reuse; a conflicting target is an error. Interrupted runs must resume safely, including tag-existing/release-missing, existing correct published Release, or draft created by the same operation. Document limits and handle external preexisting draft conservatively. Avoid expression-to-shell injection from commit subjects. Serialize publication safely, do not cancel an in-flight release. Normal/PR verification must not get write permissions. Do not run on pull_request_target. Actions dependencies should be pinned to known verified upstream revisions or use existing repo conventions.

## Deliverables

Focused .github workflow(s), small auditable automation helpers/tests where necessary, standalone installed-consumer smoke fixture, README instructions including updating CMake version, exact commit title, main tip limitation, recovery and repeat-safe behavior. Test parsing (normal/release/malformed/leading-zero/version-mismatch/multiline subject), tag conflict/idempotent release decisions using mocks/fixtures without writing GitHub. Validate YAML and workflow semantics with tooling when available; do not falsely claim a hosted run occurred.

Preserve all existing QALSH production code, API, formats, untracked historical research and consumer branches. No algorithm changes, benchmark campaigns, actual tag/Release creation, commit or push. Parent reviews implementation directly. Max three reviews, at most two fix attempts before parent takeover. One implementation agent only; no nested agents.
