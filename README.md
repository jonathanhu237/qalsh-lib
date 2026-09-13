# qalsh-lib

`qalsh-lib` is a reusable C++20 static library for immutable QALSH projection
indexes and search execution. It provides an ephemeral in-memory sorted-array
index plus persistent B+ tree and sorted-array layouts. Original points remain
owned by the caller and are retrieved by point ID through a borrowed
`PointView` accessor.

## Build and install

```sh
cmake -S . -B build -DQALSH_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/.local"
```

An external CMake project links `qalsh::qalsh` after
`find_package(qalsh CONFIG REQUIRED)`. Source-tree consumers can use
`add_subdirectory(path/to/qalsh-lib)` and link the same target.

The library has no third-party runtime dependency. It requires a compiler with
C++20 support and uses the host's native little-endian IEEE-754 `float` binary
representation for its persistent format.

## Release automation

A push to `main` always runs the hosted Ubuntu verification workflow. A release
is requested only by the exact **tip commit** title
`chore(release): vMAJOR.MINOR.PATCH`, using stable numeric SemVer components
with no leading zeroes or prerelease suffix. Before making that commit, update
the `project(qalsh-lib VERSION ...)` declaration in `CMakeLists.txt` to the
same `MAJOR.MINOR.PATCH`; the workflow rejects a mismatch and rejects malformed
release-looking titles explicitly. Pull requests and ordinary commits have a
read-only token and never publish.

For example, after updating `CMakeLists.txt` to `VERSION 1.2.3`, use:

```sh
git add CMakeLists.txt
git commit -m "chore(release): v1.2.3"
git push origin main
```

For the first version, when the CMake version is already correct and no source
change is needed, an empty tip commit is explicit and supported:

```sh
git commit --allow-empty -m "chore(release): v1.2.3"
git push origin main
```

The gate configures a Release build, runs the complete CTest suite, installs to
an isolated prefix, and builds/runs the standalone consumer under
`.github/release/consumer`. That consumer uses only the installed package with
`find_package(qalsh CONFIG REQUIRED)`, links `qalsh::qalsh`, and performs a
real query. A successful release creates an annotated `vX.Y.Z` tag pointing at
the tested event SHA (with message `chore(release): vX.Y.Z`) and a non-draft
GitHub Release with generated notes. GitHub provides its default source
archives; the workflow uploads no custom source package or assets.

Publication is safe to retry. A tag already resolving to the tested commit is
reused, while a conflicting tag is an error. A correctly published stable
release is left untouched. A draft is published only when it carries the
workflow's marker and exact tested commit; an external/pre-existing draft is
never edited and requires manual resolution. Verification runs are not in a
workflow-wide concurrency queue: same-version publication jobs (including
retries) serialize with cancellation disabled, while independent versions may
publish concurrently or out of order. Each release requests GitHub's
`make_latest: legacy` policy rather than blindly making completion order the
latest release. If verification fails, fix the source/version and push a new
exact-title commit, or rerun the same workflow run after a transient failure.
The trigger is intentionally tip-only: pushing a later ordinary commit does
not retroactively release an earlier title. GitHub-hosted Ubuntu and the job's
`contents: write` permission are required; local development validation uses
Centaurus separately and does not create real tags or releases.

## Public query seam

The public header is `include/qalsh/qalsh.h`:

1. Derive or explicitly provide `IndexConfig`, including the metric, dimensions,
   QALSH values, and optionally exact row-major projection vectors.
2. Build an `InMemoryIndex`, or call `PersistentIndex::Build` with a
   `PointAccessor`. Persistent construction defaults to a B+ tree; pass
   `PersistentBuildOptions{.layout = IndexLayout::sorted_array}` for the
   sorted-array file layout.
3. Open a persistent file later with `PersistentIndex::Open`. The file magic
   selects and validates its recorded layout, so callers do not need to repeat
   the construction option.
4. Construct `SearchEngine` with the shared immutable index and the same
   caller-owned point accessor.
