#include <qalsh/qalsh.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {
void Check(bool condition) {
    if (!condition) throw std::runtime_error("dispatch equivalence test failed");
}

// Exercise the ordinary external-index seam, not the built-in range dispatch.
class ForwardIndex final : public qalsh::ProjectionIndex {
public:
    explicit ForwardIndex(std::shared_ptr<const ProjectionIndex> index) : index_(std::move(index)) {}
    const qalsh::IndexMetadata& metadata() const noexcept override { return index_->metadata(); }
    std::span<const float> projection_vectors() const noexcept override { return index_->projection_vectors(); }
    std::unique_ptr<Cursor> make_cursor(std::uint32_t t, float q) const override { return index_->make_cursor(t, q); }
    void reset_cursor(Cursor& c, std::uint32_t t, float q) const override { index_->reset_cursor(c, t, q); }
    qalsh::ScanReport scan(Cursor& c, float b, std::size_t n, void* x, HitCallback f,
                          qalsh::ScanScope scope) const override { return index_->scan(c, b, n, x, f, scope); }
private:
    std::shared_ptr<const ProjectionIndex> index_;
};

// A malformed external index must be rejected before it reaches either the
// standard collision counter or a user strategy that declines evaluations.
class InvalidHitIndex final : public qalsh::ProjectionIndex {
public:
    explicit InvalidHitIndex(std::shared_ptr<const ProjectionIndex> index) : index_(std::move(index)) {}
    const qalsh::IndexMetadata& metadata() const noexcept override { return index_->metadata(); }
    std::span<const float> projection_vectors() const noexcept override { return index_->projection_vectors(); }
    std::unique_ptr<Cursor> make_cursor(std::uint32_t t, float q) const override { return index_->make_cursor(t, q); }
    void reset_cursor(Cursor& c, std::uint32_t t, float q) const override { index_->reset_cursor(c, t, q); }
    qalsh::ScanReport scan(Cursor& c, float b, std::size_t n, void* x, HitCallback f,
                          qalsh::ScanScope scope) const override {
        struct Forward { void* context; HitCallback callback; qalsh::PointId invalid_id; };
        Forward forward{x, f, metadata().num_points};
        return index_->scan(c, b, n, &forward, [](void* context, const qalsh::ProjectionHit& hit) {
            const auto& forward = *static_cast<Forward*>(context);
            auto invalid = hit;
            invalid.point_id = forward.invalid_id;
            forward.callback(forward.context, invalid);
        }, scope);
    }
private:
    std::shared_ptr<const ProjectionIndex> index_;
};

class DeclineStrategy final : public qalsh::SearchStrategy {
public:
    void start(const qalsh::QueryStart&) override {}
    void on_evaluation(const qalsh::EvaluationEvent&, const qalsh::QuerySnapshot&) override {}
    void on_scan_boundary(const qalsh::ScanBoundaryEvent&, const qalsh::QuerySnapshot&) override {}
    void on_radius_advanced(const qalsh::QuerySnapshot&) override {}
    qalsh::StrategyAction next(const qalsh::QuerySnapshot& state) override {
        return state.projection_hits ? qalsh::StrategyAction::Finish() : qalsh::StrategyAction::Scan(0);
    }
    qalsh::HitDecision on_projection_hit(const qalsh::ProjectionHit&, const qalsh::QuerySnapshot&) override {
        return {};
    }
};

void CheckInvalidExternalHits(const std::shared_ptr<const qalsh::ProjectionIndex>& index,
                              const qalsh::PointAccessor& accessor) {
    qalsh::SearchEngine engine(std::make_shared<InvalidHitIndex>(index), accessor);
    float query[2]{0, 0};
    const auto check = [&](auto& strategy) {
        bool rejected = false;
        try { (void)engine.search(query, 1, strategy); }
        catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected);
    };
    qalsh::DefaultQalshStrategy typed, erased;
    check(typed);
    qalsh::SearchStrategy& base = erased;
    check(base);
    DeclineStrategy declined;
    check(declined);
}

