#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

class InputError : public std::runtime_error {
   public:
    explicit InputError(const std::string& message) : std::runtime_error(message) {}
};

class SameFileError final : public InputError {
   public:
    explicit SameFileError(const std::string& message) : InputError(message) {}
};

[[nodiscard]] bool ParseSize(std::string_view token, std::size_t& value) {
    if (token.empty()) return false;
    unsigned long long parsed = 0;
    const auto result = std::from_chars(token.data(), token.data() + token.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != token.data() + token.size() ||
        parsed > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    value = static_cast<std::size_t>(parsed);
    return true;
}

[[nodiscard]] float ParseFiniteDistance(std::string_view token, const std::string& path,
                                        std::size_t line) {
    if (token.empty()) {
        throw InputError(path + ": line " + std::to_string(line) + " has an empty distance");
    }
    std::string owned(token);
    char* end = nullptr;
    errno = 0;
    const float value = std::strtof(owned.c_str(), &end);
    if (end != owned.c_str() + owned.size() || !std::isfinite(value) ||
        value < 0.0F) {
        throw InputError(path + ": line " + std::to_string(line) +
                         " has a non-finite, negative, or malformed distance");
    }
    return value;
}

[[nodiscard]] std::vector<float> ReadFloats(const std::filesystem::path& path,
                                            std::size_t count) {
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(float)) {
        throw InputError(path.string() + ": requested binary input is too large");
    }
    const auto expected_bytes = static_cast<std::uintmax_t>(count * sizeof(float));
    std::error_code error;
    const auto actual_bytes = std::filesystem::file_size(path, error);
    if (error) throw InputError(path.string() + ": cannot stat binary input");
    if (actual_bytes != expected_bytes) {
        throw InputError(path.string() + ": binary input size does not match the declared shape");
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) throw InputError(path.string() + ": cannot open binary input");
    std::vector<float> values(count);
    input.read(reinterpret_cast<char*>(values.data()),
               static_cast<std::streamsize>(count * sizeof(float)));
    if (!input || input.gcount() != static_cast<std::streamsize>(count * sizeof(float))) {
        throw InputError(path.string() + ": truncated binary input");
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!std::isfinite(values[index])) {
            throw InputError(path.string() + ": binary input contains a non-finite value at " +
                             std::to_string(index));
        }
    }
    return values;
}

struct ResultRow {
    std::uint32_t point_id{0};
    float reported_distance{0.0F};
};

