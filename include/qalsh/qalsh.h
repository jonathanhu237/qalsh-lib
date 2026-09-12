#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <concepts>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace qalsh {

using PointId = std::uint32_t;
using Coordinate = float;
using Projection = float;
using Distance = float;

namespace detail {
struct HitRange;
struct RangeExecution;
template <typename Strategy> void DeliverRange(RangeExecution&, const HitRange&);
}

/** The first-version distance kernels supported by the projection index. */
enum class Metric : std::uint32_t {
    l1 = 1,
    l2 = 2,
};

/**
 * Storage layout used by an index. Persistent indexes record this value in
 * their file header and are opened through the matching implementation.
 */
enum class IndexLayout : std::uint32_t {
    in_memory = 0,
    b_plus_tree = 1,
    sorted_array = 2,
};

/**
 * A borrowed view of one original point.
 *
 * A PointAccessor may return a view into an immutable data set or into a
 * reusable scratch buffer.  The library consumes the view before it invokes
 * the accessor again; callers must not retain the view after that call.
 */
using PointView = std::span<const Coordinate>;
using PointAccessor = std::function<PointView(PointId)>;

struct QalshParameters {
    float approximation_ratio{2.0F};
    float bucket_width{0.0F};
    float error_probability{1.0F / 2.71828182845904523536F};
    std::uint32_t num_hash_tables{0};
    std::uint32_t collision_threshold{0};
    /** Base beta*n budget: default top-k uses min(n, base+k-1); zero is invalid. */
    std::uint32_t candidate_budget{100};
    float initial_radius{1.0F};
    float radius_growth{2.0F};
    std::uint32_t scan_quantum{128};
};

/** Derive the standard L1/L2 QALSH parameters without creating an index. */
QalshParameters DeriveQalshParameters(Metric metric, std::uint32_t num_points,
                                      float approximation_ratio = 2.0F,
                                      float error_probability = 1.0F / 2.71828182845904523536F,
                                      std::uint32_t candidate_budget = 100);

struct IndexConfig {
    Metric metric{Metric::l2};
    std::uint32_t num_points{0};
    std::uint32_t num_dimensions{0};
    QalshParameters qalsh{};
    std::uint32_t seed{42};
    /** Row-major vectors, with exactly num_hash_tables*num_dimensions values. */
    std::vector<Coordinate> projection_vectors;
    /** Internal sorting workers (1..1024), not persisted. Accessor calls stay serial. */
    std::uint32_t build_threads{1};
};

struct PersistentBuildOptions {
    /** B+ trees are the default persistent representation. */
    IndexLayout layout{IndexLayout::b_plus_tree};
    /** Physical page/region size used to preserve logical scan boundaries. */
    std::uint32_t page_size{16U * 1024U};
    /** Replace an existing destination atomically when true. */
    bool overwrite{false};
};

struct IndexMetadata {
    Metric metric{Metric::l2};
    std::uint32_t num_points{0};
    std::uint32_t num_dimensions{0};
    std::uint32_t num_hash_tables{0};
    std::uint32_t page_size{0};
    std::uint32_t seed{0};
    std::uint64_t file_size{0};
    QalshParameters qalsh{};
    IndexLayout layout{IndexLayout::in_memory};
};

struct ProjectionHit {
    std::uint32_t table_id{0};
    PointId point_id{0};
    Projection projected_value{0.0F};
    Projection query_value{0.0F};
    float absolute_difference{0.0F};
};

/**
 * The logical extent completed by one projection scan request.  `quantum`
 * bounds work by the caller's entry budget.  `range` completes the current
 * resumable traversal range (a B+ tree leaf for the disk backend) before
 * reporting its boundary; it is not a consumer-specific search mode.
 */
enum class ScanScope : std::uint32_t {
    quantum,
    range,
};

struct ScanReport {
    std::uint32_t table_id{0};
    std::size_t entries_visited{0};
    bool window_exhausted{false};
    bool table_exhausted{false};
    /** True when every selected table has completed this radius window. */
    bool round_complete{false};
    bool all_tables_exhausted{false};
};

class ProjectionIndex {
   public:
    class Cursor;
    using HitCallback = void (*)(void*, const ProjectionHit&);

