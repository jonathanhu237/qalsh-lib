---
status: accepted
---

# Default to B+ trees with an optional sorted-array file layout

QALSH consumers need shared indexing and search code without coupling their algorithms to one persistent layout. Provide B+ trees by default and allow callers to select a sorted-array file through a construction option, while retaining the existing ephemeral in-memory index. This accepts the maintenance cost of two persistent layouts in exchange for backend choice and controlled performance comparisons within the same library; both reuse the common search execution and strategy contract.

The index records its layout so opening an existing file identifies the backend without a conflicting caller choice. Backend selection must preserve the strategy's documented logical scan and termination boundaries. The main workload opens an index once and runs many queries, so steady-state query performance is the primary timing concern; opening time and memory costs remain separately visible.

This decision replaces the earlier first-version exclusion of persistent sorted arrays. It does not approve a slowdown allowance or authorize implementation during the ongoing requirements discussion.
