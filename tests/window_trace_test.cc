#include <qalsh/qalsh.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using qalsh::Coordinate;
using qalsh::PointId;
using qalsh::PointView;
using qalsh::Projection;

void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("window trace test failed: " + message);
}

[[nodiscard]] std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }

struct Entry {
    Projection value{0.0F};
    PointId point_id{0};
};

enum class Backend { memory, bplus, sorted_array };

struct ScanCoverage {
    std::size_t calls{0};
    std::size_t partial_ranges{0};
    std::size_t simd_eligible_forward{0};
    std::size_t simd_eligible_reverse{0};
    std::size_t window_exhausted{0};
    std::size_t table_exhausted{0};
};

// This index is an intentionally independent public scan reference. It owns
// sorted value/id tables and performs a scalar linear window prefix; it never
// includes or calls the production ClampToWindow helper.
class ScalarReferenceIndex final : public qalsh::ProjectionIndex {
   public:
    class Cursor final : public qalsh::ProjectionIndex::Cursor {
       public:
        static constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();
        const ScalarReferenceIndex* owner{nullptr};
        std::uint32_t table_id{0};
        Projection query_value{0.0F};
        std::size_t left{kNoIndex};
        std::size_t right{kNoIndex};
        bool left_blocked{false};
        bool right_blocked{false};
        float last_bound{-std::numeric_limits<float>::infinity()};
    };

    ScalarReferenceIndex(qalsh::IndexConfig config, Backend backend,
                         std::uint32_t page_size)
        : backend_(backend), page_size_(page_size), projection_vectors_(config.projection_vectors) {
        metadata_ = qalsh::IndexMetadata{
            .metric = config.metric,
            .num_points = config.num_points,
            .num_dimensions = config.num_dimensions,
            .num_hash_tables = config.qalsh.num_hash_tables,
            .page_size = backend == Backend::memory ? 0U : page_size,
            .seed = config.seed,
            .file_size = 0,
            .qalsh = config.qalsh,
            .layout = backend == Backend::memory
                          ? qalsh::IndexLayout::in_memory
                          : (backend == Backend::bplus ? qalsh::IndexLayout::b_plus_tree
                                                       : qalsh::IndexLayout::sorted_array)};
        tables_.resize(config.qalsh.num_hash_tables);
    }

    void Add(PointId point_id, PointView point) {
        for (std::uint32_t table = 0; table < metadata_.num_hash_tables; ++table) {
            Projection value = 0.0F;
            const auto* vectors = projection_vectors_.data() + table * metadata_.num_dimensions;
            for (std::size_t dimension = 0; dimension < point.size(); ++dimension) {
                value += point[dimension] * vectors[dimension];
            }
            tables_[table].push_back(Entry{value, point_id});
        }
    }

    void Finish() {
        for (auto& table : tables_) {
            std::sort(table.begin(), table.end(), [](const Entry& lhs, const Entry& rhs) {
                return lhs.value != rhs.value ? lhs.value < rhs.value : lhs.point_id < rhs.point_id;
            });
        }
    }

    [[nodiscard]] const qalsh::IndexMetadata& metadata() const noexcept override { return metadata_; }
    [[nodiscard]] std::span<const Coordinate> projection_vectors() const noexcept override {
        return projection_vectors_;
    }

    [[nodiscard]] const ScanCoverage& coverage() const noexcept { return coverage_; }
    void reset_coverage() const noexcept { coverage_ = {}; }

    [[nodiscard]] std::unique_ptr<qalsh::ProjectionIndex::Cursor>
    make_cursor(std::uint32_t table_id, Projection query_value) const override {
        auto cursor = std::make_unique<Cursor>();
        cursor->owner = this;
        reset_cursor(*cursor, table_id, query_value);
        return cursor;
    }