    virtual ~ProjectionIndex() = default;
    [[nodiscard]] virtual const IndexMetadata& metadata() const noexcept = 0;
    [[nodiscard]] virtual std::span<const Coordinate> projection_vectors() const noexcept = 0;
    [[nodiscard]] virtual std::unique_ptr<Cursor> make_cursor(std::uint32_t table_id,
                                                               Projection query_value) const = 0;
    virtual void reset_cursor(Cursor& cursor, std::uint32_t table_id, Projection query_value) const = 0;
    /**
     * Scan a resumable projection cursor.  The callback is invoked once for
     * every visited entry.  `range` completes one logical traversal range;
     * `quantum` consumes at most max_entries entries. Do not reset or reenter
     * the same cursor during its callback. After a callback throws, reset the
     * cursor before reusing it.
     */
    [[nodiscard]] virtual ScanReport scan(Cursor& cursor, float bound, std::size_t max_entries,
                                          void* context, HitCallback callback,
                                          ScanScope scope = ScanScope::quantum) const = 0;
};

class ProjectionIndex::Cursor {
   public:
    virtual ~Cursor() = default;
};

class InMemoryIndex final : public ProjectionIndex {
   public:
    using ProjectionIndex::scan;
    /** Build an ephemeral sorted-array index. */
    static std::shared_ptr<InMemoryIndex> Build(IndexConfig config, const PointAccessor& accessor);

    [[nodiscard]] const IndexMetadata& metadata() const noexcept override;
    [[nodiscard]] std::span<const Coordinate> projection_vectors() const noexcept override;
    [[nodiscard]] std::unique_ptr<Cursor> make_cursor(std::uint32_t table_id,
                                                       Projection query_value) const override;
    void reset_cursor(Cursor& cursor, std::uint32_t table_id, Projection query_value) const override;
    [[nodiscard]] ScanReport scan(Cursor& cursor, float bound, std::size_t max_entries, void* context,
                                  HitCallback callback, ScanScope scope = ScanScope::quantum) const override;

   private:
    friend class SearchEngine;
    template <typename Visitor>
    ScanReport scan_impl(Cursor& cursor, float bound, std::size_t max_entries,
                         Visitor&& visitor, ScanScope scope) const;
    struct Entry {
        Projection value{0.0F};
        PointId point_id{0};
    };
    struct Table {
        std::vector<Entry> entries;
    };
    IndexMetadata metadata_{};
    std::vector<Coordinate> projection_vectors_;
    std::vector<Table> tables_;

    InMemoryIndex() = default;
};

class BPlusTreeIndex final : public ProjectionIndex {
   public:
    using ProjectionIndex::scan;
    static constexpr std::uint32_t kDefaultPageSize = 16U * 1024U;

    /** Build a persistent, immutable B+ tree index at path. */
    static void Build(const std::string& path, IndexConfig config, const PointAccessor& accessor,
                      std::uint32_t page_size = kDefaultPageSize);
    static void Build(const std::string& path, IndexConfig config, const PointAccessor& accessor,
                      std::uint32_t page_size, bool overwrite);
    /** Open and validate an index produced by Build in a separate process. */
    static std::shared_ptr<BPlusTreeIndex> Open(const std::string& path);

    ~BPlusTreeIndex() override;
    BPlusTreeIndex(const BPlusTreeIndex&) = delete;
    BPlusTreeIndex& operator=(const BPlusTreeIndex&) = delete;

    [[nodiscard]] const IndexMetadata& metadata() const noexcept override;
    [[nodiscard]] std::span<const Coordinate> projection_vectors() const noexcept override;
    [[nodiscard]] std::unique_ptr<Cursor> make_cursor(std::uint32_t table_id,
                                                       Projection query_value) const override;
    void reset_cursor(Cursor& cursor, std::uint32_t table_id, Projection query_value) const override;
    [[nodiscard]] ScanReport scan(Cursor& cursor, float bound, std::size_t max_entries, void* context,
                                  HitCallback callback, ScanScope scope = ScanScope::quantum) const override;
    [[nodiscard]] const std::string& path() const noexcept;

   private:
    friend class SearchEngine;
    template <typename Visitor>
    ScanReport scan_impl(Cursor& cursor, float bound, std::size_t max_entries,
                         Visitor&& visitor, ScanScope scope) const;
    struct FileHeader;
    struct CachedPageHeader {
        std::uint32_t kind{0};
        std::uint32_t count{0};
        std::uint64_t previous{0};
        std::uint64_t next{0};
    };
    explicit BPlusTreeIndex(int fd, std::string path);
    void load_header();
    void read_page(std::uint64_t page_id, std::vector<std::byte>& page) const;

