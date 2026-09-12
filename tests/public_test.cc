#include "qalsh/qalsh.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using qalsh::Coordinate;
using qalsh::PointId;
using qalsh::PointView;

void Check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error("public test failed: " + message);
    }
}

template <typename Callable>
void CheckThrows(Callable&& callable, const std::string& message) {
    bool threw = false;
    try {
        callable();
    } catch (const std::exception&) {
        threw = true;
    }
    Check(threw, message);
}

struct Fixture {
    std::vector<std::vector<Coordinate>> points{{0.0F, 0.0F}, {1.0F, 0.0F},
                                                 {0.0F, 2.0F}, {5.0F, 5.0F}};

    qalsh::PointAccessor accessor() const {
        return [this](PointId id) -> PointView { return points.at(id); };
    }
};

qalsh::IndexConfig Config() {
    qalsh::IndexConfig config;
    config.metric = qalsh::Metric::l2;
    config.num_points = 4;
    config.num_dimensions = 2;
    config.qalsh = qalsh::QalshParameters{.approximation_ratio = 2.0F,
                                          .bucket_width = 10.0F,
                                          .error_probability = 0.1F,
                                          .num_hash_tables = 2,
                                          .collision_threshold = 1,
                                          .candidate_budget = 100,
                                          .initial_radius = 1.0F,
                                          .radius_growth = 2.0F,
                                          .scan_quantum = 3};
    config.projection_vectors = {1.0F, 0.0F, 0.0F, 1.0F};
    return config;
}

class ExhaustiveStrategy final : public qalsh::SearchStrategy {
   public:
    void start(const qalsh::QueryStart& query) override {
        query_ = query;
        table_ = 0;
        requested_all_ = false;
    }
    qalsh::StrategyAction next(const qalsh::QuerySnapshot& state) override {
        if (state.all_tables_exhausted || requested_all_) {
            return qalsh::StrategyAction::Finish();
        }
        return qalsh::StrategyAction::Scan(table_);
    }
    qalsh::HitDecision on_projection_hit(const qalsh::ProjectionHit& hit,
                                          const qalsh::QuerySnapshot&) override {
        return qalsh::HitDecision{.candidate = hit.point_id};
    }
    void on_evaluation(const qalsh::EvaluationEvent&, const qalsh::QuerySnapshot&) override {}
    void on_scan_boundary(const qalsh::ScanBoundaryEvent& event,
                          const qalsh::QuerySnapshot&) override {
        if (!event.report.window_exhausted && !event.report.table_exhausted) {
            return;
        }
        if (event.report.table_id + 1 >= query_.num_hash_tables) {
            requested_all_ = true;
        } else {
            table_ = event.report.table_id + 1;
        }
    }
    void on_radius_advanced(const qalsh::QuerySnapshot&) override {}
   private:
    qalsh::QueryStart query_{};
    std::uint32_t table_{0};
    bool requested_all_{false};
};

class DeferredStrategy final : public qalsh::SearchStrategy {
   public:
    void start(const qalsh::QueryStart& query) override { phase_ = 0; query_ = query; }
    qalsh::StrategyAction next(const qalsh::QuerySnapshot&) override {
        if (phase_ == 0) {
            return qalsh::StrategyAction::Scan(0);
        }
        if (phase_ == 1) {
            phase_ = 2;
            return qalsh::StrategyAction::Evaluate(0);  // duplicate of the hit candidate
        }
        if (phase_ == 2) {
            phase_ = 3;
            return qalsh::StrategyAction::Evaluate(3);  // deferred work after scan exhaustion
        }
        return qalsh::StrategyAction::Finish();
    }
    qalsh::HitDecision on_projection_hit(const qalsh::ProjectionHit& hit,
                                          const qalsh::QuerySnapshot&) override {
        if (hit.point_id == 0) {
            return qalsh::HitDecision{.candidate = 0};
        }
        return {};
    }
    void on_evaluation(const qalsh::EvaluationEvent& event,
                       const qalsh::QuerySnapshot&) override {
        if (event.status == qalsh::EvaluationStatus::duplicate) {
            duplicate_seen_ = true;
        }
    }
    void on_scan_boundary(const qalsh::ScanBoundaryEvent& event,
                          const qalsh::QuerySnapshot&) override {
        if (event.report.window_exhausted || event.report.table_exhausted) {
            phase_ = 1;
        }
    }
    void on_radius_advanced(const qalsh::QuerySnapshot&) override {}
    [[nodiscard]] bool duplicate_seen() const { return duplicate_seen_; }

   private:
    qalsh::QueryStart query_{};
    int phase_{0};
    bool duplicate_seen_{false};
};

void CheckSearch(const std::shared_ptr<const qalsh::ProjectionIndex>& index,
                 const Fixture& fixture) {
    qalsh::SearchEngine engine(index, fixture.accessor());
    ExhaustiveStrategy strategy;
    const std::vector<Coordinate> query{0.1F, 0.1F};
    const qalsh::SearchResult result = engine.search(query, 2, strategy);
    Check(result.neighbors.size() == 2, "exhaustive search returns k neighbors");
    Check(result.neighbors[0].point_id == 0 && result.neighbors[1].point_id == 1,
          "exhaustive neighbors are sorted");
    Check(result.evaluated_candidates == 4, "deduplicated candidate count");
}

