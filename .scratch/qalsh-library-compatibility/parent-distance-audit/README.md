# Independent returned-distance audit

Parent audit of archived initial-implementation pilot output; not final-source acceptance. The 40 Federalist and 12 toy TSV files were copied from Centaurus implementation-evidence/c-federalist-pilot and h-toy-queries. Local source coordinates were used for this small calculation; SHA256 was verified against remote c-federalist-bplus and h-toy-current-bplus coordinate files. No benchmark ran locally.

All current returned distances match the float32-rounded independent double-coordinate math.fsum/sqrt reference, across the tested layouts/modes/threads. Original outputs have the recorded differences. See summary.json and per-TSV JSON for counts and full discrepancies/hashes. This checks only returned distances, not exhaustive nearest neighbors or projection identity. Numeric compatibility policy remains unresolved.

Verified coordinate hashes:
- Federalist A.bin: 5bb17396ec0d8b77741f059cb484901a46ae62acee3437afb1d72ae05be07efd
- Federalist B.bin: 4ab3e20e57692ef81665c6ac97ad32a41d11b4b7bf99fb843c779e9e720b3e95
- toy base.bin: d7c83b5c773588acc7cead7d9626819e2782ddec0c61bdb610b56559a99ff139
- toy queries.bin: 5bdb37bed9f4cada61021f163854de0b6fef410dace1be15d06b0ede121d9521