// Virtual inheritance also checks that concrete strategy dispatch retains the
// original typed address rather than downcasting a possibly virtual base.
class TraceStrategy final : public virtual qalsh::SearchStrategy {
public:
    using Event = std::tuple<char, std::uint32_t, std::size_t, std::size_t, float, bool, bool,
                             std::vector<std::pair<float, std::uint32_t>>>;
    explicit TraceStrategy(qalsh::ScanScope scope) : scope_(scope) {}
    void start(const qalsh::QueryStart& q) override { tables_ = q.num_hash_tables; }
    qalsh::StrategyAction next(const qalsh::QuerySnapshot& s) override {
        if (s.all_tables_exhausted) {
            if (deferred_++ == 0) return qalsh::StrategyAction::AdvanceRadius();
            if (deferred_ == 2) return qalsh::StrategyAction::Evaluate(3);
            if (deferred_ == 3) return qalsh::StrategyAction::Evaluate(1);
            return qalsh::StrategyAction::Finish();
        }
        if (table_ == tables_) return qalsh::StrategyAction::AdvanceRadius();
        return qalsh::StrategyAction::Scan(table_, scope_);
    }
    qalsh::HitDecision on_projection_hit(const qalsh::ProjectionHit& hit,
                                         const qalsh::QuerySnapshot& s) override {
        Check(s.projection_hits == ++hits_);
        Record('h', hit.point_id, s);
        return hit.point_id % 3 == 0 ? qalsh::HitDecision{hit.point_id} : qalsh::HitDecision{};
    }
    void on_evaluation(const qalsh::EvaluationEvent& e, const qalsh::QuerySnapshot& s) override {
        Check(s.projection_hits == hits_);
        if (e.status == qalsh::EvaluationStatus::evaluated) ++evaluated_;
        Check(s.evaluated_candidates == evaluated_);
        Record(e.status == qalsh::EvaluationStatus::duplicate ? 'd' : 'e', e.point_id, s);
    }
    void on_scan_boundary(const qalsh::ScanBoundaryEvent& e, const qalsh::QuerySnapshot& s) override {
        Check(s.projection_hits == hits_);
        Record('b', e.report.table_id, s);
        if (e.report.window_exhausted || e.report.table_exhausted) ++table_;
    }
    void on_radius_advanced(const qalsh::QuerySnapshot& s) override {
        Check(!s.all_tables_exhausted || s.current_round_complete);
        table_ = 0;
        Record('r', 0, s);
    }
    std::vector<Event> events;
private:
    void Record(char kind, std::uint32_t id, const qalsh::QuerySnapshot& s) {
        std::vector<std::pair<float, std::uint32_t>> neighbors;
        for (auto n : s.neighbors) neighbors.emplace_back(n.distance, n.point_id);
        Check(std::is_sorted(neighbors.begin(), neighbors.end()));
        events.emplace_back(kind, id, s.projection_hits, s.evaluated_candidates, s.radius,
                            s.current_round_complete, s.all_tables_exhausted, std::move(neighbors));
    }
    qalsh::ScanScope scope_;
    std::uint32_t table_{0}, tables_{0}, deferred_{0};
    std::size_t hits_{0}, evaluated_{0};
};

void Run(const std::shared_ptr<const qalsh::ProjectionIndex>& index, const qalsh::PointAccessor& accessor) {
    float query[2]{0, 0};
    for (auto scope : {qalsh::ScanScope::quantum, qalsh::ScanScope::range}) {
        for (auto quantum : {1U, 3U, 128U}) {
            qalsh::SearchOptions options; options.scan_quantum = quantum;
            qalsh::SearchEngine native(index, accessor, options);
            qalsh::SearchEngine external(std::make_shared<ForwardIndex>(index), accessor, options);
            TraceStrategy typed(scope), erased(scope), forwarded(scope);
            const auto a = native.search(query, 7, typed);
            qalsh::SearchStrategy& dynamic = erased;
            const auto b = native.search(query, 7, dynamic);
            const auto c = external.search(query, 7, forwarded);
            Check(typed.events == erased.events && typed.events == forwarded.events);
            Check(a.evaluated_candidates == b.evaluated_candidates && a.evaluated_candidates == c.evaluated_candidates);
            Check(a.projection_hits == b.projection_hits && a.projection_hits == c.projection_hits);
            Check(a.reason == b.reason && a.reason == c.reason);
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    Check(argc == 2);
    const auto path = std::filesystem::path(argv[1]) / "dispatch.qalsh";
    const auto array_path = std::filesystem::path(argv[1]) / "dispatch-array.qalsh";
    std::vector<std::vector<float>> points;
    for (unsigned i = 0; i < 257; ++i) points.push_back({float(int(i % 17) - 8), float(int(i % 11) - 5)});
    qalsh::PointAccessor accessor = [&](qalsh::PointId id) -> qalsh::PointView { return points.at(id); };
    qalsh::IndexConfig config;
    config.num_points = points.size(); config.num_dimensions = 2;
    config.qalsh.num_hash_tables = 3; config.qalsh.collision_threshold = 2;
    config.qalsh.bucket_width = 2; config.projection_vectors = {1,0, 0,1, 1,1};
    const auto memory = qalsh::InMemoryIndex::Build(config, accessor);
    Run(memory, accessor);
    CheckInvalidExternalHits(memory, accessor);
    qalsh::BPlusTreeIndex::Build(path.string(), config, accessor, 512, true);
    Run(qalsh::BPlusTreeIndex::Open(path.string()), accessor);
    qalsh::PersistentIndex::Build(
        array_path.string(), config, accessor,
        qalsh::PersistentBuildOptions{.layout = qalsh::IndexLayout::sorted_array,
                                      .page_size = 512,
                                      .overwrite = true});
    Run(qalsh::PersistentIndex::Open(array_path.string()), accessor);
    std::filesystem::remove(path);
    std::filesystem::remove(array_path);
}