void CheckTableSchedule() {
    qalsh::TableScanSchedule schedule;
    schedule.start(2);
    Check(schedule.next_table() == std::optional<std::uint32_t>(0),
          "table scheduler starts with the first table");
    Check(schedule.next_table() == std::optional<std::uint32_t>(1),
          "table scheduler visits every table once");
    Check(!schedule.next_table().has_value(), "table scheduler reports an empty queue");

    schedule.on_scan_boundary(qalsh::ScanBoundaryEvent{
        .report = qalsh::ScanReport{.table_id = 0, .entries_visited = 1}, .radius = 1.0F});
    Check(schedule.next_table() == std::optional<std::uint32_t>(0),
          "partial scan is requeued");
    schedule.on_scan_boundary(qalsh::ScanBoundaryEvent{
        .report = qalsh::ScanReport{.table_id = 0, .window_exhausted = true}, .radius = 1.0F});
    Check(!schedule.round_complete(), "one completed table does not complete a round");
    schedule.on_scan_boundary(qalsh::ScanBoundaryEvent{
        .report = qalsh::ScanReport{.table_id = 1, .table_exhausted = true}, .radius = 1.0F});
    Check(schedule.round_complete() && !schedule.all_tables_exhausted(),
          "window and permanent exhaustion complete the round independently");
    schedule.on_radius_advanced();
    Check(!schedule.round_complete() && !schedule.all_tables_exhausted(),
          "only permanently exhausted tables stay complete after radius growth");
    Check(schedule.next_table() == std::optional<std::uint32_t>(0),
          "non-exhausted tables are requeued after radius growth");
}

void CheckQuantumOne(const std::shared_ptr<const qalsh::ProjectionIndex>& index,
                     const Fixture& fixture) {
    const std::vector<Coordinate> query{0.1F, 0.1F};
    qalsh::SearchEngine checked_engine(
        index, fixture.accessor(), qalsh::SearchOptions{.num_hash_tables = std::nullopt, .scan_quantum = 1});
    ExhaustiveStrategy checked_strategy;
    const qalsh::SearchResult checked = checked_engine.search(query, 2, checked_strategy);
    Check(checked.evaluated_candidates == 4 && checked.neighbors.size() == 2,
          "quantum-one checked scan continues on the right side");

    auto cursor = index->make_cursor(0, 0.1F);
    std::size_t hits = 0;
    const auto callback = [](void* context, const qalsh::ProjectionHit&) {
        ++*static_cast<std::size_t*>(context);
    };
    const qalsh::ScanReport unbounded = index->scan(
        *cursor, 10.0F, std::numeric_limits<std::size_t>::max(), &hits, callback);
    Check(unbounded.entries_visited == 4 && hits == 4,
          "checked scan handles the maximum entry budget without overflow");

    qalsh::SearchEngine fast_engine(
        index, fixture.accessor(), qalsh::SearchOptions{.num_hash_tables = std::nullopt, .scan_quantum = 1});
    ExhaustiveStrategy fast_strategy;
    const qalsh::SearchResult fast = fast_engine.search(query, 2, fast_strategy);
    Check(fast.evaluated_candidates == 4 && fast.neighbors.size() == 2,
          "quantum-one trusted scan continues on the right side");

    qalsh::SearchEngine default_engine(
        index, fixture.accessor(), qalsh::SearchOptions{.num_hash_tables = std::nullopt, .scan_quantum = 1});
    qalsh::DefaultQalshStrategy default_strategy;
    const qalsh::SearchResult default_result = default_engine.search(query, 1, default_strategy);
    Check(default_result.evaluated_candidates > 0 &&
              default_result.reason != qalsh::TerminationReason::step_limit,
          "quantum-one point-ID fast scan makes progress");
}

void CheckDeferredEvaluation(const std::shared_ptr<const qalsh::ProjectionIndex>& index,
                             const Fixture& fixture) {
    qalsh::SearchEngine engine(index, fixture.accessor());
    DeferredStrategy strategy;
    const std::vector<Coordinate> query{0.1F, 0.1F};
    const qalsh::SearchResult result = engine.search(query, 2, strategy);
    Check(strategy.duplicate_seen(), "duplicate deferred evaluation is reported");
    Check(result.evaluated_candidates == 2, "deferred candidates are evaluated once");
    Check(result.neighbors.size() == 2 && result.neighbors[0].point_id == 0 &&
              result.neighbors[1].point_id == 3,
          "deferred candidate appears in top-k");
}