    int fd_{-1};
    std::string path_;
    IndexMetadata metadata_{};
    std::vector<Coordinate> projection_vectors_;
    std::vector<std::uint64_t> roots_;
    // Validated once at open and reused by cursor/scan hot paths.  The file
    // is immutable, so retaining these tiny headers avoids reparsing a page
    // header every time a scan resumes.
    std::vector<CachedPageHeader> page_headers_;
    // Leaf ranks let a cursor stop when its two outward scans meet without
    // walking the linked list or allocating a per-query visited set.
    std::vector<std::uint64_t> leaf_ranks_;
    std::uint64_t data_offset_{0};
    std::uint64_t page_count_{0};
    const std::byte* mapping_{nullptr};
    std::size_t mapping_size_{0};
};

class SortedArrayIndex final : public ProjectionIndex {
   public:
    using ProjectionIndex::scan;
    static constexpr std::uint32_t kDefaultPageSize = 16U * 1024U;

    /** Build a persistent immutable sorted-array index at path. */
    static void Build(const std::string& path, IndexConfig config, const PointAccessor& accessor,
                      std::uint32_t page_size = kDefaultPageSize);
    static void Build(const std::string& path, IndexConfig config, const PointAccessor& accessor,
                      std::uint32_t page_size, bool overwrite);
    /** Open and validate a sorted-array index produced by Build. */
    static std::shared_ptr<SortedArrayIndex> Open(const std::string& path);

    ~SortedArrayIndex() override;
    SortedArrayIndex(const SortedArrayIndex&) = delete;
    SortedArrayIndex& operator=(const SortedArrayIndex&) = delete;

    [[nodiscard]] const IndexMetadata& metadata() const noexcept override;
    [[nodiscard]] std::span<const Coordinate> projection_vectors() const noexcept override;
    [[nodiscard]] std::unique_ptr<Cursor> make_cursor(std::uint32_t table_id,
                                                       Projection query_value) const override;
    void reset_cursor(Cursor& cursor, std::uint32_t table_id, Projection query_value) const override;
    [[nodiscard]] ScanReport scan(Cursor& cursor, float bound, std::size_t max_entries, void* context,
                                  HitCallback callback, ScanScope scope = ScanScope::quantum) const override;
    [[nodiscard]] const std::string& path() const noexcept;

   private:
    friend class SearchEngine;
    template <typename Visitor>
    ScanReport scan_impl(Cursor& cursor, float bound, std::size_t max_entries,
                         Visitor&& visitor, ScanScope scope) const;
    explicit SortedArrayIndex(int fd, std::string path);
    void load_header();

    int fd_{-1};
    std::string path_;
    IndexMetadata metadata_{};
    std::vector<Coordinate> projection_vectors_;
    std::vector<std::uint64_t> table_offsets_;
    std::uint64_t data_offset_{0};
    std::uint64_t mapping_size_{0};
    const std::byte* mapping_{nullptr};
};

/** Build or open either supported persistent layout through one entry point. */
class PersistentIndex {
   public:
    static void Build(const std::string& path, IndexConfig config, const PointAccessor& accessor,
                      PersistentBuildOptions options = {});
    [[nodiscard]] static std::shared_ptr<ProjectionIndex> Open(const std::string& path);
};

struct Neighbor {
    Distance distance{0.0F};
    PointId point_id{0};
};

enum class TerminationReason : std::uint32_t {
    strategy,
    scan_exhausted,
    invalid_strategy_action,
    step_limit,
};

struct SearchResult {
    std::vector<Neighbor> neighbors;
    TerminationReason reason{TerminationReason::strategy};
    std::size_t evaluated_candidates{0};
    std::size_t projection_hits{0};
    /** All k neighbors returned after normal finish/exhaustion, not a quality guarantee.
     * False for partial results, step limits and invalid strategy actions.
     */
    bool complete{false};
};

struct QueryStart {
    std::uint32_t num_points{0};
    std::uint32_t num_dimensions{0};
    std::uint32_t num_hash_tables{0};
    std::uint32_t k{0};
    Metric metric{Metric::l2};
    float radius{1.0F};
    float radius_growth{2.0F};
    float bucket_width{0.0F};
    std::uint32_t collision_threshold{0};
    std::uint32_t candidate_budget{100};
    float approximation_ratio{2.0F};
};

