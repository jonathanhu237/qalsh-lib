#include <qalsh/qalsh.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void Check(bool value) {
    if (!value) throw std::runtime_error("parallel build invariant failed");
}

std::vector<char> Bytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    Check(input.is_open());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    qalsh::IndexConfig config;
    config.num_points = 1000;
    config.num_dimensions = 3;
    config.qalsh.num_hash_tables = 9;
    config.qalsh.collision_threshold = 3;
    config.qalsh.bucket_width = 2;
    const auto owner = std::this_thread::get_id();
    std::array<float, 3> scratch{};
    qalsh::PointAccessor access = [&](qalsh::PointId id) -> qalsh::PointView {
        Check(std::this_thread::get_id() == owner);
        scratch = {static_cast<float>(id % 17), static_cast<float>(id % 31),
                   static_cast<float>(id % 7)};
        return scratch;
    };
    auto serial = qalsh::InMemoryIndex::Build(config, access);
    const auto path1 = (std::filesystem::path(argv[1]) / "serial-build.qalsh").string();
    const auto path4 = (std::filesystem::path(argv[1]) / "parallel-build.qalsh").string();
    qalsh::BPlusTreeIndex::Build(path1, config, access, 512, true);
    config.build_threads = 4;
    auto parallel = qalsh::InMemoryIndex::Build(config, access);
    qalsh::BPlusTreeIndex::Build(path4, config, access, 512, true);
    Check(Bytes(path1) == Bytes(path4));
    qalsh::SearchEngine first(serial, access), second(parallel, access);
    for (unsigned i = 0; i < 20; ++i) {
        const std::array<float, 3> query{static_cast<float>(i % 17) + .1F,
                                       static_cast<float>(i % 31) + .2F,
                                       static_cast<float>(i % 7)};
        qalsh::DefaultQalshStrategy a, b;
        const auto x = first.search(query, 5, a), y = second.search(query, 5, b);
        Check(x.projection_hits == y.projection_hits && x.neighbors.size() == y.neighbors.size());
        for (std::size_t j = 0; j < x.neighbors.size(); ++j) {
            Check(x.neighbors[j].point_id == y.neighbors[j].point_id &&
                  x.neighbors[j].distance == y.neighbors[j].distance);
        }
    }
    for (const auto invalid : {0U, 1025U}) {
        config.build_threads = invalid;
        bool rejected = false;
        try {
            (void)qalsh::InMemoryIndex::Build(config, access);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        Check(rejected);
    }
    std::filesystem::remove(path1);
    std::filesystem::remove(path4);
}