    void reset_cursor(qalsh::ProjectionIndex::Cursor& base_cursor,
                      std::uint32_t table_id, Projection query_value) const override {
        Check(table_id < tables_.size(), "reference table is out of range");
        auto* cursor = dynamic_cast<Cursor*>(&base_cursor);
        Check(cursor != nullptr && cursor->owner == this, "reference cursor has the wrong owner");
        const auto& entries = tables_[table_id];
        const auto it = std::lower_bound(entries.begin(), entries.end(), query_value,
                                         [](const Entry& entry, Projection value) {
                                             return entry.value < value;
                                         });
        const std::size_t split = static_cast<std::size_t>(it - entries.begin());
        *cursor = Cursor{};
        cursor->owner = this;
        cursor->table_id = table_id;
        cursor->query_value = query_value;
        cursor->left = split == 0U ? Cursor::kNoIndex : split - 1U;
        cursor->right = split == entries.size() ? Cursor::kNoIndex : split;
    }

    [[nodiscard]] qalsh::ScanReport scan(qalsh::ProjectionIndex::Cursor& base_cursor,
                                         float bound, std::size_t max_entries, void* context,
                                         HitCallback callback,
                                         qalsh::ScanScope scope) const override {
        Check(callback != nullptr, "reference callback is missing");
        Check(max_entries > 0U, "reference quantum is zero");
        Check(std::isfinite(bound) && bound >= 0.0F, "reference bound is invalid");
        auto* cursor = dynamic_cast<Cursor*>(&base_cursor);
        Check(cursor != nullptr && cursor->owner == this, "reference cursor has the wrong owner");
        Check(scope == qalsh::ScanScope::quantum || scope == qalsh::ScanScope::range,
              "reference scan scope is invalid");
        if (bound < cursor->last_bound) throw std::invalid_argument("reference bound decreased");
        if (bound > cursor->last_bound) {
            cursor->left_blocked = false;
            cursor->right_blocked = false;
            cursor->last_bound = bound;
        }

        const auto& entries = tables_[cursor->table_id];
        ++coverage_.calls;
        const std::size_t left_limit = scope == qalsh::ScanScope::range
                                           ? max_entries
                                           : max_entries / 2U + max_entries % 2U;
        std::size_t right_limit = scope == qalsh::ScanScope::range ? max_entries : max_entries / 2U;
        std::size_t visited = 0U;

        const auto prefix = [&](std::size_t first, std::size_t count, bool reverse) {
            std::size_t inside = 0U;
            while (inside < count) {
                const std::size_t index = reverse ? first - inside : first + inside;
                if (!(std::abs(cursor->query_value - entries[index].value) <= bound)) break;
                ++inside;
            }
            return inside;
        };
        const auto emit = [&](std::size_t first, std::size_t count, bool reverse) {
            for (std::size_t i = 0; i < count; ++i) {
                const std::size_t index = reverse ? first - i : first + i;
                const Entry& entry = entries[index];
                callback(context, qalsh::ProjectionHit{cursor->table_id, entry.point_id,
                                                        entry.value, cursor->query_value,
                                                        std::abs(cursor->query_value - entry.value)});
            }
        };

        const auto scan_left = [&] {
            std::size_t scanned = 0U;
            while (scanned < left_limit && cursor->left != Cursor::kNoIndex &&
                   !cursor->left_blocked) {
                const std::size_t index = cursor->left;
                std::size_t available = std::min(left_limit - scanned, index + 1U);
                std::size_t region_start = 0U;
                std::size_t region_available = available;
                if (backend_ == Backend::bplus) {
                    const std::size_t capacity = PageCapacity();
                    region_start = (index / capacity) * capacity;
                    region_available = index - region_start + 1U;
                    available = std::min(left_limit - scanned, region_available);
                } else if (backend_ == Backend::sorted_array && scope == qalsh::ScanScope::range) {
                    const std::size_t capacity = PageCapacity();
                    region_start = (index / capacity) * capacity;
                    region_available = index - region_start + 1U;
                    available = std::min(left_limit - scanned, region_available);
                }
                const std::size_t count = prefix(index, available, true);
                if (count > 0U && count < available) {
                    ++coverage_.partial_ranges;
                    if (available >= 6U) ++coverage_.simd_eligible_reverse;
                }
                cursor->left_blocked = count < available;
                if (count != 0U) emit(index, count, true);
                visited += count;
                scanned += count;

                if (backend_ == Backend::bplus && count == available &&
                    available == region_available && index - region_start + 1U == available) {
                    if (region_start == 0U) {
                        cursor->left = Cursor::kNoIndex;
                    } else {
                        cursor->left = region_start - 1U;
                    }
                    if (scope == qalsh::ScanScope::range) break;
                } else if (backend_ == Backend::sorted_array && scope == qalsh::ScanScope::range &&
                           count == available && available == region_available) {
                    cursor->left = region_start == 0U ? Cursor::kNoIndex : region_start - 1U;
                    break;
                } else {
                    cursor->left = count == index + 1U ? Cursor::kNoIndex : index - count;
                }
            }
        };

        const auto scan_right = [&] {
            std::size_t scanned = 0U;
            while (scanned < right_limit && cursor->right != Cursor::kNoIndex &&
                   !cursor->right_blocked) {
                const std::size_t index = cursor->right;
                std::size_t available = std::min(right_limit - scanned, entries.size() - index);
                std::size_t region_end = entries.size();
                std::size_t region_available = available;
                if (backend_ == Backend::bplus) {
                    const std::size_t capacity = PageCapacity();
                    const std::size_t region_start = (index / capacity) * capacity;
                    region_end = std::min(entries.size(), region_start + capacity);
                    region_available = region_end - index;
                    available = std::min(right_limit - scanned, region_available);
                } else if (backend_ == Backend::sorted_array && scope == qalsh::ScanScope::range) {
                    const std::size_t capacity = PageCapacity();
                    const std::size_t region_start = (index / capacity) * capacity;
                    region_end = std::min(entries.size(), region_start + capacity);
                    region_available = region_end - index;
                    available = std::min(right_limit - scanned, region_available);
                }
                const std::size_t count = prefix(index, available, false);
                if (count > 0U && count < available) {
                    ++coverage_.partial_ranges;
                    if (available >= 6U) ++coverage_.simd_eligible_forward;
                }
                cursor->right_blocked = count < available;
                if (count != 0U) emit(index, count, false);
                visited += count;
                scanned += count;
                if (backend_ == Backend::bplus && count == available &&
                    available == region_available) {
                    cursor->right = region_end == entries.size() ? Cursor::kNoIndex : region_end;
                    if (scope == qalsh::ScanScope::range) break;
                } else if (backend_ == Backend::sorted_array && scope == qalsh::ScanScope::range &&
                           count == available && available == region_available) {
                    cursor->right = region_end == entries.size() ? Cursor::kNoIndex : region_end;
                    break;
                } else {
                    cursor->right = index + count == entries.size() ? Cursor::kNoIndex : index + count;
                }
            }
        };

        if (cursor->left != Cursor::kNoIndex && !cursor->left_blocked) scan_left();
        if (cursor->left != Cursor::kNoIndex || cursor->right != Cursor::kNoIndex) {
            if (scope == qalsh::ScanScope::quantum && right_limit == 0U) {
                right_limit = max_entries - visited;
            }
            scan_right();
        }
        const bool window_exhausted = (cursor->left == Cursor::kNoIndex || cursor->left_blocked) &&
                                       (cursor->right == Cursor::kNoIndex || cursor->right_blocked);
        const bool table_exhausted = cursor->left == Cursor::kNoIndex &&
                                     cursor->right == Cursor::kNoIndex;
        if (window_exhausted) ++coverage_.window_exhausted;
        if (table_exhausted) ++coverage_.table_exhausted;
        return qalsh::ScanReport{
            .table_id = cursor->table_id,
            .entries_visited = visited,
            .window_exhausted = window_exhausted,
            .table_exhausted = table_exhausted,
        };
    }