struct QuerySnapshot {
    float radius{1.0F};
    float bound{0.0F};
    std::uint32_t k{0};
    std::size_t evaluated_candidates{0};
    std::size_t projection_hits{0};
    bool current_round_complete{false};
    bool all_tables_exhausted{false};
    std::span<const Neighbor> neighbors{};
};

enum class EvaluationStatus : std::uint32_t {
    evaluated,
    duplicate,
};

struct EvaluationEvent {
    PointId point_id{0};
    EvaluationStatus status{EvaluationStatus::evaluated};
    bool exact_distance{false};
    Distance distance{0.0F};
};

struct ScanBoundaryEvent {
    ScanReport report{};
    float radius{1.0F};
};

/**
 * Shared resumable projection-table scheduler.
 *
 * A strategy may use this small state machine while retaining its own
 * candidate and termination rules.  It owns the ordinary table queue and
 * exhaustion ledger: a table is requeued after a partial quantum, marked
 * complete after a logical window boundary, and omitted from later radius
 * windows after its cursor is exhausted.  The scheduler has no access to
 * cursors or result storage.
 */
class TableScanSchedule {
   public:
    void start(std::uint32_t table_count);
    [[nodiscard]] bool has_pending() const noexcept { return !pending_tables_.empty(); }
    [[nodiscard]] std::optional<std::uint32_t> next_table() {
        if (pending_tables_.empty()) {
            return std::nullopt;
        }
        const std::uint32_t table = pending_tables_.front();
        pending_tables_.pop_front();
        return table;
    }
    void on_scan_boundary(const ScanBoundaryEvent& event);
    void on_radius_advanced();
    [[nodiscard]] bool round_complete() const noexcept { return completed_tables_ == table_count_; }
    [[nodiscard]] bool all_tables_exhausted() const noexcept {
        return exhausted_tables_ == table_count_;
    }
    [[nodiscard]] std::uint32_t table_count() const noexcept { return table_count_; }

   private:
    std::uint32_t table_count_{0};
    std::size_t exhausted_tables_{0};
    std::size_t completed_tables_{0};
    std::vector<std::uint8_t> table_exhausted_;
    std::vector<std::uint8_t> table_completed_;
    std::deque<std::uint32_t> pending_tables_;
};

enum class StrategyActionKind : std::uint32_t {
    scan,
    evaluate,
    advance_radius,
    finish,
};

struct StrategyAction {
    StrategyActionKind kind{StrategyActionKind::finish};
    std::uint32_t table_id{0};
    PointId point_id{0};
    ScanScope scan_scope{ScanScope::quantum};

    static StrategyAction Scan(std::uint32_t table_id,
                               ScanScope scope = ScanScope::quantum) {
        return {StrategyActionKind::scan, table_id, 0, scope};
    }
    static StrategyAction Evaluate(PointId point_id) {
        return {StrategyActionKind::evaluate, 0, point_id, ScanScope::quantum};
    }
    static StrategyAction AdvanceRadius() {
        return {StrategyActionKind::advance_radius, 0, 0, ScanScope::quantum};
    }
    static StrategyAction Finish() {
        return {StrategyActionKind::finish, 0, 0, ScanScope::quantum};
    }
};

/** One candidate ID, or a default-constructed decision to ignore the hit. */
struct HitDecision {
    // Point counts are uint32_t, so UINT32_MAX is never a valid point ID.
    // A single scalar also avoids partially initialized optional return values
    // and their store-forwarding stalls in per-hit callback ABIs.
    static constexpr PointId kNoCandidate = std::numeric_limits<PointId>::max();
    PointId candidate{kNoCandidate};
    [[nodiscard]] bool has_candidate() const noexcept { return candidate != kNoCandidate; }
};

/**
 * External search-strategy contract.  The engine owns cursor traversal,
 * point access, bounded distance evaluation, deduplication and result
 * maintenance.  A strategy owns all algorithm-specific mutable state.
 */
