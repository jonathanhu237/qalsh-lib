#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

std::vector<float> Read(const char* path, std::size_t count) {
    std::vector<float> values(count);
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(count * sizeof(float)));
    if (!input || input.gcount() != static_cast<std::streamsize>(count * sizeof(float))) throw 2;
    return values;
}

int main(int argc, char** argv) {
    // A.bin B.bin nq nb dimensions result.tsv label
    if (argc != 8) return 2;
    const std::size_t nq = std::stoul(argv[3]);
    const std::size_t nb = std::stoul(argv[4]);
    const std::size_t dimensions = std::stoul(argv[5]);
    const auto queries = Read(argv[1], nq * dimensions);
    const auto base = Read(argv[2], nb * dimensions);
    std::ifstream result(argv[6]);
    if (!result) return 3;
    std::vector<unsigned> ids(nq, 0);
    std::vector<float> reported(nq, 0.0F);
    std::vector<bool> seen(nq, false);
    std::size_t rows = 0;
    std::size_t malformed = 0;
    std::string line;
    while (std::getline(result, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::size_t qid = 0, id = 0;
        float distance = 0.0F;
        if (std::sscanf(line.c_str(), "%zu %zu %f", &qid, &id, &distance) != 3 || qid >= nq) {
            ++malformed;
            continue;
        }
        if (seen[qid]) ++malformed;
        seen[qid] = true;
        ids[qid] = static_cast<unsigned>(id);
        reported[qid] = distance;
        ++rows;
    }
    std::size_t missing = 0, invalid = 0, exact_id = 0, close_distance = 0, reported_distance_mismatch = 0;
    double max_ratio = 1.0, max_abs_error = 0.0;
    for (std::size_t query_id = 0; query_id < nq; ++query_id) {
        if (!seen[query_id]) { ++missing; continue; }
        if (ids[query_id] >= nb || !std::isfinite(reported[query_id]) || reported[query_id] < 0.0F) {
            ++invalid;
            continue;
        }
        double best = std::numeric_limits<double>::infinity();
        double returned = 0.0;
        for (std::size_t point_id = 0; point_id < nb; ++point_id) {
            double distance = 0.0;
            for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
                distance += std::abs(static_cast<double>(queries[query_id * dimensions + dimension]) -
                                     static_cast<double>(base[point_id * dimensions + dimension]));
            }
            if (point_id == ids[query_id]) returned = distance;
            if (distance < best) best = distance;
        }
        const double expected_float = static_cast<double>(static_cast<float>(returned));
        const double abs_error = std::abs(static_cast<double>(reported[query_id]) - expected_float);
        if (abs_error > max_abs_error) max_abs_error = abs_error;
        if (abs_error > 1.0e-4) ++reported_distance_mismatch;
        // Distances are accumulated as double in the independently computed oracle.
        if (returned <= best + 1.0e-9) ++exact_id;
        if (static_cast<double>(reported[query_id]) <= static_cast<double>(static_cast<float>(best)) + 1.0e-4)
            ++close_distance;
        if (best > 0.0 && std::isfinite(returned)) max_ratio = std::max(max_ratio, returned / best);
    }
    std::cout << "label " << argv[7] << " rows " << rows << " malformed " << malformed << " missing " << missing
              << " invalid " << invalid << " exact_tie_hits " << exact_id << " close_distance_hits " << close_distance
              << " reported_distance_mismatches " << reported_distance_mismatch << std::setprecision(17)
              << " max_ratio " << max_ratio << " max_reported_abs_error " << max_abs_error << '\n';
    return 0;
}
