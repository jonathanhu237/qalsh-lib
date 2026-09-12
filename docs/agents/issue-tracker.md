# Issue tracker: Local Markdown

Issues and specs live in this repository, not on GitHub Issues.

## Conventions

- One feature per directory: `.scratch/<feature-slug>/`.
- Spec: `.scratch/<feature-slug>/spec.md`.
- Separate implementation tickets:
  `.scratch/<feature-slug>/issues/<NN>-<slug>.md`, numbered from 01.
- Record workflow state in a `Status:` line, using the invoking
  skill's vocabulary.
- Append discussion under `## Comments`.
- A workflow status does not itself authorize implementation.

## Skill operations

- Publish: create or update the corresponding local Markdown file.
- Fetch: read the referenced local file.
- Do not create remote issues unless explicitly requested.

## Wayfinding

- Map: `.scratch/<effort>/map.md`.
- Tickets: `.scratch/<effort>/issues/NN-<slug>.md`.
- Record dependencies with `Blocked by: NN, NN`.
- Claim with `Status: claimed`.
- Resolve by appending `## Answer`, setting `Status: resolved`,
  and updating the map.