class SearchStrategy {
   public:
    virtual ~SearchStrategy() = default;
    virtual void start(const QueryStart& query) = 0;
    [[nodiscard]] virtual StrategyAction next(const QuerySnapshot& state) = 0;
    /**
     * Receive one projection hit and optionally return the point to evaluate.
     * The snapshot is current when the callback is entered; all traversal,
     * distance, deduplication and result ownership remains in the engine.
     * Snapshot references and neighbor spans are borrowed for this callback;
     * copy values if they must be retained across callbacks.
     */
    [[nodiscard]] virtual HitDecision on_projection_hit(const ProjectionHit& hit,
                                                         const QuerySnapshot& state) = 0;
    virtual void on_evaluation(const EvaluationEvent& event, const QuerySnapshot& state) = 0;
    virtual void on_scan_boundary(const ScanBoundaryEvent& event, const QuerySnapshot& state) = 0;
    virtual void on_radius_advanced(const QuerySnapshot& state) = 0;
};

struct SearchOptions {
    std::optional<std::uint32_t> num_hash_tables;
    std::uint32_t scan_quantum{0};
    std::size_t max_steps{0};
    bool bounded_distance{true};
};

struct CollisionObservation {
    ProjectionHit hit{};
    std::uint32_t collision_count{0};
};

using CandidateRule = std::function<bool(const CollisionObservation&)>;
using TerminationRule = std::function<bool(const QuerySnapshot&)>;

/** Standard collision-count QALSH strategy with replaceable rules and timing.
 * Configure before search; do not call setters during a running query or from
 * its callbacks. Captured rule state may still evolve during the query.
 */
class DefaultQalshStrategy final : public SearchStrategy {
   public:
    explicit DefaultQalshStrategy(std::uint32_t collision_threshold = 0);
    void set_candidate_rule(CandidateRule rule);
    void set_termination_rule(TerminationRule rule);
    void set_candidate_budget(std::optional<std::size_t> budget);
    void set_check_after_evaluation(bool enabled);
    void set_check_at_round_boundary(bool enabled);

    void start(const QueryStart& query) override;
    [[nodiscard]] StrategyAction next(const QuerySnapshot& state) override;
    [[nodiscard]] HitDecision on_projection_hit(const ProjectionHit& hit,
                                                 const QuerySnapshot&) override {
        return candidate_rule_ ? accept_hit<true>(hit) : accept_hit<false>(hit);
    }

   private:
    template <typename Strategy>
    friend void detail::DeliverRange(detail::RangeExecution&, const detail::HitRange&);

    template <bool CustomRule>
    HitDecision accept_hit(const ProjectionHit& hit) {
        if (finish_requested_) {
            return {};
        }
        if (hit.point_id >= collision_state_.size()) {
            throw std::invalid_argument("projection hit point id is out of range");
        }
        auto& count = collision_state_[hit.point_id];
        if constexpr (CustomRule) {
            if (count == std::numeric_limits<std::uint32_t>::max()) return {};
            if (count == std::numeric_limits<std::uint32_t>::max() - 1U) {
                throw std::invalid_argument("projection collision count overflow");
            }
            ++count;
            if (!candidate_rule_(CollisionObservation{.hit = hit, .collision_count = count})) return {};
            count = std::numeric_limits<std::uint32_t>::max();
        } else {
            // Remaining collisions: zero marks an already accepted point.
            // Counting down avoids reloading a threshold after every store.
            if (count == 0) return {};
            --count;
            if (count != 0) return {};
        }
        return {.candidate = hit.point_id};
    }

    // The built-in collision counter only consumes a hit's point ID.  Keeping
    // this narrow handler separate lets the range driver avoid constructing
    // unused projection fields. Built-in indexes validate IDs at Build/Open;
    // the engine validates external hits before entering this private handler.
    HitDecision accept_builtin_id(PointId point_id) {
        auto& count = collision_state_[point_id];
        // Remaining collisions: zero marks an already accepted point.
        // Counting down avoids reloading a threshold after every store.
        if (count == 0) return {};
        --count;
        if (count != 0) return {};
        return {.candidate = point_id};
    }

   public:
    void on_evaluation(const EvaluationEvent&, const QuerySnapshot& state) override {
        if (check_after_evaluation_ && should_terminate(state)) {
            finish_requested_ = true;
        }
    }
    void on_scan_boundary(const ScanBoundaryEvent& event, const QuerySnapshot& state) override {
        table_schedule_.on_scan_boundary(event);
        if (!finish_requested_ && check_at_round_boundary_ && event.report.round_complete &&
            should_terminate(state)) {
            finish_requested_ = true;
        }
    }
    void on_radius_advanced(const QuerySnapshot&) override {
        radius_advanced_ = true;
        table_schedule_.on_radius_advanced();
    }

