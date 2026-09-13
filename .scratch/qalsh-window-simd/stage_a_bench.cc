#include "window_clamp.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using qalsh::PointId;
using qalsh::Projection;
using qalsh::detail::HitRange;

struct Case {
    std::unique_ptr<std::byte[]> storage;
    HitRange range{};
    float bound{0.0F};
};

Case MakeCase(std::size_t length, std::size_t accepted, bool reverse) {
    Case result;
    const std::size_t bytes = length * qalsh::window_detail::kEntryStride;
    result.storage = std::make_unique<std::byte[]>(std::max<std::size_t>(bytes, 1U));
    std::vector<Projection> logical(length);
    for (std::size_t i = 0; i < length; ++i) {
        const float magnitude = static_cast<float>(accepted == 0U ? i + 1U : i);
        logical[i] = reverse ? -magnitude : magnitude;
    }
    for (std::size_t i = 0; i < length; ++i) {
        const std::size_t physical = reverse ? length - 1U - i : i;
        auto* entry = result.storage.get() + physical * qalsh::window_detail::kEntryStride;
        const PointId id = static_cast<PointId>(i);
        std::memcpy(entry, &logical[i], sizeof(Projection));
        std::memcpy(entry + sizeof(Projection), &id, sizeof(id));
    }
    result.bound = accepted == 0U ? 0.0F
                                  : (accepted == length ? static_cast<float>(length)
                                                         : static_cast<float>(accepted) - 0.5F);
    const std::byte* first = nullptr;
    if (length != 0U) {
        auto* data = result.storage.get();
        first = reverse ? data + (length - 1U) * qalsh::window_detail::kEntryStride : data;
    }
    result.range = HitRange{first, length, reverse, 0U, 0.0F};
    return result;
}

void ClampScalar(HitRange& range, float bound) {
    const auto inside = [&](std::size_t position) {
        const auto* bytes = range.reverse
                                ? range.first - position * qalsh::window_detail::kEntryStride
                                : range.first + position * qalsh::window_detail::kEntryStride;
        Projection value{0.0F};
        std::memcpy(&value, bytes, sizeof(value));
        return std::abs(range.query_value - value) <= bound;
    };
    if (range.count == 0U || inside(range.count - 1U)) return;
    if (!inside(0U)) {
        range.count = 0U;
        return;
    }
    std::size_t lo = 1U;
    std::size_t hi = range.count;
    while (lo < hi) {
        const std::size_t middle = lo + (hi - lo) / 2U;
        if (inside(middle)) lo = middle + 1U;
        else hi = middle;
    }
    range.count = lo;
}

void CheckCases(const std::vector<Case>& cases) {
    for (const Case& test : cases) {
        HitRange scalar = test.range;
        HitRange candidate = test.range;
        ClampScalar(scalar, test.bound);
        qalsh::window_detail::ClampToWindow(candidate, test.bound);
        if (scalar.count != candidate.count) throw std::runtime_error("kernel oracle mismatch");
    }
}

std::vector<std::pair<std::size_t, std::uint64_t>> ReadHistogram(const char* path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error(std::string("cannot open histogram: ") + path);
    std::vector<std::pair<std::size_t, std::uint64_t>> histogram;
    std::size_t length = 0U;
    std::uint64_t frequency = 0U;
    while (input >> length >> frequency) histogram.emplace_back(length, frequency);
    if (histogram.empty()) throw std::runtime_error("empty histogram");
    return histogram;
}

std::vector<Case> MakeSynthetic(std::size_t count, std::size_t min_length,
                                std::size_t max_length, std::uint32_t seed) {
    std::mt19937 generator(seed);
    std::uniform_int_distribution<std::size_t> length_distribution(min_length, max_length);
    std::vector<Case> cases;
    cases.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t length = length_distribution(generator);
        std::uniform_int_distribution<std::size_t> accepted_distribution(0U, length);
        cases.push_back(MakeCase(length, accepted_distribution(generator), (i & 1U) != 0U));
    }
    return cases;
}

