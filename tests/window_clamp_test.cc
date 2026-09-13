#include "window_clamp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using qalsh::PointId;
using qalsh::Projection;
using qalsh::detail::HitRange;

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct PackedRange {
    std::unique_ptr<std::byte[]> storage;
    std::size_t offset{0};
    std::size_t count{0};
    bool reverse{false};

    PackedRange(const std::vector<Projection>& logical, bool reverse_value,
                std::size_t alignment)
        : offset(alignment), count(logical.size()), reverse(reverse_value) {
        const std::size_t bytes = count * qalsh::window_detail::kEntryStride;
        storage = std::make_unique<std::byte[]>(offset + bytes);
        std::vector<Projection> physical = logical;
        if (reverse) std::reverse(physical.begin(), physical.end());
        for (std::size_t i = 0; i < physical.size(); ++i) {
            auto* entry = storage.get() + offset + i * qalsh::window_detail::kEntryStride;
            const PointId point_id = static_cast<PointId>(i);
            std::memcpy(entry, &physical[i], sizeof(Projection));
            std::memcpy(entry + sizeof(Projection), &point_id, sizeof(point_id));
        }
    }

    [[nodiscard]] HitRange range(Projection query_value) const {
        const std::byte* first = nullptr;
        if (count != 0U) {
            auto* data = storage.get() + offset;
            first = reverse ? data + (count - 1U) * qalsh::window_detail::kEntryStride : data;
        }
        return HitRange{first, count, reverse, 17U, query_value};
    }
};

[[nodiscard]] std::size_t ScalarPrefix(const std::vector<Projection>& logical,
                                       Projection query_value, float bound) {
    for (std::size_t i = 0; i < logical.size(); ++i) {
        if (!(std::abs(query_value - logical[i]) <= bound)) return i;
    }
    return logical.size();
}

void CheckRange(const std::vector<Projection>& logical, Projection query_value,
                float bound, bool reverse, std::size_t alignment) {
    PackedRange packed(logical, reverse, alignment);
    HitRange range = packed.range(query_value);
    const auto* original_first = range.first;
    qalsh::window_detail::ClampToWindow(range, bound);
    const std::size_t expected = ScalarPrefix(logical, query_value, bound);
    if (range.count != expected) {
        std::cerr << "mismatch n=" << logical.size() << " q=" << query_value
                  << " bound=" << bound << " reverse=" << reverse
                  << " alignment=" << alignment << " actual=" << range.count
                  << " expected=" << expected << " values=";
        for (Projection value : logical) std::cerr << ' ' << value;
        std::cerr << '\n';
        throw std::runtime_error("SIMD window prefix differs from scalar oracle");
    }
    Check(range.first == original_first && range.reverse == reverse && range.query_value == query_value,
          "window clamp changed range identity");
}

void EnumerateSorted(const std::array<Projection, 4>& alphabet, std::size_t remaining,
                     std::size_t minimum, std::vector<Projection>& values,
                     const auto& visit) {
    if (remaining == 0U) {
        visit(values);
        return;
    }
    for (std::size_t i = minimum; i < alphabet.size(); ++i) {
        values.push_back(alphabet[i]);
        EnumerateSorted(alphabet, remaining - 1U, i, values, visit);
        values.pop_back();
    }
}

