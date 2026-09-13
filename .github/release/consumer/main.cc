#include <qalsh/qalsh.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

}  // namespace

int main() {
    using qalsh::Coordinate;
    using qalsh::PointId;
    using qalsh::PointView;

    const std::vector<std::vector<Coordinate>> points{{0.0F, 0.0F},
                                                       {3.0F, 4.0F},
                                                       {8.0F, 8.0F}};
    const qalsh::PointAccessor accessor = [&points](PointId id) -> PointView {
        return points.at(id);
    };

    qalsh::IndexConfig config;
    config.metric = qalsh::Metric::l2;
    config.num_points = static_cast<std::uint32_t>(points.size());
    config.num_dimensions = 2;
    config.qalsh = qalsh::QalshParameters{
        .approximation_ratio = 2.0F,
        .bucket_width = 10.0F,
        .error_probability = 0.1F,
        .num_hash_tables = 2,
        .collision_threshold = 1,
        .candidate_budget = 3,
        .initial_radius = 1.0F,
        .radius_growth = 2.0F,
        .scan_quantum = 16,
    };
    // Explicit vectors make this smoke test deterministic while still using
    // the public projection-index and search APIs from the installed package.
    config.projection_vectors = {1.0F, 0.0F, 0.0F, 1.0F};

    const auto index = qalsh::InMemoryIndex::Build(config, accessor);
    qalsh::SearchEngine engine(index, accessor);
    qalsh::DefaultQalshStrategy strategy;
    const std::array<Coordinate, 2> query{0.1F, 0.2F};
    const qalsh::SearchResult result = engine.search(query, 1, strategy);

    Check(result.complete, "installed consumer query did not complete");
    Check(result.neighbors.size() == 1, "installed consumer returned the wrong k");
    Check(result.neighbors.front().point_id == 0, "installed consumer chose the wrong neighbor");
    Check(std::abs(result.neighbors.front().distance - std::sqrt(0.05F)) < 1.0e-5F,
          "installed consumer returned the wrong distance");
    std::cout << "installed qalsh query passed\n";
    return 0;
}