std::vector<Case> MakeHistogramSynthetic(std::size_t count, const char* available_path,
                                          const char* accepted_path, std::uint32_t seed) {
    const auto available = ReadHistogram(available_path);
    const auto accepted = ReadHistogram(accepted_path);
    std::vector<double> available_weights;
    std::vector<double> accepted_weights;
    available_weights.reserve(available.size());
    accepted_weights.reserve(accepted.size());
    for (const auto& [length, frequency] : available) {
        (void)length;
        available_weights.push_back(static_cast<double>(frequency));
    }
    for (const auto& [length, frequency] : accepted) {
        (void)length;
        accepted_weights.push_back(static_cast<double>(frequency));
    }
    std::mt19937 generator(seed);
    std::discrete_distribution<std::size_t> available_distribution(available_weights.begin(),
                                                                    available_weights.end());
    std::discrete_distribution<std::size_t> accepted_distribution(accepted_weights.begin(),
                                                                    accepted_weights.end());
    std::vector<Case> cases;
    cases.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t available_length = available[available_distribution(generator)].first;
        const std::size_t accepted_length =
            std::min(available_length, accepted[accepted_distribution(generator)].first);
        cases.push_back(MakeCase(available_length, accepted_length, (i & 1U) != 0U));
    }
    return cases;
}

std::vector<Case> MakeTails() {
    const std::array<std::size_t, 12> lengths{31U, 32U, 33U, 63U, 64U, 65U,
                                               127U, 128U, 129U, 255U, 256U, 257U};
    std::vector<Case> cases;
    for (std::size_t length : lengths) {
        const std::array<std::size_t, 6> accepted_lengths{
            0U, 1U, length / 4U, length / 2U, length - 1U, length};
        for (std::size_t accepted : accepted_lengths) {
            cases.push_back(MakeCase(length, accepted, false));
            cases.push_back(MakeCase(length, accepted, true));
        }
    }
    return cases;
}

[[nodiscard]] std::uint64_t Timed(const std::vector<Case>& cases, bool scalar,
                                  std::size_t repetitions, std::uint64_t& checksum) {
    volatile std::uint64_t sink = 0U;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t repetition = 0; repetition < repetitions; ++repetition) {
        for (const Case& test : cases) {
            HitRange range = test.range;
            if (scalar) ClampScalar(range, test.bound);
            else qalsh::window_detail::ClampToWindow(range, test.bound);
            sink += range.count;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    checksum ^= sink;
    return static_cast<std::uint64_t>(elapsed);
}

void Run(const std::string& name, const std::vector<Case>& cases) {
    CheckCases(cases);
    constexpr std::size_t repetitions = 2500U;
    std::uint64_t checksum = 0U;
    std::array<std::uint64_t, 5> scalar{};
    std::array<std::uint64_t, 5> candidate{};
    for (std::size_t sample = 0; sample < scalar.size(); ++sample) {
        // Alternate order to reduce one-sided frequency drift. The checksum
        // keeps both paths observable and is checked after every population.
        if ((sample & 1U) == 0U) {
            scalar[sample] = Timed(cases, true, repetitions, checksum);
            candidate[sample] = Timed(cases, false, repetitions, checksum);
        } else {
            candidate[sample] = Timed(cases, false, repetitions, checksum);
            scalar[sample] = Timed(cases, true, repetitions, checksum);
        }
    }
    std::sort(scalar.begin(), scalar.end());
    std::sort(candidate.begin(), candidate.end());
    const double scalar_ns = static_cast<double>(scalar[2]) / (cases.size() * repetitions);
    const double candidate_ns = static_cast<double>(candidate[2]) / (cases.size() * repetitions);
    std::cout << name << " cases=" << cases.size() << " scalar_ns=" << scalar_ns
              << " candidate_ns=" << candidate_ns << " ratio=" << candidate_ns / scalar_ns
              << " checksum=" << checksum << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    if (!qalsh::window_detail::UsesSse2()) {
        std::cout << "stage-a skipped: SIMD path is unavailable\n";
        return 0;
    }
    if (argc != 3) throw std::runtime_error("usage: stage_a_bench available.tsv accepted.tsv");
    Run("histogram-synthetic", MakeHistogramSynthetic(16384U, argv[1], argv[2], 20260912U));
    Run("short", MakeSynthetic(4096U, 1U, 32U, 20260913U));
    Run("tails", MakeTails());
    Run("medium", MakeSynthetic(4096U, 33U, 128U, 20260914U));
    Run("long", MakeSynthetic(2048U, 129U, 1024U, 20260915U));
    return 0;
}
