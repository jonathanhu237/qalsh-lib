#include "ann_searcher.h"
#include "config.h"
#include "point_set.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <omp.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::uint64_t HashResults(const std::vector<AnnResult>& results) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const AnnResult& result : results) {
        std::uint32_t distance_bits = 0U;
        static_assert(sizeof(distance_bits) == sizeof(result.distance));
        std::memcpy(&distance_bits, &result.distance, sizeof(distance_bits));
        hash ^= static_cast<std::uint64_t>(result.point_id);
        hash *= 1099511628211ULL;
        hash ^= static_cast<std::uint64_t>(distance_bits);
        hash *= 1099511628211ULL;
    }
    return hash;
}

void RunPopulation(const std::vector<Point>& queries, std::vector<std::unique_ptr<AnnSearcher>>& workers,
                   std::vector<AnnResult>& results) {
    const int thread_count = omp_get_max_threads();
#pragma omp parallel for schedule(static)
    for (int i = 0; i < static_cast<int>(queries.size()); ++i) {
        const int thread = omp_get_thread_num();
        if (thread >= thread_count) std::abort();
        results[static_cast<std::size_t>(i)] = workers[static_cast<std::size_t>(thread)]->Search(
            queries[static_cast<std::size_t>(i)]);
    }
}

}  // namespace

int main(int argc, char** argv) {
    // dataset norm ab/ba memory threads output repetitions variant
    if (argc != 8) return 2;
    const std::filesystem::path dir(argv[1]);
    const float norm = std::stof(argv[2]);
    const bool ab = std::string(argv[3]) == "ab";
    if (std::string(argv[4]) != "memory") return 2;
    const unsigned threads = std::stoul(argv[5]);
    const unsigned repetitions = std::stoul(argv[7]);
    if (threads == 0U || repetitions != 30U) return 2;
    omp_set_num_threads(static_cast<int>(threads));

    DatasetMetadata metadata;
    metadata.Load(dir / "metadata.json");
    const unsigned num_queries = ab ? metadata.num_points_a : metadata.num_points_b;
    const unsigned num_base_points = ab ? metadata.num_points_b : metadata.num_points_a;
    const auto queries = PointSet::LoadPointsFromFile(dir / (ab ? "A.bin" : "B.bin"), num_queries,
                                                       metadata.num_dimensions);
    if (queries.size() != num_queries) return 3;

    std::vector<std::unique_ptr<AnnSearcher>> workers;
    workers.push_back(std::make_unique<QalshAnnSearcher>(2.0F, 1608637542U));
    const auto open_start = std::chrono::steady_clock::now();
    workers.front()->Init(PointSetMetadata{dir / (ab ? "B.bin" : "A.bin"), num_base_points,
                                           metadata.num_dimensions},
                          norm);
    for (unsigned thread = 1U; thread < threads; ++thread) workers.push_back(workers.front()->Clone());
    const auto opened = std::chrono::steady_clock::now();

    unsigned warm_threads = 0U;
#pragma omp parallel reduction(+ : warm_threads)
    { ++warm_threads; }
    if (warm_threads != threads || workers.size() != threads) return 4;

    std::vector<AnnResult> results(num_queries);
    const auto warm_start = std::chrono::steady_clock::now();
    RunPopulation(queries, workers, results);
    const auto warmed = std::chrono::steady_clock::now();
    const std::uint64_t warm_hash = HashResults(results);

    std::vector<std::uint64_t> repetition_hashes;
    repetition_hashes.reserve(repetitions);
    const auto measured_start = std::chrono::steady_clock::now();
    for (unsigned repetition = 0U; repetition < repetitions; ++repetition) {
        RunPopulation(queries, workers, results);
        repetition_hashes.push_back(HashResults(results));
    }
    const auto measured_end = std::chrono::steady_clock::now();
    const std::uint64_t final_hash = HashResults(results);

    std::ofstream output(argv[6]);
    if (!output) return 5;
    output << "# warm_hash " << warm_hash << "\n# repetition_hashes";
    for (const std::uint64_t hash : repetition_hashes) output << ' ' << hash;
    output << "\n" << std::setprecision(9);
    for (std::size_t i = 0; i < results.size(); ++i) {
        output << i << ' ' << results[i].point_id << ' ' << results[i].distance << '\n';
    }
    const double open_ms = std::chrono::duration<double, std::milli>(opened - open_start).count();
    const double warm_ms = std::chrono::duration<double, std::milli>(warmed - warm_start).count();
    const double query_ms = std::chrono::duration<double, std::milli>(measured_end - measured_start).count();
    std::cout << std::setprecision(12) << "open_ms " << open_ms << " warm_ms " << warm_ms
              << " query_ms " << query_ms << " query_ms_per_population " << query_ms / repetitions
              << " warm_hash " << warm_hash << " final_hash " << final_hash << '\n';
    return 0;
}