[[nodiscard]] std::vector<ResultRow> ReadResults(const std::filesystem::path& path,
                                                  std::size_t query_count,
                                                  std::size_t point_count) {
    std::ifstream input(path);
    if (!input) throw InputError(path.string() + ": cannot open result file");
    std::vector<ResultRow> rows(query_count);
    std::vector<bool> seen(query_count, false);
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') continue;

        std::size_t field_end = line.find_first_of(" \t\r", first);
        if (field_end == std::string::npos) {
            throw InputError(path.string() + ": line " + std::to_string(line_number) +
                             " does not contain three fields");
        }
        const std::string qid_token = line.substr(first, field_end - first);
        const auto id_begin = line.find_first_not_of(" \t\r", field_end);
        if (id_begin == std::string::npos) {
            throw InputError(path.string() + ": line " + std::to_string(line_number) +
                             " does not contain three fields");
        }
        field_end = line.find_first_of(" \t\r", id_begin);
        if (field_end == std::string::npos) {
            throw InputError(path.string() + ": line " + std::to_string(line_number) +
                             " does not contain three fields");
        }
        const std::string id_token = line.substr(id_begin, field_end - id_begin);
        const auto distance_begin = line.find_first_not_of(" \t\r", field_end);
        if (distance_begin == std::string::npos) {
            throw InputError(path.string() + ": line " + std::to_string(line_number) +
                             " does not contain three fields");
        }
        field_end = line.find_first_of(" \t\r", distance_begin);
        const std::string distance_token = line.substr(
            distance_begin, field_end == std::string::npos ? std::string::npos : field_end - distance_begin);
        if (field_end != std::string::npos &&
            line.find_first_not_of(" \t\r", field_end) != std::string::npos) {
            throw InputError(path.string() + ": line " + std::to_string(line_number) +
                             " contains extra fields");
        }

        std::size_t query_id = 0;
        std::size_t point_id = 0;
        if (!ParseSize(qid_token, query_id) || !ParseSize(id_token, point_id)) {
            throw InputError(path.string() + ": line " + std::to_string(line_number) +
                             " contains a malformed query or point ID");
        }
        if (query_id >= query_count) {
            throw InputError(path.string() + ": line " + std::to_string(line_number) +
                             " has an out-of-range query ID");
        }
        if (point_id >= point_count || point_id > std::numeric_limits<std::uint32_t>::max()) {
            throw InputError(path.string() + ": line " + std::to_string(line_number) +
                             " has an out-of-range point ID");
        }
        if (seen[query_id]) {
            throw InputError(path.string() + ": line " + std::to_string(line_number) +
                             " duplicates a query ID");
        }
        seen[query_id] = true;
        rows[query_id] = ResultRow{static_cast<std::uint32_t>(point_id),
                                   ParseFiniteDistance(distance_token, path.string(), line_number)};
    }
    if (!input.eof()) throw InputError(path.string() + ": read error");
    for (std::size_t query_id = 0; query_id < query_count; ++query_id) {
        if (!seen[query_id]) {
            throw InputError(path.string() + ": missing query ID " + std::to_string(query_id));
        }
    }
    return rows;
}

[[nodiscard]] bool SameFile(const std::filesystem::path& lhs,
                            const std::filesystem::path& rhs) {
    std::error_code lhs_error;
    std::error_code rhs_error;
    const auto lhs_absolute = std::filesystem::absolute(lhs, lhs_error).lexically_normal();
    const auto rhs_absolute = std::filesystem::absolute(rhs, rhs_error).lexically_normal();
    if (!lhs_error && !rhs_error && lhs_absolute == rhs_absolute) return true;

    lhs_error.clear();
    rhs_error.clear();
    const auto lhs_canonical = std::filesystem::weakly_canonical(lhs, lhs_error);
    const auto rhs_canonical = std::filesystem::weakly_canonical(rhs, rhs_error);
    if (!lhs_error && !rhs_error && lhs_canonical == rhs_canonical) return true;

    std::error_code equivalent_error;
    const bool equivalent = std::filesystem::equivalent(lhs, rhs, equivalent_error);
    return !equivalent_error && equivalent;
}

template <typename Float>
[[nodiscard]] long double UnitRoundoff() {
    // numeric_limits::epsilon() is the distance between 1 and the next value;
    // the standard relative-rounding unit is half of that distance.
    return static_cast<long double>(std::numeric_limits<Float>::epsilon()) / 2.0L;
}

[[nodiscard]] long double RoundoffBound(long double magnitude, std::size_t dimensions,
                                        long double operations_per_dimension,
                                        long double unit_roundoff) {
    if (magnitude == 0.0L) return 0.0L;
    if (!std::isfinite(magnitude) || !std::isfinite(unit_roundoff)) {
        throw InputError("independent distance roundoff bound is not finite");
    }

    // gamma_n = n*u/(1-n*u) is the usual forward-error bound for n
    // correctly-rounded operations.  The operation counts below are derived
    // from the independent accumulator and the float32 distance path rather
    // than selected as a data-dependent absolute tolerance.
    const long double operation_count = operations_per_dimension *
                                         static_cast<long double>(dimensions) + 1.0L;
    const long double operation_product = operation_count * unit_roundoff;
    if (!(operation_product < 1.0L)) {
        throw InputError("independent distance has too many operations for its error bound");
    }
    const long double gamma = operation_product / (1.0L - operation_product);
    const long double bound = gamma * magnitude * (1.0L + gamma);
    if (!std::isfinite(bound)) {
        throw InputError("independent distance roundoff bound overflowed");
    }
    return bound;
}