   private:
    [[nodiscard]] std::size_t PageCapacity() const {
        return (static_cast<std::size_t>(page_size_) - 32U) / 8U;
    }

    Backend backend_;
    std::uint32_t page_size_;
    qalsh::IndexMetadata metadata_{};
    std::vector<Coordinate> projection_vectors_;
    std::vector<std::vector<Entry>> tables_;
    mutable ScanCoverage coverage_{};
};

class ForwardIndex final : public qalsh::ProjectionIndex {
   public:
    explicit ForwardIndex(std::shared_ptr<const qalsh::ProjectionIndex> index)
        : index_(std::move(index)) {}
    [[nodiscard]] const qalsh::IndexMetadata& metadata() const noexcept override {
        return index_->metadata();
    }
    [[nodiscard]] std::span<const Coordinate> projection_vectors() const noexcept override {
        return index_->projection_vectors();
    }
    [[nodiscard]] std::unique_ptr<Cursor> make_cursor(std::uint32_t table_id,
                                                       Projection query_value) const override {
        return index_->make_cursor(table_id, query_value);
    }
    void reset_cursor(Cursor& cursor, std::uint32_t table_id,
                      Projection query_value) const override {
        index_->reset_cursor(cursor, table_id, query_value);
    }
    [[nodiscard]] qalsh::ScanReport scan(Cursor& cursor, float bound, std::size_t max_entries,
                                         void* context, HitCallback callback,
                                         qalsh::ScanScope scope) const override {
        return index_->scan(cursor, bound, max_entries, context, callback, scope);
    }