void TestExhaustiveSmallRanges() {
    const std::array<Projection, 4> alphabet{-1.0F, -0.0F, 0.0F, 1.0F};
    const std::array<Projection, 7> queries{-2.0F, -1.0F, -0.0F, 0.0F, 0.5F, 1.0F, 2.0F};
    const std::array<float, 6> bounds{0.0F, std::numeric_limits<float>::denorm_min(),
                                      0.5F, 1.0F, std::numeric_limits<float>::max(),
                                      std::numeric_limits<float>::infinity()};
    std::vector<Projection> values;
    std::size_t cases = 0;
    for (std::size_t length = 0; length <= 16U; ++length) {
        EnumerateSorted(alphabet, length, 0U, values, [&](const std::vector<Projection>& sorted) {
            for (Projection query : queries) {
                for (float bound : bounds) {
                    // A valid outward range starts at the query partition. Test
                    // every possible prefix length of either side of that split.
                    const auto partition = std::lower_bound(sorted.begin(), sorted.end(), query);
                    const auto split = static_cast<std::size_t>(partition - sorted.begin());
                    std::vector<Projection> right(partition, sorted.end());
                    std::vector<Projection> left;
                    left.assign(sorted.begin(), partition);
                    std::reverse(left.begin(), left.end());
                    for (bool reverse : {false, true}) {
                        const auto& side = reverse ? left : right;
                        for (std::size_t available = 0; available <= side.size(); ++available) {
                            std::vector<Projection> logical(side.begin(), side.begin() + available);
                            for (std::size_t alignment = 0; alignment < 8U; ++alignment) {
                                CheckRange(logical, query, bound, reverse, alignment);
                            }
                            ++cases;
                        }
                    }
                    (void)split;
                }
            }
        });
    }
    Check(cases > 100000U, "small-range exhaustive corpus was not exercised");
}

void TestRandomizedBoundaryLengths() {
    constexpr std::uint32_t seed = 20260912U;
    std::mt19937 generator(seed);
    std::uniform_int_distribution<int> value_distribution(-500, 500);
    std::uniform_real_distribution<float> query_distribution(-550.0F, 550.0F);
    std::uniform_real_distribution<float> bound_distribution(0.0F, 80.0F);
    const std::array<std::size_t, 24> lengths{
        1U,   2U,   3U,   4U,   5U,   7U,   8U,    15U,   16U,   17U,   31U,  32U,
        33U,  63U,  64U,  65U,  127U, 128U, 129U, 255U, 256U, 257U, 1025U, 4097U};
    std::size_t cases = 0;
    for (std::size_t length : lengths) {
        for (unsigned repetition = 0; repetition < 8U; ++repetition) {
            std::vector<Projection> sorted(length);
            for (Projection& value : sorted) {
                // Quantization deliberately creates long equal-value runs.
                value = static_cast<float>(value_distribution(generator)) / 8.0F;
            }
            std::sort(sorted.begin(), sorted.end());
            const Projection query = query_distribution(generator);
            const float bound = repetition == 0U ? 0.0F : bound_distribution(generator);
            const auto partition = static_cast<std::size_t>(
                std::lower_bound(sorted.begin(), sorted.end(), query) - sorted.begin());
            const std::vector<Projection> right(sorted.begin() + partition, sorted.end());
            std::vector<Projection> left(sorted.begin(), sorted.begin() + partition);
            std::reverse(left.begin(), left.end());
            for (bool reverse : {false, true}) {
                const auto& side = reverse ? left : right;
                std::vector<std::size_t> availabilities{0U, 1U, 3U, 4U, 5U,
                                                        side.size() / 2U, side.size()};
                if (side.size() > 1U) availabilities.push_back(side.size() - 1U);
                std::sort(availabilities.begin(), availabilities.end());
                availabilities.erase(std::unique(availabilities.begin(), availabilities.end()),
                                     availabilities.end());
                for (std::size_t available : availabilities) {
                    if (available > side.size()) continue;
                    std::vector<Projection> logical(side.begin(), side.begin() + available);
                    for (std::size_t alignment = 0; alignment < 8U; ++alignment) {
                        CheckRange(logical, query, bound, reverse, alignment);
                    }
                    ++cases;
                }
            }
        }
    }
    Check(cases > 1000U, "randomized boundary corpus was not exercised");
}