void CheckStrategyControls(const std::shared_ptr<const qalsh::ProjectionIndex>& index,
                           const Fixture& fixture) {
    qalsh::SearchEngine engine(index, fixture.accessor());
    const std::vector<Coordinate> query{0.1F, 0.1F};

    qalsh::DefaultQalshStrategy stop_after_first;
    stop_after_first.set_termination_rule(
        [](const qalsh::QuerySnapshot& state) { return state.evaluated_candidates >= 1; });
    const qalsh::SearchResult stopped = engine.search(query, 1, stop_after_first);
    Check(stopped.evaluated_candidates == 1,
          "candidate-level termination stops the current scan batch");
    Check(stopped.projection_hits > stopped.evaluated_candidates,
          "candidate-level termination still consumes the logical scan range");

    qalsh::IndexConfig replacement_config = Config();
    replacement_config.qalsh.collision_threshold = 0;
    auto replacement_index = qalsh::InMemoryIndex::Build(replacement_config, fixture.accessor());
    qalsh::SearchEngine replacement_engine(replacement_index, fixture.accessor());
    qalsh::DefaultQalshStrategy replacement;
    replacement.set_candidate_rule([](const qalsh::CollisionObservation&) { return true; });
    const qalsh::SearchResult replacement_result = replacement_engine.search(query, 1, replacement);
    Check(replacement_result.evaluated_candidates >= 1,
          "candidate-rule replacement does not require threshold metadata");

    qalsh::DefaultQalshStrategy termination_replacement;
    termination_replacement.set_termination_rule(
        [](const qalsh::QuerySnapshot& state) { return state.evaluated_candidates >= 2; });
    termination_replacement.set_candidate_rule(
        [](const qalsh::CollisionObservation&) { return true; });
    const qalsh::SearchResult termination_result =
        replacement_engine.search(query, 1, termination_replacement);
    Check(termination_result.evaluated_candidates == 2,
          "termination replacement is not preceded by the default budget");
}

void CheckRemainingRegressionFixes(const Fixture& fixture) {
    // The default top-k budget is the paper's base budget plus k - 1, not
    // the base budget as an absolute cap.
    std::vector<std::vector<float>> far_points(200, std::vector<float>{1000.0F});
    qalsh::IndexConfig far_config;
    far_config.metric = qalsh::Metric::l2;
    far_config.num_points = static_cast<std::uint32_t>(far_points.size());
    far_config.num_dimensions = 1;
    far_config.qalsh = qalsh::QalshParameters{.approximation_ratio = 2.0F,
                                               .bucket_width = 2.0F,
                                               .error_probability = 0.1F,
                                               .num_hash_tables = 1,
                                               .collision_threshold = 1,
                                               .candidate_budget = 100,
                                               .initial_radius = 1.0F,
                                               .radius_growth = 2.0F,
                                               .scan_quantum = 128};
    far_config.projection_vectors = {0.0F};
    const auto far_accessor = [&far_points](PointId id) -> PointView { return far_points.at(id); };
    const auto far_index = qalsh::InMemoryIndex::Build(far_config, far_accessor);
    qalsh::SearchEngine far_engine(far_index, far_accessor);
    qalsh::DefaultQalshStrategy far_strategy;
    const std::vector<float> far_query{0.0F};
    const qalsh::SearchResult far_result = far_engine.search(far_query, 101, far_strategy);
    Check(far_result.evaluated_candidates == 200 && far_result.neighbors.size() == 101,
          "default top-k budget includes k - 1 additional candidates");

    qalsh::DefaultQalshStrategy limited_strategy;
    limited_strategy.set_candidate_budget(1);
    const auto limited_result = far_engine.search(far_query, 101, limited_strategy);
    Check(limited_result.neighbors.size() == 1 && !limited_result.complete &&
              limited_result.reason == qalsh::TerminationReason::strategy,
          "normal strategy termination must not label a partial top-k complete");
    Check(far_result.complete && far_result.reason == qalsh::TerminationReason::scan_exhausted,
          "exhaustion with all requested neighbors is a complete result");

    // Query scratch may retain capacity, but must not retain the immutable
    // index itself after the caller releases its last owner.
    std::weak_ptr<const qalsh::ProjectionIndex> weak_index;
    {
        const auto local_index = qalsh::InMemoryIndex::Build(Config(), fixture.accessor());
        weak_index = local_index;
        qalsh::SearchEngine local_engine(local_index, fixture.accessor());
        qalsh::DefaultQalshStrategy local_strategy;
        (void)local_engine.search(std::vector<float>{0.1F, 0.1F}, 1, local_strategy);
    }
    Check(weak_index.expired(), "query scratch does not retain a released index");

    // A root whose separators are individually plausible but whose child
    // subtrees are reversed must be rejected before a query can route through
    // it.
    std::vector<std::vector<float>> ordered_points;
    ordered_points.reserve(61);
    for (std::uint32_t point_id = 0; point_id < 61; ++point_id) {
        ordered_points.push_back({static_cast<float>(point_id)});
    }
    qalsh::IndexConfig routing_config;
    routing_config.metric = qalsh::Metric::l2;
    routing_config.num_points = 61;
    routing_config.num_dimensions = 1;
    routing_config.qalsh = qalsh::QalshParameters{.approximation_ratio = 2.0F,
                                                  .bucket_width = 2.0F,
                                                  .error_probability = 0.1F,
                                                  .num_hash_tables = 1,
                                                  .collision_threshold = 1,
                                                  .candidate_budget = 100,
                                                  .initial_radius = 1.0F,
                                                  .radius_growth = 2.0F,
                                                  .scan_quantum = 128};
    routing_config.projection_vectors = {1.0F};
    const auto routing_accessor = [&ordered_points](PointId id) -> PointView {
        return ordered_points.at(id);
    };
    const std::filesystem::path routing_path =
        std::filesystem::temp_directory_path() / "qalsh-public-misrouted-root.qalsh";
    std::error_code ignored;
    std::filesystem::remove(routing_path, ignored);
    qalsh::BPlusTreeIndex::Build(routing_path.string(), routing_config, routing_accessor, 512, true);
    {
        std::fstream file(routing_path, std::ios::binary | std::ios::in | std::ios::out);
        Check(file.is_open(), "misrouted root fixture opens for mutation");
        std::uint64_t roots_offset = 0;
        std::uint64_t data_offset = 0;
        std::uint64_t root = 0;
        file.seekg(80);
        file.read(reinterpret_cast<char*>(&roots_offset), sizeof(roots_offset));
        file.seekg(96);
        file.read(reinterpret_cast<char*>(&data_offset), sizeof(data_offset));
        file.seekg(static_cast<std::streamoff>(roots_offset));
        file.read(reinterpret_cast<char*>(&root), sizeof(root));
        Check(file.good(), "misrouted root fixture header reads");
        const std::uint64_t children_offset = data_offset + root * 512U + 32U;
        std::uint64_t children[2]{};
        file.seekg(static_cast<std::streamoff>(children_offset));
        file.read(reinterpret_cast<char*>(children), sizeof(children));
        Check(file.good(), "misrouted root fixture child reads");
        std::swap(children[0], children[1]);
        file.seekp(static_cast<std::streamoff>(children_offset));
        file.write(reinterpret_cast<const char*>(children), sizeof(children));
        const float separator = 0.0F;
        file.write(reinterpret_cast<const char*>(&separator), sizeof(separator));
    }
    CheckThrows([&] { (void)qalsh::BPlusTreeIndex::Open(routing_path.string()); },
                "misrouted B+ root is rejected");
    std::filesystem::remove(routing_path, ignored);
}