struct DistanceEstimate {
    long double value{0.0L};
    long double independent_error{0.0L};
    long double magnitude{0.0L};
};

[[nodiscard]] DistanceEstimate L1(const std::vector<float>& query,
                                   const std::vector<float>& base,
                                   std::size_t query_id, std::size_t point_id,
                                   std::size_t dimensions) {
    // Each input is binary32 and is exactly representable in long double. A
    // Neumaier accumulator retains a correction for lost low-order terms;
    // the reported independent_error is still a conservative gamma bound for
    // every arithmetic operation in this independent reference.
    long double sum = 0.0L;
    long double correction = 0.0L;
    long double magnitude_sum = 0.0L;
    const std::size_t query_offset = query_id * dimensions;
    const std::size_t point_offset = point_id * dimensions;
    for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
        const long double difference =
            static_cast<long double>(query[query_offset + dimension]) -
            static_cast<long double>(base[point_offset + dimension]);
        const long double term = std::fabs(difference);
        if (!std::isfinite(term)) throw InputError("independent L1 reference overflowed");

        const long double next = sum + term;
        if (!std::isfinite(next)) throw InputError("independent L1 reference overflowed");
        if (std::fabs(sum) >= std::fabs(term)) {
            correction += (sum - next) + term;
        } else {
            correction += (term - next) + sum;
        }
        if (!std::isfinite(correction)) {
            throw InputError("independent L1 reference compensation overflowed");
        }
        sum = next;

        const long double next_magnitude = magnitude_sum + term;
        if (!std::isfinite(next_magnitude)) {
            throw InputError("independent L1 reference magnitude overflowed");
        }
        magnitude_sum = next_magnitude;
    }

    const long double value = sum + correction;
    if (!std::isfinite(value)) throw InputError("independent L1 reference overflowed");
    if (value < 0.0L) throw InputError("independent L1 reference became negative");
    const long double nonnegative_value = value;
    const long double magnitude = std::max(magnitude_sum, nonnegative_value);
    // Neumaier's correction uses four additions/subtractions per term plus
    // the final correction add.  This bound is intentionally tied to that
    // operation count and to long-double machine precision.
    const long double independent_error =
        RoundoffBound(magnitude, dimensions, 4.0L, UnitRoundoff<long double>());
    return DistanceEstimate{nonnegative_value, independent_error, magnitude};
}

[[nodiscard]] long double ApproximationRatio(long double distance, long double best) {
    if (best == 0.0L) {
        return distance == 0.0L ? 1.0L : std::numeric_limits<long double>::infinity();
    }
    return distance / best;
}

[[nodiscard]] bool MaybeExact(const DistanceEstimate& distance,
                              const DistanceEstimate& best) {
    // A zero reference is an exact mathematical statement: no nonzero value
    // is made exact merely because an absolute tolerance is convenient.
    if (best.value == 0.0L) return distance.value == 0.0L;
    const long double uncertainty = distance.independent_error + best.independent_error;
    return std::isfinite(uncertainty) && distance.value <= best.value + uncertainty;
}

[[nodiscard]] bool DefinitelyGreater(const DistanceEstimate& lhs,
                                     const DistanceEstimate& rhs) {
    const long double lhs_lower = std::max(0.0L, lhs.value - lhs.independent_error);
    const long double rhs_upper = rhs.value + rhs.independent_error;
    return std::isfinite(lhs_lower) && std::isfinite(rhs_upper) && lhs_lower > rhs_upper;
}

