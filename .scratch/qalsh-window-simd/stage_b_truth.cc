#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

std::vector<float> Read(const char* path, std::size_t count) {
    std::vector<float> values(count);
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(count * sizeof(float)));
    if (!input || input.gcount() != static_cast<std::streamsize>(count * sizeof(float))) throw 2;
    return values;
}

int main(int argc, char** argv) {
    // nq nb dimensions queries.bin base.bin truth.tsv
    if (argc != 7) return 2;
    const std::size_t nq = std::stoul(argv[1]);
    const std::size_t nb = std::stoul(argv[2]);
    const std::size_t dimensions = std::stoul(argv[3]);
    const auto queries = Read(argv[4], nq * dimensions);
    const auto base = Read(argv[5], nb * dimensions);
    std::ofstream output(argv[6]);
    if (!output) return 3;
    output << std::setprecision(17);
    for (std::size_t query_id = 0; query_id < nq; ++query_id) {
        double best = std::numeric_limits<double>::infinity();
        std::size_t best_id = 0;
        for (std::size_t point_id = 0; point_id < nb; ++point_id) {
            double distance = 0.0;
            for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
                distance += std::abs(static_cast<double>(queries[query_id * dimensions + dimension]) -
                                     static_cast<double>(base[point_id * dimensions + dimension]));
            }
            if (distance < best || (distance == best && point_id < best_id)) {
                best = distance;
                best_id = point_id;
            }
        }
        output << query_id << ' ' << best_id << ' ' << best << ' ' << static_cast<float>(best) << '\n';
    }
}