void CheckTiesAndValidation(const std::shared_ptr<const qalsh::ProjectionIndex>& index,
                            const Fixture& fixture) {
    const std::vector<float> lhs{1.0F, 1.0F};
    const std::vector<float> zero{0.0F, 0.0F};
    const qalsh::Distance tie_distance = qalsh::L2Distance(lhs, zero);
    const qalsh::BoundedDistance bounded =
        qalsh::BoundedDistanceValue(lhs, zero, qalsh::Metric::l2, tie_distance);
    Check(bounded.exact && bounded.distance == tie_distance,
          "equal L2 bound remains exact");

    qalsh::SearchEngine engine(index, fixture.accessor());
    // The fixture's points 1 and 2 are not a tie for this query; use a small
    // separate index with two equal-distance points and reversed projection order.
    Fixture tie_fixture;
    tie_fixture.points = {{1.0F, 1.0F}, {-1.0F, 1.0F}};
    auto tie_config = Config();
    tie_config.num_points = 2;
    tie_config.qalsh.num_hash_tables = 1;
    tie_config.qalsh.collision_threshold = 1;
    // Visit the larger ID first. Both points have sqrt(2) distance, whose
    // rounded square is below 2.0F on common IEEE-754 implementations.
    tie_config.projection_vectors = {1.0F, 0.0F};
    auto tie_index = qalsh::InMemoryIndex::Build(tie_config, tie_fixture.accessor());
    qalsh::SearchEngine tie_engine(tie_index, tie_fixture.accessor());
    ExhaustiveStrategy tie_strategy;
    const std::vector<float> query{0.0F, 0.0F};
    const qalsh::SearchResult tie_result = tie_engine.search(query, 1, tie_strategy);
    Check(tie_result.neighbors.size() == 1 && tie_result.neighbors.front().point_id == 0,
          "smaller ID survives equal-distance pruning");

    CheckThrows([&] { (void)engine.search(std::vector<float>{0.0F}, 1, tie_strategy); },
                "query dimensionality is validated");
    CheckThrows([&] {
        const std::vector<float> bad{std::numeric_limits<float>::quiet_NaN(), 0.0F};
        (void)qalsh::L2Distance(bad, zero);
    }, "non-finite distance inputs are rejected");
}