   private:
    std::shared_ptr<const qalsh::ProjectionIndex> index_;
};

struct HitRecord {
    PointId point_id{0};
    Projection value{0.0F};
    Projection query{0.0F};
    float difference{0.0F};
};

struct ScanRecord {
    qalsh::ScanReport report{};
    std::vector<HitRecord> hits;
};

ScanRecord OneScan(const qalsh::ProjectionIndex& index,
                   qalsh::ProjectionIndex::Cursor& cursor, float bound,
                   std::size_t max_entries, qalsh::ScanScope scope) {
    ScanRecord output;
    output.report = index.scan(
        cursor, bound, max_entries, &output,
        [](void* context, const qalsh::ProjectionHit& hit) {
            auto& output = *static_cast<ScanRecord*>(context);
            output.hits.push_back(HitRecord{hit.point_id, hit.projected_value,
                                            hit.query_value, hit.absolute_difference});
        },
        scope);
    return output;
}

void CompareScanRecords(const ScanRecord& expected, const ScanRecord& actual,
                        const std::string& label) {
    Check(expected.report.table_id == actual.report.table_id &&
              expected.report.entries_visited == actual.report.entries_visited &&
              expected.report.window_exhausted == actual.report.window_exhausted &&
              expected.report.table_exhausted == actual.report.table_exhausted,
          label + " scan report differs");
    Check(expected.hits.size() == actual.hits.size(), label + " hit count differs");
    for (std::size_t i = 0; i < expected.hits.size(); ++i) {
        const auto& lhs = expected.hits[i];
        const auto& rhs = actual.hits[i];
        Check(lhs.point_id == rhs.point_id && Bits(lhs.value) == Bits(rhs.value) &&
                  Bits(lhs.query) == Bits(rhs.query) && Bits(lhs.difference) == Bits(rhs.difference),
              label + " hit differs at position " + std::to_string(i));
    }
}

void ComparePublicScans(const std::shared_ptr<const qalsh::ProjectionIndex>& actual,
                        const ScalarReferenceIndex& reference,
                        const std::vector<Projection>& query_projections,
                        const std::string& label) {
    for (qalsh::ScanScope scope : {qalsh::ScanScope::quantum, qalsh::ScanScope::range}) {
        for (std::uint32_t table = 0; table < query_projections.size(); ++table) {
            auto actual_cursor = actual->make_cursor(table, query_projections[table]);
            auto reference_cursor = reference.make_cursor(table, query_projections[table]);
            for (float bound : {0.0F, 0.25F, 0.75F, 2.5F, 100.0F}) {
                for (unsigned call = 0; call < 16U; ++call) {
                    const auto expected = OneScan(reference, *reference_cursor, bound, 7U, scope);
                    const auto observed = OneScan(*actual, *actual_cursor, bound, 7U, scope);
                    CompareScanRecords(expected, observed,
                                       label + " table=" + std::to_string(table) +
                                           " scope=" + std::to_string(static_cast<unsigned>(scope)) +
                                           " bound=" + std::to_string(bound));
                    if (expected.report.window_exhausted || expected.report.table_exhausted) break;
                }
            }
        }
    }
    const auto& coverage = reference.coverage();
    Check(coverage.partial_ranges > 0U, label + " did not exercise a partial window");
    Check(coverage.simd_eligible_forward > 0U,
          label + " did not exercise a forward SIMD-sized partial range");
    Check(coverage.simd_eligible_reverse > 0U,
          label + " did not exercise a reverse SIMD-sized partial range");
    std::cout << "public_scan " << label << " calls " << coverage.calls
              << " partial " << coverage.partial_ranges
              << " simd_eligible_forward " << coverage.simd_eligible_forward
              << " simd_eligible_reverse " << coverage.simd_eligible_reverse
              << " window_exhausted " << coverage.window_exhausted
              << " table_exhausted " << coverage.table_exhausted << '\n';
}