   private:
    [[nodiscard]] bool should_terminate(const QuerySnapshot& state) const {
        if (termination_rule_) {
            return termination_rule_(state);
        }
        if (active_candidate_budget_.has_value() &&
            state.evaluated_candidates >= *active_candidate_budget_) {
            return true;
        }
        return state.neighbors.size() >= state.k &&
               state.neighbors.back().distance <= query_.approximation_ratio * state.radius;
    }

   private:
    std::uint32_t collision_threshold_{0};
    std::uint32_t active_collision_threshold_{0};
    CandidateRule candidate_rule_;
    TerminationRule termination_rule_;
    std::optional<std::size_t> candidate_budget_override_;
    bool candidate_budget_override_set_{false};
    std::optional<std::size_t> active_candidate_budget_;
    bool check_after_evaluation_{true};
    bool check_at_round_boundary_{true};
    QueryStart query_{};
    // Standard mode: remaining hits. Custom mode: observed hits/sentinel.
    std::vector<std::uint32_t> collision_state_;
    TableScanSchedule table_schedule_;
    bool radius_advanced_{false};
    bool finish_requested_{false};
};

namespace detail {

// Borrowed storage range. Backends validate extent/window and retain its bytes
// for this call. Decoding via memcpy is valid for mapped or unaligned storage.
struct HitRange {
    const std::byte* first{nullptr};
    std::size_t count{0};
    bool reverse{false};
    std::uint32_t table_id{0};
    Projection query_value{0};
};
struct RangeExecution {
    void* strategy;
    QuerySnapshot state;
    void* context;
    void (*evaluate)(RangeExecution&, PointId);
};
using RangeDriver = void (*)(RangeExecution&, const HitRange&);

template <bool IncludeDifference = true, typename Visitor>
#if defined(__GNUC__) || defined(__clang__)
[[gnu::always_inline]]
#endif
inline void ForEachHit(const HitRange& range, Visitor&& visitor) {
    static_assert(sizeof(Projection) == 4 && sizeof(PointId) == 4);
    // This storage range is immutable for the call, including across user
    // evaluation callbacks. Keep its geometry in values rather than aliases.
    const auto* first = range.first;
    const auto count = range.count;
    const auto table = range.table_id;
    const auto query = range.query_value;
    const auto deliver = [&]<bool Reverse>() {
        for (std::size_t i = 0; i < count; ++i) {
            const auto* bytes = Reverse ? first - i * 8U : first + i * 8U;
            Projection value;
            PointId id;
            std::memcpy(&value, bytes, sizeof(value));
            std::memcpy(&id, bytes + sizeof(value), sizeof(id));
            ProjectionHit hit{table, id, value, query, 0.0F};
            if constexpr (IncludeDifference) {
                hit.absolute_difference = std::abs(query - value);
            }
            if constexpr (std::invocable<Visitor&, const ProjectionHit&, std::size_t>) visitor(hit, i);
            else visitor(hit);
        }
    };
    if (range.reverse) {
        deliver.template operator()<true>();
    } else {
        deliver.template operator()<false>();
    }
}

template <typename Visitor>
#if defined(__GNUC__) || defined(__clang__)
[[gnu::always_inline]]
#endif
inline void ForEachHitId(const HitRange& range, Visitor&& visitor) {
    static_assert(sizeof(Projection) == 4 && sizeof(PointId) == 4);
    const auto* first = range.first;
    const auto count = range.count;
    const auto deliver = [&]<bool Reverse>() {
        for (std::size_t i = 0; i < count; ++i) {
            const auto* bytes = Reverse ? first - i * 8U : first + i * 8U;
            PointId id;
            std::memcpy(&id, bytes + sizeof(Projection), sizeof(id));
            visitor(id, i);
        }
    };
    if (range.reverse) {
        deliver.template operator()<true>();
    } else {
        deliver.template operator()<false>();
    }
}

template <typename Strategy>
void DeliverRange(RangeExecution& execution, const HitRange& range) {
    auto& strategy = *static_cast<Strategy*>(execution.strategy);
    // Local state lets a concrete strategy optimize away unused observations;
    // an observer still sees the current snapshot on every delivered hit.
    const auto run = [&](auto&& accept) {
        // The standard rule observes hits/counts, not a full query snapshot.
        // Keep only its live hit counter in the hot loop; retain full live
        // snapshots for arbitrary strategies.
        auto state = [&] {
            if constexpr (std::same_as<Strategy, DefaultQalshStrategy>) return execution.state.projection_hits;
            else return execution.state;
        }();
        ForEachHit(range, [&](const ProjectionHit& hit, [[maybe_unused]] std::size_t position) {
            if constexpr (!std::same_as<Strategy, DefaultQalshStrategy>) ++state.projection_hits;
            const HitDecision decision = accept(hit, state);
            if (decision.has_candidate()) {
                if constexpr (std::same_as<Strategy, DefaultQalshStrategy>) execution.state.projection_hits = state + position + 1;
                else execution.state = state;
                execution.evaluate(execution, decision.candidate);
                if constexpr (!std::same_as<Strategy, DefaultQalshStrategy>) state = execution.state;
            }
        });
        if constexpr (std::same_as<Strategy, DefaultQalshStrategy>) execution.state.projection_hits = state + range.count;
        else execution.state = state;
    };
    if constexpr (std::same_as<Strategy, DefaultQalshStrategy>) {
        if (strategy.candidate_rule_) {
            run([&](const ProjectionHit& hit, const auto&) { return strategy.template accept_hit<true>(hit); });
        } else {
            // A built-in strategy can only change finish_requested_ while an
            // evaluation callback is running.  Hoist that member read out of
            // the per-hit callback while retaining full range consumption so
            // projection hit accounting and cursor boundaries stay intact.
            auto state = execution.state.projection_hits;
            bool accepting = !strategy.finish_requested_;
            ForEachHitId(range, [&](PointId point_id, std::size_t position) {
                if (!accepting) return;
                const HitDecision decision = strategy.accept_builtin_id(point_id);
                if (decision.has_candidate()) {
                    execution.state.projection_hits = state + position + 1;
                    execution.evaluate(execution, decision.candidate);
                    accepting = !strategy.finish_requested_;
                }
            });
            execution.state.projection_hits = state + range.count;
        }
    } else {
        run([&](const ProjectionHit& hit, const QuerySnapshot& state) { return strategy.on_projection_hit(hit, state); });
    }
}
}  // namespace detail

