# Parent review 1

Status: changes requested

Code, workflow, consumer and all focused tests read directly. Core publication target/permissions/shell-injection defenses and installed-consumer seam look sound. No real GitHub writes authorized.

## R1 — shared workflow concurrency can silently drop requested releases

The workflow-wide main concurrency group holds only one running and one pending run by default, even with cancel-in-progress:false. A normal subsequent push can replace a pending release run, silently losing a requested release. Isolate publication concurrency by version (independent versions touch independent tags/releases), and do not put ordinary verification runs in that same queue. Preserve running publications on reruns. Document concurrency behavior and add a regression check over workflow configuration. If independent versions can publish out of order, use/document an appropriate GitHub latest-release policy rather than blindly promoting older versions. No unsupported workflow syntax.

## R2 — bounded draft enumeration fails open

_get_release stops after10 full pages and returns None even though later drafts were not examined. At the bound, fail closed with an actionable error instead of claiming release missing and attempting a write. Add fixture proving that a full capped search does not POST/PATCH a release.

## Nonblocking documentation correction

README lacks copyable git commit/push examples including first-version empty-commit case. Add those and explicitly state GitHub default source archives are provided (no custom assets). Validation notes should distinguish local macOS vs Centaurus vs hosted workflow; current 'Validation is local only' is ambiguous.