class TraceStrategy final : public qalsh::SearchStrategy {
   public:
    explicit TraceStrategy(qalsh::ScanScope scope, bool exhaustive = false)
        : scope_(scope), exhaustive_(exhaustive) {}

    void start(const qalsh::QueryStart& query) override {
        table_ = 0U;
        table_count_ = query.num_hash_tables;
        round_done_ = false;
        radius_rounds_ = 0U;
        deferred_ = false;
        deferred_requested_ = false;
        saw_partial_boundary_ = false;
        saw_long_boundary_ = false;
        boundary_count_ = 0U;
        partial_boundary_count_ = 0U;
        saw_window_exhausted_ = false;
        saw_table_exhausted_ = false;
        saw_deferred_evaluation_ = false;
        Record('s', 0U, query.radius, query.bucket_width, query.k, 0U, 0U, false, false, {});
    }

    [[nodiscard]] qalsh::StrategyAction next(const qalsh::QuerySnapshot& state) override {
        if (state.all_tables_exhausted) {
            if (!deferred_) {
                deferred_ = true;
                deferred_requested_ = true;
                return qalsh::StrategyAction::Evaluate(0U);
            }
            return qalsh::StrategyAction::Finish();
        }
        if (round_done_) {
            if (exhaustive_ || radius_rounds_ < 4U) return qalsh::StrategyAction::AdvanceRadius();
            return qalsh::StrategyAction::Finish();
        }
        return qalsh::StrategyAction::Scan(table_, scope_);
    }

    [[nodiscard]] qalsh::HitDecision on_projection_hit(const qalsh::ProjectionHit& hit,
                                                         const qalsh::QuerySnapshot& state) override {
        Record('h', hit.point_id, state.radius, state.bound, state.k,
               state.projection_hits, state.evaluated_candidates,
               state.current_round_complete, state.all_tables_exhausted,
               {{Bits(hit.projected_value), hit.point_id}});
        return hit.point_id % 5U == 0U ? qalsh::HitDecision{hit.point_id} : qalsh::HitDecision{};
    }

    void on_evaluation(const qalsh::EvaluationEvent& event,
                       const qalsh::QuerySnapshot& state) override {
        if (deferred_requested_ && state.all_tables_exhausted) {
            saw_deferred_evaluation_ = true;
        }
        Record(event.status == qalsh::EvaluationStatus::evaluated ? 'e' : 'd', event.point_id,
               state.radius, state.bound, state.k, state.projection_hits,
               state.evaluated_candidates, state.current_round_complete,
               state.all_tables_exhausted, {{Bits(event.distance), event.exact_distance ? 1U : 0U}});
    }

    void on_scan_boundary(const qalsh::ScanBoundaryEvent& event,
                          const qalsh::QuerySnapshot& state) override {
        const auto& report = event.report;
        ++boundary_count_;
        saw_long_boundary_ |= report.entries_visited >= 6U;
        if (!report.window_exhausted && !report.table_exhausted) ++partial_boundary_count_;
        saw_partial_boundary_ |= !report.window_exhausted && !report.table_exhausted;
        saw_window_exhausted_ |= report.window_exhausted;
        saw_table_exhausted_ |= report.table_exhausted;
        Record('b', report.table_id, state.radius, event.radius, state.k,
               state.projection_hits, state.evaluated_candidates,
               report.round_complete, report.all_tables_exhausted,
               {{report.entries_visited, report.window_exhausted ? 1U : 0U},
                {report.table_exhausted, report.round_complete ? 1U : 0U}});
        if (report.window_exhausted || report.table_exhausted) {
            if (table_ + 1U < table_count_) {
                ++table_;
            } else {
                round_done_ = true;
            }
        }
    }