[[nodiscard]] bool DefinitelyWorseRatio(const DistanceEstimate& candidate,
                                        const DistanceEstimate& baseline,
                                        const DistanceEstimate& best) {
    if (best.value == 0.0L) {
        // Ratios against zero are infinite.  The selected-distance interval
        // below remains the meaningful comparison when both returned points
        // are nonzero; this branch catches the exact-zero baseline case.
        return baseline.value == 0.0L && candidate.value != 0.0L;
    }

    const long double denominator_lower = best.value - best.independent_error;
    const long double denominator_upper = best.value + best.independent_error;
    if (!(denominator_lower > 0.0L) || !std::isfinite(denominator_upper)) return false;

    const long double candidate_lower =
        std::max(0.0L, candidate.value - candidate.independent_error) / denominator_upper;
    const long double baseline_upper =
        (baseline.value + baseline.independent_error) / denominator_lower;
    return std::isfinite(candidate_lower) && std::isfinite(baseline_upper) &&
           candidate_lower > baseline_upper;
}

struct FloatRoundingCell {
    long double lower{0.0L};
    long double upper{0.0L};
};

[[nodiscard]] FloatRoundingCell RoundingCell(float value) {
    const long double center = static_cast<long double>(value);
    const float predecessor = std::nextafter(value, -std::numeric_limits<float>::infinity());
    const float successor = std::nextafter(value, std::numeric_limits<float>::infinity());

    // Reported distances are nonnegative.  At zero, do not include the
    // negative predecessor's rounding cell; at FLT_MAX, finite values above
    // the largest float encode as infinity and therefore are not accepted.
    const long double lower = value > 0.0F
                                  ? (static_cast<long double>(predecessor) + center) / 2.0L
                                  : 0.0L;
    const long double upper = std::isfinite(successor)
                                  ? (center + static_cast<long double>(successor)) / 2.0L
                                  : center;
    return FloatRoundingCell{lower, upper};
}

[[nodiscard]] long double ReportedDistanceError(float reported,
                                                 const DistanceEstimate& distance) {
    const float expected = static_cast<float>(distance.value);
    if (!std::isfinite(expected)) return std::numeric_limits<long double>::infinity();
    return std::fabs(static_cast<long double>(reported) - static_cast<long double>(expected));
}

[[nodiscard]] bool ReportedDistanceConsistent(float reported,
                                              const DistanceEstimate& distance,
                                              std::size_t dimensions) {
    const long double float_path_error =
        RoundoffBound(distance.magnitude, dimensions, 3.0L, UnitRoundoff<float>());
    const long double total_error = distance.independent_error + float_path_error;
    if (!std::isfinite(total_error)) return false;

    const long double lower = std::max(0.0L, distance.value - total_error);
    const long double upper = distance.value + total_error;
    const FloatRoundingCell cell = RoundingCell(reported);
    return std::isfinite(lower) && std::isfinite(upper) && cell.upper >= lower &&
           cell.lower <= upper;
}

struct QueryComparison {
    bool baseline_exact{false};
    bool candidate_exact{false};
    bool pass{true};
    long double best_distance{0.0L};
    long double best_error{0.0L};
    long double baseline_distance{0.0L};
    long double baseline_error{0.0L};
    long double candidate_distance{0.0L};
    long double candidate_error{0.0L};
    long double baseline_ratio{1.0L};
    long double candidate_ratio{1.0L};
    long double baseline_reported_error{0.0L};
    long double candidate_reported_error{0.0L};
    std::string failure;
};