5. Use `DefaultQalshStrategy` for ordinary QALSH, or implement the small
   `SearchStrategy` contract for a complete external strategy.

A strategy requests `scan`, `evaluate`, `advance_radius`, or `finish`. A scan
may use the generic `quantum` or resumable `range` scope; the latter is useful
when an external algorithm needs a backend-independent logical boundary. The
engine performs cursor traversal, validates point IDs, invokes the accessor,
computes bounded L1/L2 distances, deduplicates evaluations per query, and
maintains deterministic `(distance, point_id)` top-k ordering. Projection hits
are delivered through one callback, while deferred strategies may request an
ordinary point-ID evaluation through the same action surface. `ScanReport`
distinguishes a logical window boundary from actual table exhaustion, so
deferred work can continue after scan exhaustion.

`PointAccessor` returns a borrowed span. The engine consumes that span before
calling the accessor again. A reusable read buffer is therefore valid, as is a
span into immutable caller-owned memory. Accessors used by concurrent queries
must provide their own synchronization or independent buffers. Search cursors and all mutable query state are local to each `search` call;
concurrent calls therefore share only immutable index resources. The bounded
kernel returns an explicit `exact` flag. An incomplete sum is never reported
as an exact distance; equal-to-bound values are fully evaluated so that
deterministic ID tie handling remains correct. Bounded and unbounded calls use
the same accumulation order and double intermediates, avoiding float32 square
overflow/underflow. Unbounded distances outside float32's range throw
`std::overflow_error` rather than returning a false exact infinity.

`SearchResult::complete` requires all requested neighbors and normal strategy
completion or scan exhaustion. Partial results, step limits and invalid actions
are not complete; the separate termination reason remains available. This is
not an accuracy guarantee.

The default budget is
`candidate_budget + k - 1`, capped by the point count (100 is the default base
budget). An explicit `set_candidate_budget` override can request an absolute
budget or disable budgeting. A caller that supplies a complete termination rule
replaces both the standard budget and distance predicate; qalsh4c explicitly
opts out because its workflow uses incumbent-based termination.

`TableScanSchedule` supplies reusable table queueing, logical window
boundaries, exhaustion tracking, and radius requeueing for external
strategies. A strategy can keep algorithm-specific candidate state without
copying ordinary projection-table orchestration. `IndexConfig::build_threads` optionally parallelizes independent table sorts
(default 1, maximum 1024). It is a construction-only limit, not persisted in the
index. Projection generation and point-accessor calls remain serial, including
when the accessor reuses one scratch buffer. The implementation uses standard
C++ threads, not an OpenMP runtime dependency.

## Persistent format and rebuilds

`PersistentIndex::Build` writes an immutable file atomically through a securely
created temporary file. Overwrite mode uses atomic replacement; the default
mode publishes with an atomic no-replace link, so a concurrent destination is
never clobbered. It stores format/version and byte-order markers, metric, point
count, dimensionality, table count, page/region size, seed, QALSH parameters
(including the candidate budget), actual projection vectors, and the selected
layout's table data. Build and query can run in separate processes. The caller must provide the original
point set corresponding to the index; metadata cannot prove dataset identity,
so changing points requires a rebuild. Historical qalsh4c/qalsh-h index files
are not read by this library.

The static library intentionally does not own or copy the original dataset.
Index construction can use a temporary accessor buffer, and query access can
read one point at a time from a file or cache. `PersistentIndex::Open` reports
failures with C++ exceptions rather than exiting the host process.

## Source provenance

The implementation in this repository is original MIT-licensed library code.
The QALSH parameter formulas and search concepts are based on Huang et al.,
*Query-Aware Locality-Sensitive Hashing for Approximate Nearest Neighbor
Search*, PVLDB 2015 (<https://www.vldb.org/pvldb/vol9/p1-huang.pdf>). The
qalsh4c and qalsh-h migrations retain their upstream notices and keep
consumer-specific strategies outside this generic core; GPL reference
repositories were not copied into this MIT library.