void CheckEqualProjectionBoundary() {
    // Every point shares the projection key, while only point zero is the
    // nearest neighbour.  Persistent tree descent must choose the leftmost
    // equal key, just like lower_bound in the in-memory and array layouts.
    std::vector<std::array<float, 2>> points;
    for (std::uint32_t point_id = 0; point_id < 128; ++point_id) {
        points.push_back({1.0F, static_cast<float>(point_id)});
    }
    const qalsh::PointAccessor accessor = [&points](qalsh::PointId point_id) -> qalsh::PointView {
        return points.at(point_id);
    };
    qalsh::IndexConfig config;
    config.metric = qalsh::Metric::l2;
    config.num_points = static_cast<std::uint32_t>(points.size());
    config.num_dimensions = 2;
    config.qalsh.num_hash_tables = 1;
    config.qalsh.collision_threshold = 1;
    config.qalsh.candidate_budget = 1;
    config.qalsh.bucket_width = 2.0F;
    config.qalsh.scan_quantum = 2;
    config.projection_vectors = {1.0F, 0.0F};
    const std::vector<float> query{1.0F, 0.0F};

    const auto search = [&](const std::shared_ptr<const qalsh::ProjectionIndex>& index) {
        qalsh::SearchEngine engine(index, accessor);
        qalsh::DefaultQalshStrategy strategy;
        return engine.search(query, 1, strategy);
    };
    const auto memory = search(qalsh::InMemoryIndex::Build(config, accessor));
    Check(memory.neighbors.size() == 1 && memory.neighbors.front().point_id == 0 &&
              memory.neighbors.front().distance == 0.0F && memory.evaluated_candidates == 1,
          "equal projections choose the leftmost nearest point in memory");

    std::error_code ignored;
    const std::array<qalsh::IndexLayout, 2> layouts{
        qalsh::IndexLayout::b_plus_tree, qalsh::IndexLayout::sorted_array};
    for (const qalsh::IndexLayout layout : layouts) {
        const std::string suffix = layout == qalsh::IndexLayout::b_plus_tree ? "bplus" : "array";
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / ("qalsh-public-equal-projection-" + suffix + ".qalsh");
        std::filesystem::remove(path, ignored);
        qalsh::PersistentIndex::Build(
            path.string(), config, accessor,
            qalsh::PersistentBuildOptions{.layout = layout, .page_size = 512, .overwrite = true});
        const auto result = search(qalsh::PersistentIndex::Open(path.string()));
        Check(result.neighbors.size() == 1 && result.neighbors.front().point_id == 0 &&
                  result.neighbors.front().distance == 0.0F && result.evaluated_candidates == 1,
              "equal projections choose the leftmost nearest point in persistent layouts");
        std::filesystem::remove(path, ignored);
    }
}

struct LogicalBoundaryEvent {
    char kind{0};
    PointId point_id{0};
    std::size_t entries_visited{0};
    std::size_t projection_hits{0};
    std::size_t evaluated_candidates{0};
    bool window_exhausted{false};
    bool table_exhausted{false};
    bool round_complete{false};
    bool all_tables_exhausted{false};

    bool operator==(const LogicalBoundaryEvent&) const = default;
};

// This strategy deliberately declines every projection hit.  It therefore
// drives the same public scan-boundary sequence through all physical pages
// and then performs one explicit evaluation after exhaustion.  The event
// trace is the public contract we compare between persistent layouts; it does
// not inspect either backend's cursor or page representation.
class MultiPageBoundaryStrategy final : public qalsh::SearchStrategy {
   public:
    explicit MultiPageBoundaryStrategy(qalsh::ScanScope scope) : scope_(scope) {}

    void start(const qalsh::QueryStart&) override {
        deferred_evaluation_requested_ = false;
        radius_advanced_ = false;
        events.clear();
    }

    qalsh::StrategyAction next(const qalsh::QuerySnapshot& state) override {
        if (!state.all_tables_exhausted) {
            return qalsh::StrategyAction::Scan(0, scope_);
        }
        if (!radius_advanced_) {
            radius_advanced_ = true;
            return qalsh::StrategyAction::AdvanceRadius();
        }
        if (!deferred_evaluation_requested_) {
            deferred_evaluation_requested_ = true;
            return qalsh::StrategyAction::Evaluate(129);
        }
        return qalsh::StrategyAction::Finish();
    }

    qalsh::HitDecision on_projection_hit(const qalsh::ProjectionHit& hit,
                                          const qalsh::QuerySnapshot& state) override {
        events.push_back(LogicalBoundaryEvent{.kind = 'h',
                                               .point_id = hit.point_id,
                                               .projection_hits = state.projection_hits,
                                               .evaluated_candidates = state.evaluated_candidates,
                                               .all_tables_exhausted = state.all_tables_exhausted});
        return {};
    }

    void on_evaluation(const qalsh::EvaluationEvent& event,
                       const qalsh::QuerySnapshot& state) override {
        Check(radius_advanced_, "deferred evaluation follows a radius increase");
        Check(state.all_tables_exhausted,
              "deferred evaluation runs after all persistent scan ranges exhaust");
        events.push_back(LogicalBoundaryEvent{.kind = event.status == qalsh::EvaluationStatus::evaluated
                                                       ? 'e'
                                                       : 'd',
                                               .point_id = event.point_id,
                                               .projection_hits = state.projection_hits,
                                               .evaluated_candidates = state.evaluated_candidates,
                                               .all_tables_exhausted = state.all_tables_exhausted});
    }

    void on_scan_boundary(const qalsh::ScanBoundaryEvent& event,
                          const qalsh::QuerySnapshot& state) override {
        Check(state.projection_hits >= event.report.entries_visited,
              "logical boundary snapshot includes its visited entries");
        events.push_back(LogicalBoundaryEvent{.kind = 'b',
                                               .entries_visited = event.report.entries_visited,
                                               .projection_hits = state.projection_hits,
                                               .evaluated_candidates = state.evaluated_candidates,
                                               .window_exhausted = event.report.window_exhausted,
                                               .table_exhausted = event.report.table_exhausted,
                                               .round_complete = event.report.round_complete,
                                               .all_tables_exhausted = event.report.all_tables_exhausted});
    }