    void on_radius_advanced(const qalsh::QuerySnapshot& state) override {
        table_ = 0U;
        round_done_ = false;
        ++radius_rounds_;
        Record('r', 0U, state.radius, state.bound, state.k,
               state.projection_hits, state.evaluated_candidates,
               state.current_round_complete, state.all_tables_exhausted, {});
    }

    [[nodiscard]] const std::vector<std::string>& events() const { return events_; }
    [[nodiscard]] bool saw_partial_boundary() const noexcept { return saw_partial_boundary_; }
    [[nodiscard]] bool saw_long_boundary() const noexcept { return saw_long_boundary_; }
    [[nodiscard]] std::size_t boundary_count() const noexcept { return boundary_count_; }
    [[nodiscard]] std::size_t partial_boundary_count() const noexcept {
        return partial_boundary_count_;
    }
    [[nodiscard]] bool saw_window_exhausted() const noexcept { return saw_window_exhausted_; }
    [[nodiscard]] bool saw_table_exhausted() const noexcept { return saw_table_exhausted_; }
    [[nodiscard]] bool saw_deferred_evaluation() const noexcept {
        return saw_deferred_evaluation_;
    }

   private:
    using Pair = std::pair<std::uint32_t, std::uint32_t>;

    void Record(char kind, std::uint32_t id, float radius, float bound, std::uint32_t k,
                std::size_t hits, std::size_t evaluated, bool round_complete,
                bool all_exhausted, std::vector<Pair> payload) {
        std::ostringstream output;
        output << kind << ':' << id << ':' << Bits(radius) << ':' << Bits(bound) << ':' << k << ':'
               << hits << ':' << evaluated << ':' << round_complete << ':' << all_exhausted;
        for (const auto [first, second] : payload) output << ':' << first << ',' << second;
        events_.push_back(output.str());
    }

    qalsh::ScanScope scope_;
    bool exhaustive_{false};
    std::uint32_t table_{0};
    std::uint32_t table_count_{0};
    bool round_done_{false};
    unsigned radius_rounds_{0U};
    bool deferred_{false};
    bool deferred_requested_{false};
    bool saw_partial_boundary_{false};
    bool saw_long_boundary_{false};
    std::size_t boundary_count_{0U};
    std::size_t partial_boundary_count_{0U};
    bool saw_window_exhausted_{false};
    bool saw_table_exhausted_{false};
    bool saw_deferred_evaluation_{false};
    std::vector<std::string> events_;
};

struct Fixture {
    qalsh::IndexConfig config;
    std::vector<std::vector<Coordinate>> points;
    std::vector<Projection> query_projections;
    std::vector<Coordinate> query;

    Fixture() : points(97U), query{0.25F, -0.75F} {
        config.num_points = points.size();
        config.num_dimensions = 2U;
        config.qalsh.num_hash_tables = 3U;
        config.qalsh.collision_threshold = 2U;
        config.qalsh.bucket_width = 2.0F;
        config.qalsh.initial_radius = 0.5F;
        config.qalsh.radius_growth = 2.0F;
        config.qalsh.scan_quantum = 15U;
        config.projection_vectors = {1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F};
        for (std::size_t i = 0; i < points.size(); ++i) {
            points[i] = {static_cast<float>(static_cast<int>(i % 23U) - 11),
                         static_cast<float>(static_cast<int>((i * 7U) % 19U) - 9)};
        }
        query_projections = {query[0], query[1], query[0] + query[1]};
    }

    [[nodiscard]] qalsh::PointAccessor accessor() const {
        return [this](PointId id) -> PointView { return points.at(id); };
    }
};

