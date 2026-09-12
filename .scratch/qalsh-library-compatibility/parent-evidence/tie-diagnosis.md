# H toy override tie discrepancy

Final-source case: m=32, beta=0.01, complete-radius=true, k=100,
query 39. Frozen H baseline returns point9097 at rank99; migrated H returns
point6030. Both report float32 distance12533.3232421875. Result counts remain100.

Independent double-coordinate Euclidean distances, computed with Python
math.fsum and sqrt from the actual toy coordinate files:

- 9097: 12533.32352368764, rounds to12533.3232421875.
- 6030: 12533.323284262848, rounds to12533.3232421875.

The frozen H source compares heap candidates by distance only and replaces
only on a strictly smaller distance; final sorting is also distance-only.
The shared engine uses deterministic (float32 distance, point ID) ordering.
Thus the lower-ID current answer is consistent with the library ordering,
but does not preserve the baseline's chosen tied neighbor. The independently
computed real distance also favors6030; neither fact grants an acceptance
exception. This is a concrete unresolved equal-distance-ID policy discrepancy,
in addition to the existing N1 distance-rounding differences.

Source references: frozen h-baseline-source/src/ann_searcher.cc lines120,175,303;
current src/qalsh.cc InsertNeighbor. Full raw records are in
final-coverage/h-toy-override-complete1-{baseline,current}.tsv and its quality JSON.

GIST final-source B+ cases add the same compatibility concern. At k100/default,
51queries have reordered IDs, all within pairs tied in both versions; ID sets
are identical. At k100/complete,58queries reorder such ties and2change cutoff
membership: query30 old970785/current9010 at1.1136281490325928; query36
old391088/current391064 at1.7872439622879028. These are not accepted exceptions.
Full machine-readable checks: h-tie-audit.json.