void TestFloatingPointBoundaries() {
    const float denorm = std::numeric_limits<float>::denorm_min();
    const float maximum = std::numeric_limits<float>::max();
    const std::vector<Projection> sorted{-maximum, -1.0F, -denorm, -0.0F,
                                         0.0F, denorm, 1.0F, maximum};
    const std::array<Projection, 8> queries{-maximum, -1.0F, -denorm, -0.0F,
                                            0.0F, denorm, 1.0F, maximum};
    const std::array<float, 10> bounds{
        0.0F,
        denorm,
        std::nextafter(1.0F, 0.0F),
        1.0F,
        std::nextafter(1.0F, std::numeric_limits<float>::infinity()),
        std::nextafter(maximum, 0.0F),
        maximum,
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::min(),
        2.0F,
    };
    for (Projection query : queries) {
        const auto partition = static_cast<std::size_t>(
            std::lower_bound(sorted.begin(), sorted.end(), query) - sorted.begin());
        const std::vector<Projection> right(sorted.begin() + partition, sorted.end());
        std::vector<Projection> left(sorted.begin(), sorted.begin() + partition);
        std::reverse(left.begin(), left.end());
        for (float bound : bounds) {
            for (bool reverse : {false, true}) {
                const auto& side = reverse ? left : right;
                for (std::size_t available = 0; available <= side.size(); ++available) {
                    std::vector<Projection> logical(side.begin(), side.begin() + available);
                    for (std::size_t alignment = 0; alignment < 8U; ++alignment) {
                        CheckRange(logical, query, bound, reverse, alignment);
                    }
                }
            }
        }
    }

    // Explicitly exercise the common early exits and cutoff positions.
    for (const auto& [logical, query, bound, expected] : std::array{
             std::tuple<std::vector<Projection>, Projection, float, std::size_t>{
                 {1.0F, 2.0F, 3.0F, 4.0F}, 0.0F, 0.0F, 0U},
             std::tuple<std::vector<Projection>, Projection, float, std::size_t>{
                 {-1.0F, -0.0F, 0.0F, 1.0F}, 0.0F, std::numeric_limits<float>::infinity(), 4U},
             std::tuple<std::vector<Projection>, Projection, float, std::size_t>{
                 {0.0F, 1.0F, 2.0F, 3.0F}, 0.0F, 1.0F, 2U},
         }) {
        for (bool reverse : {false, true}) {
            const std::vector<Projection>& side = logical;
            if (!reverse) {
                Check(ScalarPrefix(side, query, bound) == expected,
                      "explicit floating-point prefix setup is invalid");
            }
            CheckRange(side, query, bound, reverse, 0U);
        }
    }

    // A finite subtraction can overflow to infinity. Infinity remains inside
    // an unbounded helper window, but not a finite max-float window.  This is
    // a valid reverse outward range from the query-side endpoint.
    const std::vector<Projection> extremes{maximum, 0.0F, -maximum};
    CheckRange(extremes, maximum, std::numeric_limits<float>::infinity(), true, 0U);
    CheckRange(extremes, maximum, maximum, true, 0U);
}

void TestEmptyAndExactTail() {
    CheckRange({}, 0.0F, 0.0F, false, 0U);
    CheckRange({}, 0.0F, std::numeric_limits<float>::infinity(), true, 0U);
    for (std::size_t count = 1; count <= 129U; ++count) {
        std::vector<Projection> right(count);
        for (std::size_t i = 0; i < count; ++i) right[i] = static_cast<float>(i);
        CheckRange(right, 0.0F, static_cast<float>(count / 2U), false, 0U);
        std::vector<Projection> left(count);
        for (std::size_t i = 0; i < count; ++i) left[i] = -static_cast<float>(i);
        CheckRange(left, 0.0F, static_cast<float>(count / 2U), true, 0U);
    }
}

}  // namespace

int main() {
    std::cout << "window clamp mode: "
              << (qalsh::window_detail::UsesSse2() ? "sse2" : "scalar") << '\n';
    TestExhaustiveSmallRanges();
    TestRandomizedBoundaryLengths();
    TestFloatingPointBoundaries();
    TestEmptyAndExactTail();
    return 0;
}
