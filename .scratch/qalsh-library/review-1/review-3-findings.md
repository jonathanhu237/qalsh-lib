# Final acceptance review 3

Review verdict before the fixes below: NEEDS CHANGES.

Findings:

1. P1: internal bounded L2 evaluation squared the rounded float bound directly, so an equal-distance candidate (notably a rounded `sqrt(2)`) could be pruned before point-ID tie ordering.
2. P1: H artifact renames were not directory-fsynced before the durable `.qalsh-current` manifest, leaving a durability window.
3. P2: H versioned generations accumulated across successful rebuilds.

Resolutions:

- `src/qalsh.cc` now uses a next-representable finite L2 bound and also compares the computed partial `sqrt(sum_squared)` before early pruning. The public regression uses two diagonal equal-distance points with the larger ID visited first and requires the smaller ID.
- `qalsh-h/src/config.cc` flushes the H index directory after artifact renames and before writing the manifest; after the manifest commit it removes superseded versioned/staging artifacts and flushes the directory again. The H strategy test rebuilds/publishes twice and checks that only the active versioned pair remains.
- All changes remain uncommitted and unpushed.

Post-fix validation: qalsh-lib Release, ASan/UBSan, and `-Wall -Wextra -Werror` CTest `1/1`; qalsh4c source Release and ASan/UBSan CTest `23/23`; qalsh-h source Release, ASan/UBSan, and `-Wall -Wextra -Werror` CTest `1/1`; Docker GCC 15 bind-mounted-toolchain CTest qalsh-lib `1/1`, qalsh4c `23/23`, qalsh-h `1/1`; fixed-seed H rebuild/query controls passed with zero partial queries. Parent verification also rebuilt a truly fresh H toy index without direct compatibility files, confirmed one active versioned pair after repeated rebuilds, and reran the rejected-rebuild hash check.