[[nodiscard]] QueryComparison CompareQuery(const std::vector<float>& query,
                                           const std::vector<float>& base,
                                           std::size_t query_id, std::size_t point_count,
                                           std::size_t dimensions,
                                           const ResultRow& baseline,
                                           const ResultRow& candidate) {
    QueryComparison result;
    DistanceEstimate best{std::numeric_limits<long double>::infinity(),
                           std::numeric_limits<long double>::infinity(),
                           std::numeric_limits<long double>::infinity()};
    for (std::size_t point_id = 0; point_id < point_count; ++point_id) {
        const DistanceEstimate distance = L1(query, base, query_id, point_id, dimensions);
        if (distance.value < best.value) best = distance;
    }

    const DistanceEstimate baseline_distance =
        L1(query, base, query_id, baseline.point_id, dimensions);
    const DistanceEstimate candidate_distance =
        L1(query, base, query_id, candidate.point_id, dimensions);
    result.best_distance = best.value;
    result.best_error = best.independent_error;
    result.baseline_distance = baseline_distance.value;
    result.baseline_error = baseline_distance.independent_error;
    result.candidate_distance = candidate_distance.value;
    result.candidate_error = candidate_distance.independent_error;
    result.baseline_exact = MaybeExact(baseline_distance, best);
    result.candidate_exact = MaybeExact(candidate_distance, best);
    result.baseline_ratio = ApproximationRatio(baseline_distance.value, best.value);
    result.candidate_ratio = ApproximationRatio(candidate_distance.value, best.value);

    std::vector<std::string> failures;
    result.baseline_reported_error = ReportedDistanceError(baseline.reported_distance, baseline_distance);
    result.candidate_reported_error = ReportedDistanceError(candidate.reported_distance, candidate_distance);
    if (!ReportedDistanceConsistent(baseline.reported_distance, baseline_distance, dimensions)) {
        failures.push_back("baseline_reported_distance_mismatch");
    }
    if (!ReportedDistanceConsistent(candidate.reported_distance, candidate_distance, dimensions)) {
        failures.push_back("candidate_reported_distance_mismatch");
    }
    if (result.baseline_exact && !result.candidate_exact) failures.push_back("recall_loss");
    if (DefinitelyGreater(candidate_distance, baseline_distance)) {
        failures.push_back("selected_distance_regression");
    }
    // Ratio comparison is deliberately unconditional.  Even when both rows
    // may be exact within independently justified uncertainty, a definite
    // ratio regression must not be hidden by an exact flag.
    if (DefinitelyWorseRatio(candidate_distance, baseline_distance, best)) {
        failures.push_back("approximation_regression");
    }
    if (failures.empty()) return result;

    result.pass = false;
    for (std::size_t index = 0; index < failures.size(); ++index) {
        if (index != 0U) result.failure += ',';
        result.failure += failures[index];
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    // A.bin B.bin nq nb dimensions baseline.tsv candidate.tsv
    if (argc != 8) {
        std::cerr << "usage: stage_b_quality_compare A.bin B.bin nq nb dimensions baseline.tsv candidate.tsv\n";
        return 2;
    }

    try {
        const std::filesystem::path query_path = argv[1];
        const std::filesystem::path base_path = argv[2];
        const std::size_t query_count = [&] {
            std::size_t value = 0;
            if (!ParseSize(argv[3], value) || value == 0U) throw InputError("invalid query count");
            return value;
        }();
        const std::size_t point_count = [&] {
            std::size_t value = 0;
            if (!ParseSize(argv[4], value) || value == 0U) throw InputError("invalid point count");
            return value;
        }();
        const std::size_t dimensions = [&] {
            std::size_t value = 0;
            if (!ParseSize(argv[5], value) || value == 0U) throw InputError("invalid dimension count");
            return value;
        }();
        if (query_count > std::numeric_limits<std::size_t>::max() / dimensions ||
            point_count > std::numeric_limits<std::size_t>::max() / dimensions) {
            throw InputError("declared matrix shape overflows size_t");
        }

        const std::filesystem::path baseline_path = argv[6];
        const std::filesystem::path candidate_path = argv[7];
        if (SameFile(baseline_path, candidate_path)) {
            throw SameFileError("baseline and candidate result paths refer to the same file");
        }

        const auto query = ReadFloats(query_path, query_count * dimensions);
        const auto base = ReadFloats(base_path, point_count * dimensions);
        const auto baseline = ReadResults(baseline_path, query_count, point_count);
        const auto candidate = ReadResults(candidate_path, query_count, point_count);

        std::size_t baseline_hits = 0;
        std::size_t candidate_hits = 0;
        std::size_t both_hits = 0;
        std::size_t baseline_only = 0;
        std::size_t candidate_only = 0;
        std::size_t quality_failures = 0;
        long double max_baseline_ratio = 1.0L;
        long double max_candidate_ratio = 1.0L;
        long double max_baseline_reported_error = 0.0L;
        long double max_candidate_reported_error = 0.0L;
        std::cout << std::setprecision(std::numeric_limits<long double>::max_digits10);
        for (std::size_t query_id = 0; query_id < query_count; ++query_id) {
            const auto comparison = CompareQuery(query, base, query_id, point_count, dimensions,
                                                 baseline[query_id], candidate[query_id]);
            baseline_hits += comparison.baseline_exact ? 1U : 0U;
            candidate_hits += comparison.candidate_exact ? 1U : 0U;
            both_hits += comparison.baseline_exact && comparison.candidate_exact ? 1U : 0U;
            baseline_only += comparison.baseline_exact && !comparison.candidate_exact ? 1U : 0U;
            candidate_only += comparison.candidate_exact && !comparison.baseline_exact ? 1U : 0U;
            max_baseline_ratio = std::max(max_baseline_ratio, comparison.baseline_ratio);
            max_candidate_ratio = std::max(max_candidate_ratio, comparison.candidate_ratio);
            max_baseline_reported_error =
                std::max(max_baseline_reported_error, comparison.baseline_reported_error);
            max_candidate_reported_error =
                std::max(max_candidate_reported_error, comparison.candidate_reported_error);
            if (!comparison.pass) ++quality_failures;
            std::cout << "query " << query_id << " status " << (comparison.pass ? "pass" : "fail")
                      << " baseline_id " << baseline[query_id].point_id
                      << " candidate_id " << candidate[query_id].point_id
                      << " best_distance " << comparison.best_distance
                      << " best_error " << comparison.best_error
                      << " baseline_distance " << comparison.baseline_distance
                      << " baseline_error " << comparison.baseline_error
                      << " candidate_distance " << comparison.candidate_distance
                      << " candidate_error " << comparison.candidate_error
                      << " baseline_exact " << comparison.baseline_exact
                      << " candidate_exact " << comparison.candidate_exact
                      << " baseline_ratio " << comparison.baseline_ratio
                      << " candidate_ratio " << comparison.candidate_ratio
                      << " baseline_reported_error " << comparison.baseline_reported_error
                      << " candidate_reported_error " << comparison.candidate_reported_error;
            if (!comparison.failure.empty()) std::cout << " failure " << comparison.failure;
            std::cout << '\n';
        }
        std::cout << "summary queries " << query_count
                  << " original_hits " << baseline_hits
                  << " candidate_hits " << candidate_hits
                  << " both_hits " << both_hits
                  << " original_only " << baseline_only
                  << " candidate_only " << candidate_only
                  << " quality_failures " << quality_failures
                  << " max_original_ratio " << max_baseline_ratio
                  << " max_candidate_ratio " << max_candidate_ratio
                  << " max_original_reported_error " << max_baseline_reported_error
                  << " max_candidate_reported_error " << max_candidate_reported_error << '\n';
        if (quality_failures != 0U) return 5;
        return 0;
    } catch (const SameFileError& error) {
        std::cerr << "quality comparator rejected evidence: " << error.what() << '\n';
        return 4;
    } catch (const InputError& error) {
        std::cerr << "quality comparator rejected input: " << error.what() << '\n';
        return 3;
    } catch (const std::exception& error) {
        std::cerr << "quality comparator failed: " << error.what() << '\n';
        return 3;
    }
}