    void on_radius_advanced(const qalsh::QuerySnapshot& state) override {
        events.push_back(LogicalBoundaryEvent{.kind = 'r',
                                               .projection_hits = state.projection_hits,
                                               .evaluated_candidates = state.evaluated_candidates,
                                               .all_tables_exhausted = state.all_tables_exhausted});
    }

    std::vector<LogicalBoundaryEvent> events;

   private:
    qalsh::ScanScope scope_;
    bool deferred_evaluation_requested_{false};
    bool radius_advanced_{false};
};

void CheckMultiPagePersistentBoundaries() {
    std::vector<std::array<float, 2>> points;
    points.reserve(130);
    for (PointId point_id = 0; point_id < 130; ++point_id) {
        // Equal projection keys make the lower-bound side and every physical
        // page/array region observable through the ordered hit sequence.
        points.push_back({0.0F, static_cast<float>(point_id)});
    }
    const qalsh::PointAccessor accessor = [&points](PointId point_id) -> qalsh::PointView {
        return points.at(point_id);
    };
    qalsh::IndexConfig config;
    config.metric = qalsh::Metric::l2;
    config.num_points = static_cast<std::uint32_t>(points.size());
    config.num_dimensions = 2;
    config.qalsh.num_hash_tables = 1;
    config.qalsh.collision_threshold = 1;
    config.qalsh.bucket_width = 2.0F;
    config.qalsh.scan_quantum = 7;
    config.projection_vectors = {1.0F, 0.0F};

    std::error_code ignored;
    const auto base = std::filesystem::temp_directory_path();
    const std::array<std::filesystem::path, 2> paths{
        base / "qalsh-public-boundaries-bplus.qalsh",
        base / "qalsh-public-boundaries-array.qalsh"};
    for (const qalsh::ScanScope scope : {qalsh::ScanScope::quantum, qalsh::ScanScope::range}) {
        std::array<std::vector<LogicalBoundaryEvent>, 2> traces;
        std::array<qalsh::SearchResult, 2> results;
        for (std::size_t layout_id = 0; layout_id < paths.size(); ++layout_id) {
            std::filesystem::remove(paths[layout_id], ignored);
            const auto layout = layout_id == 0 ? qalsh::IndexLayout::b_plus_tree
                                               : qalsh::IndexLayout::sorted_array;
            qalsh::PersistentIndex::Build(
                paths[layout_id].string(), config, accessor,
                qalsh::PersistentBuildOptions{.layout = layout, .page_size = 512, .overwrite = true});
            const auto index = qalsh::PersistentIndex::Open(paths[layout_id].string());
            MultiPageBoundaryStrategy strategy(scope);
            qalsh::SearchEngine engine(index, accessor);
            results[layout_id] = engine.search(std::vector<float>{0.0F, 0.0F}, 1, strategy);
            traces[layout_id] = std::move(strategy.events);
            Check(results[layout_id].projection_hits == points.size() &&
                      results[layout_id].evaluated_candidates == 1 &&
                      results[layout_id].neighbors.size() == 1 &&
                      results[layout_id].neighbors.front().point_id == 129,
                  "multi-page strategy visits all entries before deferred evaluation");
            std::size_t hit_events = 0;
            std::size_t boundary_events = 0;
            bool saw_radius_advance = false;
            bool saw_deferred_evaluation = false;
            for (const auto& event : traces[layout_id]) {
                hit_events += event.kind == 'h';
                boundary_events += event.kind == 'b';
                saw_radius_advance |= event.kind == 'r';
                saw_deferred_evaluation |= event.kind == 'e' && event.all_tables_exhausted;
            }
            Check(hit_events == points.size() && boundary_events >= 3 && saw_radius_advance &&
                      saw_deferred_evaluation,
                  "multi-page public trace covers regions, radius growth, and deferred work");
            std::filesystem::remove(paths[layout_id], ignored);
        }
        Check(traces[0] == traces[1],
              "B+ and sorted-array layouts preserve logical events for each scan scope");
        Check(results[0].projection_hits == results[1].projection_hits &&
                  results[0].evaluated_candidates == results[1].evaluated_candidates &&
                  results[0].reason == results[1].reason && results[0].complete == results[1].complete,
              "B+ and sorted-array layouts preserve search state for each scan scope");
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--open") {
            Fixture fixture;
            const auto disk = qalsh::PersistentIndex::Open(argv[2]);
            CheckSearch(disk, fixture);
            return 0;
        }
        Fixture fixture;
        CheckTableSchedule();
        const qalsh::IndexConfig config = Config();
        const auto memory = qalsh::InMemoryIndex::Build(config, fixture.accessor());
        CheckSearch(memory, fixture);
        CheckQuantumOne(memory, fixture);
        CheckDeferredEvaluation(memory, fixture);
        CheckStrategyControls(memory, fixture);
        CheckTiesAndValidation(memory, fixture);
        CheckEqualProjectionBoundary();
        CheckMultiPagePersistentBoundaries();
        CheckRemainingRegressionFixes(fixture);

        const std::vector<Coordinate> query{0.1F, 0.1F};
        qalsh::SearchEngine concurrent_engine(memory, fixture.accessor());
        auto run_query = [&concurrent_engine, &query]() {
            ExhaustiveStrategy strategy;
            return concurrent_engine.search(query, 1, strategy);
        };
        auto first = std::async(std::launch::async, run_query);
        auto second = std::async(std::launch::async, run_query);
        Check(first.get().neighbors.front().point_id == 0,
              "concurrent query one has independent state");
        Check(second.get().neighbors.front().point_id == 0,
              "concurrent query two has independent state");

        Check(std::abs(qalsh::L1Distance(std::vector<float>{0.0F, 2.0F},
                                         std::vector<float>{1.0F, 0.0F}) -
                        3.0F) < 1e-6F,
              "L1 distance");
        const qalsh::BoundedDistance pruned = qalsh::BoundedDistanceValue(
            std::vector<float>{0.0F, 2.0F}, std::vector<float>{1.0F, 0.0F},
            qalsh::Metric::l1, 1.0F);
        Check(!pruned.exact && std::isinf(pruned.distance), "bounded L1 pruning");

        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "qalsh-public-test.qalsh";
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        qalsh::BPlusTreeIndex::Build(path.string(), config, fixture.accessor(), 512, true);
        const auto disk = qalsh::BPlusTreeIndex::Open(path.string());
        Check(disk->metadata().num_points == config.num_points,
              "persistent metadata point count");
        Check(disk->metadata().qalsh.candidate_budget == config.qalsh.candidate_budget,
              "persistent candidate budget");
        Check(disk->projection_vectors().size() == config.projection_vectors.size(),
              "persistent projection vectors");
        CheckSearch(disk, fixture);
        CheckQuantumOne(disk, fixture);
        const std::string child_command = std::string("\"") + argv[0] +
                                          "\" --open \"" + path.string() + "\"";
        Check(std::system(child_command.c_str()) == 0,
              "persistent index opens in a separate process");
        CheckDeferredEvaluation(disk, fixture);
        CheckStrategyControls(disk, fixture);

        // A no-replace build must leave the existing valid file untouched.
        CheckThrows([&] { qalsh::BPlusTreeIndex::Build(path.string(), config, fixture.accessor(), 512); },
                    "no-replace build rejects an existing destination");
        const auto still_valid = qalsh::BPlusTreeIndex::Open(path.string());
        Check(still_valid->metadata().num_points == config.num_points,
              "failed no-replace build preserves destination");

        std::ofstream truncated(path, std::ios::binary | std::ios::trunc);
        truncated << "bad";
        truncated.close();
        CheckThrows([&] { (void)qalsh::BPlusTreeIndex::Open(path.string()); },
                    "truncated index is rejected");
        std::filesystem::remove(path, ignored);

        // An omitted factory option chooses B+ trees. Both the default and
        // explicit choices reopen through the same public seam and exercise
        // deferred work and strategy controls.
        const std::filesystem::path default_factory_path =
            std::filesystem::temp_directory_path() / "qalsh-public-default-factory.qalsh";
        std::filesystem::remove(default_factory_path, ignored);
        qalsh::PersistentIndex::Build(default_factory_path.string(), config, fixture.accessor());
        const auto default_factory_index = qalsh::PersistentIndex::Open(default_factory_path.string());
        Check(default_factory_index->metadata().layout == qalsh::IndexLayout::b_plus_tree,
              "persistent factory defaults to B+ trees");
        CheckSearch(default_factory_index, fixture);
        CheckDeferredEvaluation(default_factory_index, fixture);
        CheckStrategyControls(default_factory_index, fixture);
        std::filesystem::remove(default_factory_path, ignored);

        // The explicit choices record their layout in the file. Both reopen
        // through the same public ProjectionIndex/SearchEngine seam.
        const std::array<qalsh::IndexLayout, 2> persistent_layouts{
            qalsh::IndexLayout::b_plus_tree, qalsh::IndexLayout::sorted_array};
        for (const qalsh::IndexLayout layout : persistent_layouts) {
            const std::string suffix = layout == qalsh::IndexLayout::b_plus_tree ? "factory-bplus" : "factory-array";
            const std::filesystem::path factory_path =
                std::filesystem::temp_directory_path() / ("qalsh-public-" + suffix + ".qalsh");
            std::filesystem::remove(factory_path, ignored);
            qalsh::PersistentIndex::Build(
                factory_path.string(), config, fixture.accessor(),
                qalsh::PersistentBuildOptions{.layout = layout, .page_size = 512, .overwrite = true});
            const auto factory_index = qalsh::PersistentIndex::Open(factory_path.string());
            Check(factory_index->metadata().layout == layout,
                  "persistent factory reports the recorded layout");
            CheckSearch(factory_index, fixture);
            CheckQuantumOne(factory_index, fixture);
            CheckDeferredEvaluation(factory_index, fixture);
            CheckStrategyControls(factory_index, fixture);
            const std::string factory_child = std::string("\"") + argv[0] +
                                              "\" --open \"" + factory_path.string() + "\"";
            Check(std::system(factory_child.c_str()) == 0,
                  "persistent factory reopens in a separate process");
            CheckThrows([&] {
                qalsh::PersistentIndex::Build(
                    factory_path.string(), config, fixture.accessor(),
                    qalsh::PersistentBuildOptions{.layout = layout, .page_size = 512});
                }, "persistent factory no-replace build rejects an existing destination");
            // Preserve the valid header and truncate the published body so
            // each backend's extent/file-size validator is exercised.
            const std::filesystem::path truncated_path = factory_path.string() + ".truncated";
            std::filesystem::copy_file(factory_path, truncated_path,
                                       std::filesystem::copy_options::overwrite_existing);
            const auto valid_size = std::filesystem::file_size(truncated_path);
            Check(valid_size > 1, "persistent fixture has a non-empty body");
            std::filesystem::resize_file(truncated_path, valid_size - 1);
            CheckThrows([&] { (void)qalsh::PersistentIndex::Open(truncated_path.string()); },
                        "persistent factory rejects a truncated body for each layout");
            std::filesystem::remove(truncated_path, ignored);

            if (layout == qalsh::IndexLayout::sorted_array) {
                // Keep the header and file length valid, but put an impossible
                // point ID in the mapped data.  Array validation must reject
                // this before a cursor can observe it.
                const std::filesystem::path invalid_entry_path = factory_path.string() + ".invalid-entry";
                std::filesystem::copy_file(factory_path, invalid_entry_path,
                                           std::filesystem::copy_options::overwrite_existing);
                std::fstream invalid_entry(invalid_entry_path,
                                            std::ios::binary | std::ios::in | std::ios::out);
                Check(invalid_entry.is_open(), "sorted-array fixture opens for entry mutation");
                std::uint64_t data_offset = 0;
                invalid_entry.seekg(96);
                invalid_entry.read(reinterpret_cast<char*>(&data_offset), sizeof(data_offset));
                Check(invalid_entry.good(), "sorted-array fixture header reads for entry mutation");
                const std::uint32_t impossible_id = config.num_points;
                invalid_entry.seekp(static_cast<std::streamoff>(
                    data_offset + (static_cast<std::uint64_t>(config.num_points) - 1U) * 8U + 4U));
                invalid_entry.write(reinterpret_cast<const char*>(&impossible_id),
                                    sizeof(impossible_id));
                invalid_entry.close();
                CheckThrows([&] { (void)qalsh::PersistentIndex::Open(invalid_entry_path.string()); },
                            "sorted-array factory rejects an invalid mapped entry");
                std::filesystem::remove(invalid_entry_path, ignored);
            }
            std::filesystem::remove(factory_path, ignored);
        }

        Fixture chain_fixture;
        chain_fixture.points.clear();
        for (std::uint32_t point_id = 0; point_id < 130; ++point_id) {
            chain_fixture.points.push_back({static_cast<float>(point_id), 0.0F});
        }
        auto chain_config = Config();
        chain_config.num_points = static_cast<std::uint32_t>(chain_fixture.points.size());
        chain_config.qalsh.num_hash_tables = 1;
        chain_config.projection_vectors = {1.0F, 0.0F};
        const std::filesystem::path chain_path =
            std::filesystem::temp_directory_path() / "qalsh-public-chain-test.qalsh";
        std::filesystem::remove(chain_path, ignored);
        qalsh::BPlusTreeIndex::Build(chain_path.string(), chain_config,
                                      chain_fixture.accessor(), 512, true);
        {
            std::fstream file(chain_path, std::ios::binary | std::ios::in | std::ios::out);
            Check(file.is_open(), "reordered-chain fixture opens for mutation");
            const auto write_link = [&](std::uint64_t page_id, std::uint64_t field_offset,
                                        std::uint64_t value) {
                file.seekp(static_cast<std::streamoff>(512U + page_id * 512U + field_offset));
                file.write(reinterpret_cast<const char*>(&value), sizeof(value));
            };
            // The three leaves are pages 1, 2, and 3. Keep all links
            // reciprocal and covered, but put page 3 before page 2.
            write_link(1, 16, 3);
            write_link(3, 8, 1);
            write_link(3, 16, 2);
            write_link(2, 8, 3);
        }
        CheckThrows([&] { (void)qalsh::BPlusTreeIndex::Open(chain_path.string()); },
                    "reordered leaf chain is rejected");
        std::filesystem::remove(chain_path, ignored);

        const auto other_memory = qalsh::InMemoryIndex::Build(config, fixture.accessor());
        auto foreign_cursor = memory->make_cursor(0, 0.0F);
        CheckThrows([&] { other_memory->reset_cursor(*foreign_cursor, 0, 0.0F); },
                    "cursor ownership is validated");
        auto bad_config = Config();
        bad_config.metric = static_cast<qalsh::Metric>(99);
        CheckThrows([&] { (void)qalsh::InMemoryIndex::Build(bad_config, fixture.accessor()); },
                    "unsupported metric is rejected even with explicit projections");
        bad_config = Config();
        bad_config.projection_vectors[0] = std::numeric_limits<float>::infinity();
        CheckThrows([&] { (void)qalsh::InMemoryIndex::Build(bad_config, fixture.accessor()); },
                    "non-finite projection vectors are rejected");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