class SearchEngine {
   public:
    SearchEngine(std::shared_ptr<const ProjectionIndex> index, PointAccessor accessor,
                 SearchOptions options = {});
    [[nodiscard]] SearchResult search(PointView query, std::uint32_t k, SearchStrategy& strategy) const;
    template <std::derived_from<SearchStrategy> Strategy>
    [[nodiscard]] SearchResult search(PointView query, std::uint32_t k, Strategy& strategy) const {
        if constexpr (std::same_as<std::remove_cvref_t<Strategy>, DefaultQalshStrategy>) {
            return search_impl_default(query, k, strategy);
        } else {
            return search_impl(query, k, strategy, &strategy, &detail::DeliverRange<Strategy>);
        }
    }

   private:
    SearchResult search_impl(PointView query, std::uint32_t k, SearchStrategy& strategy,
                             void* strategy_address, detail::RangeDriver driver) const;
    SearchResult search_impl_default(PointView query, std::uint32_t k,
                                     DefaultQalshStrategy& strategy) const;
    template <typename Strategy>
    SearchResult search_impl_typed(PointView query, std::uint32_t k, Strategy& strategy,
                                   void* strategy_address, detail::RangeDriver driver) const;
    std::shared_ptr<const ProjectionIndex> index_;
    PointAccessor accessor_;
    SearchOptions options_{};
};

[[nodiscard]] Distance L1Distance(PointView lhs, PointView rhs);
[[nodiscard]] Distance L2Distance(PointView lhs, PointView rhs);
[[nodiscard]] Distance DistanceValue(PointView lhs, PointView rhs, Metric metric);

/** Return exact=false and infinity when the bound proves the distance larger. */
struct BoundedDistance {
    Distance distance{0.0F};
    bool exact{false};
};
[[nodiscard]] BoundedDistance BoundedDistanceValue(PointView lhs, PointView rhs, Metric metric,
                                                    Distance upper_bound);

[[nodiscard]] const char* ToString(TerminationReason reason) noexcept;
[[nodiscard]] const char* ToString(Metric metric) noexcept;

}  // namespace qalsh