void CompareSearchTraces(const std::shared_ptr<const qalsh::ProjectionIndex>& actual,
                         const std::shared_ptr<const qalsh::ProjectionIndex>& forwarded,
                         const std::shared_ptr<const ScalarReferenceIndex>& reference,
                         const Fixture& fixture, qalsh::ScanScope scope,
                         const std::string& label, bool exhaustive = false) {
    reference->reset_coverage();
    qalsh::SearchOptions options;
    options.scan_quantum = 15U;
    const qalsh::SearchEngine actual_engine(actual, fixture.accessor(), options);
    const qalsh::SearchEngine forwarded_engine(forwarded, fixture.accessor(), options);
    const qalsh::SearchEngine reference_engine(reference, fixture.accessor(), options);

    TraceStrategy actual_strategy(scope, exhaustive), erased_strategy(scope, exhaustive),
        forwarded_strategy(scope, exhaustive), reference_strategy(scope, exhaustive);
    const auto actual_result = actual_engine.search(fixture.query, 7U, actual_strategy);
    qalsh::SearchStrategy& erased = erased_strategy;
    const auto erased_result = actual_engine.search(fixture.query, 7U, erased);
    const auto forwarded_result = forwarded_engine.search(fixture.query, 7U, forwarded_strategy);
    const auto reference_result = reference_engine.search(fixture.query, 7U, reference_strategy);

    Check(actual_strategy.events() == erased_strategy.events(), label + " typed/erased trace differs");
    Check(actual_strategy.events() == forwarded_strategy.events(), label + " forwarded trace differs");
    Check(actual_strategy.events() == reference_strategy.events(), label + " scalar trace differs");
    const auto compare_result = [&](const qalsh::SearchResult& lhs,
                                    const qalsh::SearchResult& rhs, const std::string& suffix) {
        Check(lhs.reason == rhs.reason && lhs.complete == rhs.complete &&
                  lhs.evaluated_candidates == rhs.evaluated_candidates &&
                  lhs.projection_hits == rhs.projection_hits &&
                  lhs.neighbors.size() == rhs.neighbors.size(),
              label + suffix + " result metadata differs");
        for (std::size_t i = 0; i < lhs.neighbors.size(); ++i) {
            Check(lhs.neighbors[i].point_id == rhs.neighbors[i].point_id &&
                      Bits(lhs.neighbors[i].distance) == Bits(rhs.neighbors[i].distance),
                  label + suffix + " neighbor differs");
        }
    };
    compare_result(actual_result, erased_result, " erased");
    compare_result(actual_result, forwarded_result, " forwarded");
    compare_result(actual_result, reference_result, " scalar");

    if (scope == qalsh::ScanScope::quantum) {
        Check(actual_strategy.saw_partial_boundary(),
              label + " did not report a partial boundary (boundaries=" +
                  std::to_string(actual_strategy.boundary_count()) + ", partial=" +
                  std::to_string(actual_strategy.partial_boundary_count()) + ")");
        Check(reference_strategy.saw_partial_boundary(), label + " scalar trace missed partial boundary");
    }
    Check(actual_strategy.saw_window_exhausted(), label + " did not report window exhaustion");
    Check(actual_strategy.saw_long_boundary(), label + " did not deliver a long scan boundary");
    const auto& coverage = reference->coverage();
    Check(coverage.simd_eligible_forward > 0U,
          label + " scalar trace did not enter a forward SIMD-sized range");
    Check(coverage.simd_eligible_reverse > 0U,
          label + " scalar trace did not enter a reverse SIMD-sized range");
    if (exhaustive) {
        Check(actual_strategy.saw_table_exhausted(), label + " missed table exhaustion");
        Check(actual_strategy.saw_deferred_evaluation(), label + " missed deferred evaluation");
        Check(reference_strategy.saw_table_exhausted(), label + " scalar trace missed exhaustion");
        Check(reference_strategy.saw_deferred_evaluation(), label + " scalar trace missed deferred evaluation");
    }
    std::cout << "strategy_trace " << label << " events " << actual_strategy.events().size()
              << " reference_calls " << coverage.calls
              << " partial_ranges " << coverage.partial_ranges
              << " simd_eligible_forward " << coverage.simd_eligible_forward
              << " simd_eligible_reverse " << coverage.simd_eligible_reverse
              << " partial_boundary " << actual_strategy.partial_boundary_count()
              << " table_exhausted " << actual_strategy.saw_table_exhausted()
              << " deferred_evaluation " << actual_strategy.saw_deferred_evaluation() << '\n';
}

