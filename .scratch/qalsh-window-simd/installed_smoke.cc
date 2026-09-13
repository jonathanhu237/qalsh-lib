#include <qalsh/qalsh.h>
#include <array>
int main() {
    qalsh::IndexConfig config;
    config.num_points=4; config.num_dimensions=2; config.qalsh.num_hash_tables=3; config.qalsh.collision_threshold=1; config.qalsh.bucket_width=2.0F;
    std::array<std::array<float,2>,4> points{{{0,0},{1,1},{2,2},{3,3}}};
    auto built=qalsh::InMemoryIndex::Build(config,[&](qalsh::PointId id)->qalsh::PointView{return points[id];});
    qalsh::SearchEngine engine(built,[&](qalsh::PointId id)->qalsh::PointView{return points[id];});
    qalsh::DefaultQalshStrategy strategy;
    return engine.search(std::array<float,2>{0,0},1,strategy).neighbors.empty();
}