struct TempDirectory {
    std::filesystem::path path;

    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

[[nodiscard]] TempDirectory MakeTempDirectory() {
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0; attempt < 100U; ++attempt) {
        const auto path = std::filesystem::temp_directory_path() /
                          ("qalsh-window-trace-" + std::to_string(stamp) + "-" +
                           std::to_string(attempt));
        std::error_code error;
        if (std::filesystem::create_directory(path, error) && !error) {
            return TempDirectory{path};
        }
    }
    throw std::runtime_error("unable to create a unique window-trace directory");
}

}  // namespace

int main() {
    Fixture fixture;
    const auto accessor = fixture.accessor();
    auto memory = qalsh::InMemoryIndex::Build(fixture.config, accessor);

    ScalarReferenceIndex memory_reference(fixture.config, Backend::memory, 0U);
    for (PointId id = 0; id < fixture.points.size(); ++id) {
        memory_reference.Add(id, fixture.points[id]);
    }
    memory_reference.Finish();
    ComparePublicScans(memory, memory_reference, fixture.query_projections, "memory");
    CompareSearchTraces(memory, std::make_shared<ForwardIndex>(memory),
                        std::make_shared<ScalarReferenceIndex>(memory_reference), fixture,
                        qalsh::ScanScope::quantum, "memory quantum");
    CompareSearchTraces(memory, std::make_shared<ForwardIndex>(memory),
                        std::make_shared<ScalarReferenceIndex>(memory_reference), fixture,
                        qalsh::ScanScope::range, "memory range");
    CompareSearchTraces(memory, std::make_shared<ForwardIndex>(memory),
                        std::make_shared<ScalarReferenceIndex>(memory_reference), fixture,
                        qalsh::ScanScope::quantum, "memory quantum exhaustive", true);
    CompareSearchTraces(memory, std::make_shared<ForwardIndex>(memory),
                        std::make_shared<ScalarReferenceIndex>(memory_reference), fixture,
                        qalsh::ScanScope::range, "memory range exhaustive", true);

    const TempDirectory temporary = MakeTempDirectory();
    const auto bplus_path = temporary.path / "bplus.qalsh";
    const auto array_path = temporary.path / "array.qalsh";
    qalsh::BPlusTreeIndex::Build(bplus_path.string(), fixture.config, accessor, 512U, true);
    qalsh::SortedArrayIndex::Build(array_path.string(), fixture.config, accessor, 512U, true);
    auto bplus = qalsh::BPlusTreeIndex::Open(bplus_path.string());
    auto array = qalsh::SortedArrayIndex::Open(array_path.string());

    auto bplus_reference = std::make_shared<ScalarReferenceIndex>(fixture.config, Backend::bplus, 512U);
    auto array_reference = std::make_shared<ScalarReferenceIndex>(fixture.config, Backend::sorted_array, 512U);
    for (PointId id = 0; id < fixture.points.size(); ++id) {
        bplus_reference->Add(id, fixture.points[id]);
        array_reference->Add(id, fixture.points[id]);
    }
    bplus_reference->Finish();
    array_reference->Finish();
    ComparePublicScans(bplus, *bplus_reference, fixture.query_projections, "bplus");
    ComparePublicScans(array, *array_reference, fixture.query_projections, "array");
    for (qalsh::ScanScope scope : {qalsh::ScanScope::quantum, qalsh::ScanScope::range}) {
        CompareSearchTraces(bplus, std::make_shared<ForwardIndex>(bplus), bplus_reference,
                            fixture, scope, "bplus");
        CompareSearchTraces(array, std::make_shared<ForwardIndex>(array), array_reference,
                            fixture, scope, "array");
        CompareSearchTraces(bplus, std::make_shared<ForwardIndex>(bplus), bplus_reference,
                            fixture, scope, "bplus exhaustive", true);
        CompareSearchTraces(array, std::make_shared<ForwardIndex>(array), array_reference,
                            fixture, scope, "array exhaustive", true);
    }
    std::filesystem::remove(bplus_path);
    std::filesystem::remove(array_path);
}
