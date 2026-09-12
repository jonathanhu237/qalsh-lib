#include "qalsh/qalsh.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <thread>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace qalsh {
namespace {

constexpr std::array<char, 8> kBPlusMagic{'Q', 'A', 'L', 'S', 'H', 'B', 'P', '1'};
constexpr std::array<char, 8> kSortedArrayMagic{'Q', 'A', 'L', 'S', 'H', 'A', 'R', '1'};
constexpr std::uint32_t kVersion = 2;
constexpr std::uint32_t kEndianMarker = 0x01020304U;
constexpr std::uint32_t kPageHeaderSize = 32;
constexpr std::uint32_t kLeafPage = 1;
constexpr std::uint32_t kInternalPage = 2;
constexpr std::size_t kMaxReasonablePageSize = 64U * 1024U * 1024U;
// Prevent malformed metadata from requesting an unbounded vector/table
// allocation before the file layout has been checked. These bounds are well
// above the supported consumer workloads and are part of the first-version
// resource contract.
constexpr std::uint32_t kMaxReasonableHashTables = 1U << 20;
constexpr std::uint32_t kMaxReasonableDimensions = 1U << 20;
constexpr std::uint32_t kMaxReasonablePoints = 1U << 30;

struct PageHeader {
    std::uint32_t kind{0};
    std::uint32_t count{0};
    std::uint64_t previous{0};
    std::uint64_t next{0};
};
static_assert(sizeof(PageHeader) == 24);

struct DiskEntry {
    float value{0.0F};
    std::uint32_t point_id{0};
};
static_assert(sizeof(DiskEntry) == 8);

struct DiskFileHeader {
    char magic[8]{};
    std::uint32_t version{0};
    std::uint32_t endian{0};
    std::uint32_t header_size{0};
    std::uint32_t metric{0};
    std::uint32_t num_points{0};
    std::uint32_t num_dimensions{0};
    std::uint32_t num_hash_tables{0};
    std::uint32_t page_size{0};
    std::uint32_t seed{0};
    std::uint32_t collision_threshold{0};
    std::uint32_t scan_quantum{0};
    std::uint32_t candidate_budget{0};
    float approximation_ratio{0.0F};
    float bucket_width{0.0F};
    float error_probability{0.0F};
    float initial_radius{0.0F};
    float radius_growth{0.0F};
    std::uint64_t roots_offset{0};
    std::uint64_t projections_offset{0};
    std::uint64_t data_offset{0};
    std::uint64_t page_count{0};
    std::uint64_t file_size{0};
};
static_assert(sizeof(DiskFileHeader) == 120);

struct MemoryCursor final : ProjectionIndex::Cursor {
    static constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();
    const ProjectionIndex* owner{nullptr};
    std::uint32_t table_id{0};
    Projection query_value{0.0F};
    std::size_t left{kNoIndex};
    std::size_t right{kNoIndex};
    bool left_blocked{false};
    bool right_blocked{false};
    float last_bound{-std::numeric_limits<float>::infinity()};
};

struct DiskCursor final : ProjectionIndex::Cursor {
    const ProjectionIndex* owner{nullptr};
    std::uint32_t table_id{0};
    Projection query_value{0.0F};
    std::uint64_t left_page{0};
    std::uint64_t left_leaf_rank{0};
    std::uint64_t right_page{0};
    std::uint64_t right_leaf_rank{0};
    std::uint32_t left_index{0};
    std::uint32_t right_index{0};
    bool left_active{false};
    bool right_active{false};
    bool left_blocked{false};
    bool right_blocked{false};
    float last_bound{-std::numeric_limits<float>::infinity()};
    // Immutable mmap pages can be retained by a cursor between quantum
    // calls.  This avoids rebuilding the same span and page address for the
    // usual one-leaf-per-table workload.
    std::uint64_t cached_page{0};
    const std::byte* cached_page_data{nullptr};
};

struct ArrayCursor final : ProjectionIndex::Cursor {
    static constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();
    const ProjectionIndex* owner{nullptr};
    std::uint32_t table_id{0};
    Projection query_value{0.0F};
    std::size_t left{kNoIndex};
    std::size_t right{kNoIndex};
    bool left_blocked{false};
    bool right_blocked{false};
    float last_bound{-std::numeric_limits<float>::infinity()};
};

struct Page {
    PageHeader header{};
    // Leaf plans borrow the sorted build table until serialization completes.
    std::span<const DiskEntry> entries;
    std::vector<std::uint64_t> children;
    std::vector<float> separators;
    float first_key{0.0F};
};

struct SearchRuntime {
    float radius{1.0F};
    float bound{0.0F};
    std::size_t evaluated{0};
    std::size_t hits{0};
    bool round_complete{false};
    std::span<std::uint32_t> seen;
    std::uint32_t seen_epoch{0};
    std::vector<std::uint8_t> table_exhausted;
    std::size_t exhausted_tables{0};
    std::size_t completed_tables{0};
    std::vector<std::uint8_t> table_round_complete;
    // Results are kept in their public ordering.  This avoids copying and
    // sorting a heap every time a strategy observes a query snapshot while
    // retaining O(k) result storage.
    std::vector<Neighbor> neighbors;
    QuerySnapshot snapshot_cache{};
    bool snapshot_dirty{true};
};

template <typename Strategy>
struct SearchHitContext {
    SearchRuntime* runtime{nullptr};
    Strategy* strategy{nullptr};
    const PointAccessor* accessor{nullptr};
    const SearchOptions* options{nullptr};
    const IndexMetadata* metadata{nullptr};
    PointView query;
    std::uint32_t k{0};
};

struct QueryScratch {
    bool in_use{false};
    // A weak identity lets cursor storage be reused without keeping a large
    // mmap/index alive until thread exit.  Expired identities force cursor
    // replacement before any cached cursor is touched.
    std::weak_ptr<const ProjectionIndex> index_identity;
    std::vector<std::unique_ptr<ProjectionIndex::Cursor>> cursors;
    std::vector<std::uint32_t> seen_epochs;
    std::vector<Projection> query_projections;
    std::uint32_t epoch{0};
};

struct QueryScratchLease {
    QueryScratch* scratch{nullptr};
    ~QueryScratchLease() {
        if (scratch != nullptr) {
            scratch->in_use = false;
        }
    }
};

thread_local std::vector<std::unique_ptr<QueryScratch>> query_scratch_pool;

[[nodiscard]] BoundedDistance BoundedDistanceValueUnchecked(PointView lhs, PointView rhs,
                                                            Metric metric, Distance upper_bound);

[[nodiscard]] bool IsFinite(float value) { return std::isfinite(value); }

#if defined(__GNUC__) || defined(__clang__)
[[gnu::always_inline]]
#endif
inline void Require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::invalid_argument(std::string(message));
    }
}

#if defined(__GNUC__) || defined(__clang__)
[[gnu::always_inline]]
#endif
inline void RequireFinite(float value, std::string_view name) {
    if (!IsFinite(value)) {
        throw std::invalid_argument(std::string(name) + " must be finite");
    }
}

float WindowBound(float width, float radius) {
    const double value = static_cast<double>(width) * radius / 2.0;
    Require(std::isfinite(value) && value >= 0 && value <= std::numeric_limits<float>::max(),
            "QALSH scan bound is not representable");
    return static_cast<float>(value);
}

[[nodiscard]] bool CheckedAdd(std::uint64_t lhs, std::uint64_t rhs, std::uint64_t& result) {
    if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

[[nodiscard]] bool CheckedMultiply(std::uint64_t lhs, std::uint64_t rhs, std::uint64_t& result) {
    if (lhs != 0 && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

[[nodiscard]] std::uint64_t CheckedAddOrThrow(std::uint64_t lhs, std::uint64_t rhs,
                                               std::string_view description) {
    std::uint64_t result = 0;
    Require(CheckedAdd(lhs, rhs, result), description);
    return result;
}

[[nodiscard]] std::uint64_t CheckedMultiplyOrThrow(std::uint64_t lhs, std::uint64_t rhs,
                                                    std::string_view description) {
    std::uint64_t result = 0;
    Require(CheckedMultiply(lhs, rhs, result), description);
    return result;
}

[[nodiscard]] std::size_t CheckedSize(std::uint64_t value, std::string_view description) {
    Require(value <= std::numeric_limits<std::size_t>::max(), description);
    return static_cast<std::size_t>(value);
}

[[nodiscard]] std::uint64_t AlignUp(std::uint64_t value, std::uint64_t alignment) {
    Require(alignment > 0, "alignment must be positive");
    const std::uint64_t remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }
    return CheckedAddOrThrow(value, alignment - remainder, "index layout arithmetic overflow");
}

[[nodiscard]] bool IsValidMetric(Metric metric) {
    return metric == Metric::l1 || metric == Metric::l2;
}

[[nodiscard]] bool KeyLess(float lhs_value, PointId lhs_id,
                           float rhs_value, PointId rhs_id) {
    return lhs_value < rhs_value ||
           (lhs_value == rhs_value && lhs_id < rhs_id);
}

void ReadAt(int fd, void* destination, std::size_t bytes, std::uint64_t offset, const std::string& path) {
    if (bytes == 0) {
        return;
    }
    Require(destination != nullptr, "read destination is null");
    Require(offset <= static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()),
            "index offset is not representable by the host file API");
    const std::uint64_t bytes_u64 = static_cast<std::uint64_t>(bytes);
    Require(bytes_u64 <= static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()) - offset,
            "index read range is not representable by the host file API");
    auto* output = static_cast<std::byte*>(destination);
    std::size_t done = 0;
    while (done < bytes) {
        const std::uint64_t current_offset = offset + static_cast<std::uint64_t>(done);
        const ssize_t count = ::pread(fd, output + done, bytes - done, static_cast<off_t>(current_offset));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            throw std::runtime_error("failed to read index " + path + " at offset " +
                                     std::to_string(current_offset));
        }
        done += static_cast<std::size_t>(count);
    }
}

void WriteAt(int fd, const void* source, std::size_t bytes, std::uint64_t offset, const std::string& path) {
    if (bytes == 0) {
        return;
    }
    Require(source != nullptr, "write source is null");
    Require(offset <= static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()),
            "index offset is not representable by the host file API");
    const std::uint64_t bytes_u64 = static_cast<std::uint64_t>(bytes);
    Require(bytes_u64 <= static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()) - offset,
            "index write range is not representable by the host file API");
    const auto* input = static_cast<const std::byte*>(source);
    std::size_t done = 0;
    while (done < bytes) {
        const std::uint64_t current_offset = offset + static_cast<std::uint64_t>(done);
        const ssize_t count = ::pwrite(fd, input + done, bytes - done, static_cast<off_t>(current_offset));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            throw std::runtime_error("failed to write index " + path + " at offset " +
                                     std::to_string(current_offset));
        }
        done += static_cast<std::size_t>(count);
    }
}

void ValidateQalsh(const QalshParameters& qalsh, bool require_collision_threshold) {
    RequireFinite(qalsh.approximation_ratio, "approximation ratio");
    Require(qalsh.approximation_ratio > 1.0F, "approximation ratio must be greater than one");
    RequireFinite(qalsh.bucket_width, "bucket width");
    Require(qalsh.bucket_width > 0.0F, "bucket width must be positive");
    Require(IsFinite(qalsh.error_probability) && qalsh.error_probability > 0.0F &&
                qalsh.error_probability < 1.0F,
            "error probability must be in (0, 1)");
    Require(qalsh.num_hash_tables > 0, "the index must contain at least one projection table");
    Require(qalsh.candidate_budget > 0, "candidate budget must be positive");
    if (require_collision_threshold) {
        Require(qalsh.collision_threshold > 0 && qalsh.collision_threshold <= qalsh.num_hash_tables,
                "collision threshold must be in the table-count range");
    } else {
        Require(qalsh.collision_threshold <= qalsh.num_hash_tables,
                "collision threshold exceeds the table count");
    }
    Require(IsFinite(qalsh.initial_radius) && qalsh.initial_radius > 0.0F,
            "initial radius must be positive");
    Require(IsFinite(qalsh.radius_growth) && qalsh.radius_growth > 1.0F,
            "radius growth must be greater than one");
    Require(qalsh.scan_quantum > 0, "scan quantum must be positive");
}

void ValidateFileHeaderEnvelope(const DiskFileHeader& header, std::uint64_t actual_size) {
    Require(std::equal(kBPlusMagic.begin(), kBPlusMagic.end(), std::begin(header.magic)),
            "index has an invalid QALSH magic header");
    Require(header.version == kVersion, "unsupported QALSH index format version");
    Require(header.endian == kEndianMarker,
            "QALSH index byte order is not supported on this host");
    Require(header.header_size == sizeof(DiskFileHeader),
            "QALSH index header size is invalid");
    Require(header.metric == static_cast<std::uint32_t>(Metric::l1) ||
                header.metric == static_cast<std::uint32_t>(Metric::l2),
            "QALSH index metric is invalid");
    Require(header.num_points > 0 && header.num_points <= kMaxReasonablePoints &&
                header.num_dimensions > 0 && header.num_dimensions <= kMaxReasonableDimensions &&
                header.num_hash_tables > 0 && header.num_hash_tables <= kMaxReasonableHashTables,
            "QALSH index dimensions are invalid");
    Require(header.page_size >= 512 && header.page_size <= kMaxReasonablePageSize &&
                (header.page_size % 8U == 0),
            "QALSH index page size is invalid");
    Require(header.page_count > 1, "QALSH index has no data pages");
    const std::uint64_t page_bytes = CheckedMultiplyOrThrow(
        header.page_count, header.page_size, "QALSH index page layout overflows");
    const std::uint64_t expected_file_size = CheckedAddOrThrow(
        header.data_offset, page_bytes, "QALSH index file layout overflows");
    Require(header.file_size == expected_file_size && header.file_size == actual_size,
            "QALSH index file layout is invalid or incomplete");
    Require(header.file_size <= std::numeric_limits<std::size_t>::max(),
            "QALSH index file is too large for this host");
    const std::uint64_t roots_bytes = CheckedMultiplyOrThrow(
        header.num_hash_tables, sizeof(std::uint64_t),
        "QALSH index root metadata overflows");
    const std::uint64_t projection_values = CheckedMultiplyOrThrow(
        header.num_hash_tables, header.num_dimensions,
        "QALSH index projection metadata overflows");
    const std::uint64_t projection_bytes = CheckedMultiplyOrThrow(
        projection_values, sizeof(Coordinate),
        "QALSH index projection metadata overflows");
    const std::uint64_t roots_end = CheckedAddOrThrow(
        sizeof(DiskFileHeader), roots_bytes,
        "QALSH index metadata offsets overflow");
    const std::uint64_t projections_end = CheckedAddOrThrow(
        header.projections_offset, projection_bytes,
        "QALSH index metadata offsets overflow");
    Require(header.roots_offset == sizeof(DiskFileHeader) &&
                header.projections_offset == roots_end &&
                header.data_offset >= projections_end &&
                header.data_offset % header.page_size == 0 &&
                projections_end <= header.data_offset &&
                header.data_offset <= header.file_size,
            "QALSH index metadata offsets are invalid");
}

void ValidateSortedArrayFileHeaderEnvelope(const DiskFileHeader& header,
                                           std::uint64_t actual_size) {
    Require(std::equal(kSortedArrayMagic.begin(), kSortedArrayMagic.end(),
                       std::begin(header.magic)),
            "index has an invalid QALSH sorted-array magic header");
    Require(header.version == kVersion, "unsupported QALSH index format version");
    Require(header.endian == kEndianMarker,
            "QALSH index byte order is not supported on this host");
    Require(header.header_size == sizeof(DiskFileHeader),
            "QALSH index header size is invalid");
    Require(header.metric == static_cast<std::uint32_t>(Metric::l1) ||
                header.metric == static_cast<std::uint32_t>(Metric::l2),
            "QALSH index metric is invalid");
    Require(header.num_points > 0 && header.num_points <= kMaxReasonablePoints &&
                header.num_dimensions > 0 && header.num_dimensions <= kMaxReasonableDimensions &&
                header.num_hash_tables > 0 && header.num_hash_tables <= kMaxReasonableHashTables,
            "QALSH index dimensions are invalid");
    Require(header.page_size >= 512 && header.page_size <= kMaxReasonablePageSize &&
                (header.page_size % 8U == 0),
            "QALSH sorted-array region size is invalid");
    const std::uint64_t total_entries = CheckedMultiplyOrThrow(
        header.num_points, header.num_hash_tables,
        "QALSH sorted-array entry count overflows");
    const std::uint64_t entry_bytes = CheckedMultiplyOrThrow(
        total_entries, sizeof(DiskEntry), "QALSH sorted-array data size overflows");
    const std::uint64_t region_capacity =
        (static_cast<std::uint64_t>(header.page_size) - kPageHeaderSize) / sizeof(DiskEntry);
    Require(region_capacity > 0, "QALSH sorted-array region cannot hold an entry");
    const std::uint64_t regions_per_table =
        (static_cast<std::uint64_t>(header.num_points) - 1U) / region_capacity + 1U;
    const std::uint64_t region_count = CheckedMultiplyOrThrow(
        regions_per_table, header.num_hash_tables,
        "QALSH sorted-array region count overflows");
    Require(header.page_count == region_count + 1U,
            "QALSH sorted-array region count is invalid");
    const std::uint64_t expected_file_size = CheckedAddOrThrow(
        header.data_offset, entry_bytes, "QALSH sorted-array file layout overflows");
    Require(header.file_size == expected_file_size && header.file_size == actual_size,
            "QALSH sorted-array file layout is invalid or incomplete");
    Require(header.file_size <= std::numeric_limits<std::size_t>::max(),
            "QALSH index file is too large for this host");
    const std::uint64_t roots_bytes = CheckedMultiplyOrThrow(
        header.num_hash_tables, sizeof(std::uint64_t),
        "QALSH index root metadata overflows");
    const std::uint64_t projection_values = CheckedMultiplyOrThrow(
        header.num_hash_tables, header.num_dimensions,
        "QALSH index projection metadata overflows");
    const std::uint64_t projection_bytes = CheckedMultiplyOrThrow(
        projection_values, sizeof(Coordinate),
        "QALSH index projection metadata overflows");
    const std::uint64_t roots_end = CheckedAddOrThrow(
        sizeof(DiskFileHeader), roots_bytes, "QALSH index metadata offsets overflow");
    const std::uint64_t projections_end = CheckedAddOrThrow(
        header.projections_offset, projection_bytes,
        "QALSH index metadata offsets overflow");
    Require(header.roots_offset == sizeof(DiskFileHeader) &&
                header.projections_offset == roots_end &&
                header.data_offset >= projections_end &&
                header.data_offset % header.page_size == 0 &&
                projections_end <= header.data_offset &&
                header.data_offset <= header.file_size,
            "QALSH sorted-array metadata offsets are invalid");
}

void ValidateConfig(const IndexConfig& config) {
    Require(IsValidMetric(config.metric), "unsupported metric");
    Require(config.build_threads > 0 && config.build_threads <= 1024,
            "build_threads is outside the supported resource bounds");
    Require(config.num_points > 0 && config.num_points <= kMaxReasonablePoints,
            "num_points is outside the supported resource bounds");
    Require(config.num_dimensions > 0 && config.num_dimensions <= kMaxReasonableDimensions,
            "num_dimensions is outside the supported resource bounds");
    Require(config.qalsh.num_hash_tables > 0 &&
                config.qalsh.num_hash_tables <= kMaxReasonableHashTables,
            "num_hash_tables is outside the supported resource bounds");
    ValidateQalsh(config.qalsh, false);
    const std::uint64_t expected_projection_values = CheckedMultiplyOrThrow(
        config.qalsh.num_hash_tables, config.num_dimensions, "projection-vector count overflows");
    const std::size_t expected_size = CheckedSize(expected_projection_values,
                                                  "projection-vector count is too large");
    if (!config.projection_vectors.empty()) {
        Require(config.projection_vectors.size() == expected_size,
                "projection_vectors has the wrong size");
        for (float value : config.projection_vectors) {
            RequireFinite(value, "projection vector coordinate");
        }
    }
}

// Only internal, non-throwing table sorts run concurrently. Point accessors
// may reuse one scratch buffer and are always called serially before this phase.
template <typename Tables, typename Entries>
void SortTables(Tables& tables, std::uint32_t requested_threads, Entries entries) {
    const auto sort_one = [&](std::size_t id) {
        std::ranges::sort(entries(tables[id]), [](const auto& lhs, const auto& rhs) {
            return lhs.value != rhs.value ? lhs.value < rhs.value : lhs.point_id < rhs.point_id;
        });
    };
    const auto workers = std::min<std::size_t>(requested_threads, tables.size());
    if (workers <= 1) {
        for (std::size_t id = 0; id < tables.size(); ++id) sort_one(id);
        return;
    }
    std::atomic<std::size_t> next{0};
    const auto work = [&] {
        for (;;) {
            const auto id = next.fetch_add(1, std::memory_order_relaxed);
            if (id >= tables.size()) return;
            sort_one(id);
        }
    };
    std::vector<std::jthread> threads;
    threads.reserve(workers - 1);
    try {
        for (std::size_t i = 1; i < workers; ++i) threads.emplace_back(work);
    } catch (...) {
        next.store(tables.size(), std::memory_order_relaxed);
        threads.clear();
        throw;
    }
    work();
    // jthread destruction joins all sorts before the builder consumes tables.
}

[[nodiscard]] std::vector<Coordinate> MakeProjectionVectors(const IndexConfig& config) {
    if (!config.projection_vectors.empty()) {
        return config.projection_vectors;
    }

    const std::uint64_t expected_values = CheckedMultiplyOrThrow(
        config.qalsh.num_hash_tables, config.num_dimensions, "projection-vector count overflows");
    const std::size_t expected_size = CheckedSize(expected_values, "projection-vector count is too large");
    std::mt19937 generator(config.seed);
    std::vector<Coordinate> vectors;
    vectors.reserve(expected_size);
    if (config.metric == Metric::l1) {
        std::cauchy_distribution<float> distribution(0.0F, 1.0F);
        for (std::size_t i = 0; i < expected_size; ++i) {
            const float value = distribution(generator);
            RequireFinite(value, "generated projection vector coordinate");
            vectors.push_back(value);
        }
    } else if (config.metric == Metric::l2) {
        std::normal_distribution<float> distribution(0.0F, 1.0F);
        for (std::size_t i = 0; i < expected_size; ++i) {
            const float value = distribution(generator);
            RequireFinite(value, "generated projection vector coordinate");
            vectors.push_back(value);
        }
    } else {
        throw std::invalid_argument("unsupported metric");
    }
    return vectors;
}

[[nodiscard]] IndexMetadata MetadataFromConfig(const IndexConfig& config, std::uint32_t page_size,
                                               IndexLayout layout = IndexLayout::in_memory) {
    return IndexMetadata{.metric = config.metric,
                         .num_points = config.num_points,
                         .num_dimensions = config.num_dimensions,
                         .num_hash_tables = config.qalsh.num_hash_tables,
                         .page_size = page_size,
                         .seed = config.seed,
                         .file_size = 0,
                         .qalsh = config.qalsh,
                         .layout = layout};
}

void ValidatePoint(PointView point, std::uint32_t dimensions, PointId point_id) {
    if (point.size() != dimensions) {
        throw std::invalid_argument("point accessor returned the wrong dimensionality for point " +
                                    std::to_string(point_id));
    }
    unsigned finite = 1;
#ifdef QALSH_USE_OPENMP_SIMD
#pragma omp simd reduction(& : finite)
#endif
    for (std::size_t i = 0; i < point.size(); ++i) {
        finite &= static_cast<unsigned>(IsFinite(point[i]));
    }
    if (finite == 0) {
        throw std::invalid_argument("point accessor returned a non-finite coordinate");
    }
}

void ValidateDistanceOperands(PointView lhs, PointView rhs) {
    Require(lhs.size() == rhs.size(), "distance operands have different dimensions");
    for (float value : lhs) {
        RequireFinite(value, "distance operand");
    }
    for (float value : rhs) {
        RequireFinite(value, "distance operand");
    }
}

// Independent tables hide the SIMD accumulation latency without changing a
// table's reduction order. This is projection arithmetic, not candidate
// batching: accepted points are still distance-evaluated immediately.
template <unsigned Count>
void DotGroup(PointView point, const Coordinate* vectors, std::size_t dimensions, float* output) {
    static_assert(Count == 1 || Count == 4);
    [[maybe_unused]] float a = 0, b = 0, c = 0, d = 0;
#ifdef QALSH_USE_OPENMP_SIMD
#pragma omp simd reduction(+ : a, b, c, d)
#endif
    for (std::size_t i = 0; i < dimensions; ++i) {
        const float x = point[i];
        a += x * vectors[i];
        if constexpr (Count == 4) {
            b += x * vectors[dimensions + i];
            c += x * vectors[2 * dimensions + i];
            d += x * vectors[3 * dimensions + i];
        }
    }
    output[0] = a;
    if constexpr (Count == 4) {
        output[1] = b; output[2] = c; output[3] = d;
    }
}

void ProjectPoint(PointView point, const Coordinate* vectors, std::size_t dimensions,
                  std::span<Projection> output) {
    std::size_t table = 0;
    for (; table + 4 <= output.size(); table += 4) {
        DotGroup<4>(point, vectors + table * dimensions, dimensions, output.data() + table);
    }
    for (; table < output.size(); ++table) {
        DotGroup<1>(point, vectors + table * dimensions, dimensions, output.data() + table);
    }
}

[[nodiscard]] PageHeader ReadPageHeader(std::span<const std::byte> page) {
    Require(page.size() >= kPageHeaderSize, "index page is too small");
    PageHeader header{};
    std::memcpy(&header, page.data(), sizeof(header));
    return header;
}

[[nodiscard]] DiskEntry ReadDiskEntry(std::span<const std::byte> page, const PageHeader& header,
                                      std::uint32_t index) {
    Require(header.kind == kLeafPage && index < header.count, "QALSH leaf entry is out of range");
    const std::uint64_t offset = CheckedAddOrThrow(
        kPageHeaderSize,
        CheckedMultiplyOrThrow(index, sizeof(DiskEntry), "QALSH leaf entry offset overflows"),
        "QALSH leaf entry offset overflows");
    const std::uint64_t end = CheckedAddOrThrow(offset, sizeof(DiskEntry),
                                                 "QALSH leaf entry offset overflows");
    Require(end <= page.size(), "QALSH leaf entry exceeds its page boundary");
    DiskEntry entry{};
    std::memcpy(&entry, page.data() + static_cast<std::size_t>(offset), sizeof(entry));
    return entry;
}

[[nodiscard]] DiskEntry ReadArrayEntry(const std::byte* data, std::size_t bytes,
                                       std::size_t index) {
    const std::uint64_t offset = CheckedMultiplyOrThrow(
        index, sizeof(DiskEntry), "QALSH sorted-array entry offset overflows");
    const std::uint64_t end = CheckedAddOrThrow(
        offset, sizeof(DiskEntry), "QALSH sorted-array entry offset overflows");
    Require(end <= bytes, "QALSH sorted-array entry exceeds its table boundary");
    DiskEntry entry{};
    std::memcpy(&entry, data + static_cast<std::size_t>(offset), sizeof(entry));
    return entry;
}

[[nodiscard]] std::uint64_t ReadChild(std::span<const std::byte> page, const PageHeader& header,
                                      std::uint32_t index) {
    Require(header.kind == kInternalPage && index < header.count, "QALSH child is out of range");
    const std::uint64_t offset = CheckedAddOrThrow(
        kPageHeaderSize,
        CheckedMultiplyOrThrow(index, sizeof(std::uint64_t), "QALSH child offset overflows"),
        "QALSH child offset overflows");
    const std::uint64_t end = CheckedAddOrThrow(offset, sizeof(std::uint64_t),
                                                 "QALSH child offset overflows");
    Require(end <= page.size(), "QALSH child exceeds its page boundary");
    std::uint64_t child = 0;
    std::memcpy(&child, page.data() + static_cast<std::size_t>(offset), sizeof(child));
    return child;
}

[[nodiscard]] float ReadSeparator(std::span<const std::byte> page, const PageHeader& header,
                                  std::uint32_t index) {
    Require(header.kind == kInternalPage && index + 1U < header.count,
            "QALSH separator is out of range");
    const std::uint64_t children_bytes = CheckedMultiplyOrThrow(
        header.count, sizeof(std::uint64_t), "QALSH separator offset overflows");
    const std::uint64_t offset = CheckedAddOrThrow(
        CheckedAddOrThrow(kPageHeaderSize, children_bytes, "QALSH separator offset overflows"),
        CheckedMultiplyOrThrow(index, sizeof(float), "QALSH separator offset overflows"),
        "QALSH separator offset overflows");
    const std::uint64_t end = CheckedAddOrThrow(offset, sizeof(float),
                                                 "QALSH separator offset overflows");
    Require(end <= page.size(), "QALSH separator exceeds its page boundary");
    float separator = 0.0F;
    std::memcpy(&separator, page.data() + static_cast<std::size_t>(offset), sizeof(separator));
    return separator;
}

void SerializePage(const Page& page, std::uint32_t page_size, std::vector<std::byte>& output) {
    Require(page_size >= kPageHeaderSize, "page size is too small");
    Require(page.header.kind == kLeafPage || page.header.kind == kInternalPage,
            "cannot serialize an invalid page type");
    output.assign(page_size, std::byte{0});
    std::memcpy(output.data(), &page.header, sizeof(PageHeader));
    if (page.header.kind == kLeafPage) {
        Require(page.header.count == page.entries.size() && page.header.count > 0,
                "cannot serialize an empty or inconsistent leaf page");
        const std::uint64_t bytes = CheckedAddOrThrow(
            kPageHeaderSize,
            CheckedMultiplyOrThrow(page.header.count, sizeof(DiskEntry),
                                   "leaf page size overflows"),
            "leaf page size overflows");
        Require(bytes <= page_size, "leaf page exceeds its page boundary");
        for (std::size_t i = 0; i < page.entries.size(); ++i) {
            std::memcpy(output.data() + kPageHeaderSize + i * sizeof(DiskEntry), &page.entries[i],
                        sizeof(DiskEntry));
        }
    } else {
        Require(page.header.count == page.children.size() && page.header.count >= 2 &&
                    page.separators.size() + 1 == page.children.size(),
                "cannot serialize an inconsistent internal page");
        const std::uint64_t children_bytes = CheckedMultiplyOrThrow(
            page.header.count, sizeof(std::uint64_t), "internal page size overflows");
        const std::uint64_t separator_bytes = CheckedMultiplyOrThrow(
            page.header.count - 1U, sizeof(float), "internal page size overflows");
        const std::uint64_t bytes = CheckedAddOrThrow(
            CheckedAddOrThrow(kPageHeaderSize, children_bytes, "internal page size overflows"),
            separator_bytes, "internal page size overflows");
        Require(bytes <= page_size, "internal page exceeds its page boundary");
        for (std::size_t i = 0; i < page.children.size(); ++i) {
            std::memcpy(output.data() + kPageHeaderSize + i * sizeof(std::uint64_t), &page.children[i],
                        sizeof(std::uint64_t));
        }
        const std::size_t separators_offset = kPageHeaderSize + page.children.size() * sizeof(std::uint64_t);
        for (std::size_t i = 0; i < page.separators.size(); ++i) {
            std::memcpy(output.data() + separators_offset + i * sizeof(float), &page.separators[i],
                        sizeof(float));
        }
    }
}

void PublishIndexFile(const std::string& temporary_path, const std::string& destination, bool overwrite) {
    if (overwrite) {
        if (::rename(temporary_path.c_str(), destination.c_str()) != 0) {
            throw std::runtime_error("failed to atomically replace index " + destination + ": " +
                                     std::strerror(errno));
        }
    } else {
        // link(2) is an atomic no-replace publication primitive on the same
        // filesystem.  Unlike access()+rename(), it cannot clobber a file
        // created by another process between the check and publication.
        if (::link(temporary_path.c_str(), destination.c_str()) != 0) {
            throw std::runtime_error("failed to publish index without replacement " + destination + ": " +
                                     std::strerror(errno));
        }
        if (::unlink(temporary_path.c_str()) != 0) {
            throw std::runtime_error("failed to remove temporary index " + temporary_path + ": " +
                                     std::strerror(errno));
        }
    }

    const std::filesystem::path destination_path(destination);
    const std::string parent = destination_path.parent_path().empty()
                                    ? std::string(".")
                                    : destination_path.parent_path().string();
#ifdef O_DIRECTORY
    const int directory_fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
#else
    const int directory_fd = ::open(parent.c_str(), O_RDONLY);
#endif
    if (directory_fd < 0) {
        throw std::runtime_error("failed to open index directory for durability: " +
                                 std::string(std::strerror(errno)));
    }
    if (::fsync(directory_fd) != 0) {
        const int saved_errno = errno;
        (void)::close(directory_fd);
        throw std::runtime_error("failed to flush index directory: " + std::string(std::strerror(saved_errno)));
    }
    if (::close(directory_fd) != 0) {
        throw std::runtime_error("failed to close index directory: " + std::string(std::strerror(errno)));
    }
}

[[nodiscard]] bool BetterNeighbor(const Neighbor& lhs, const Neighbor& rhs) {
    return lhs.distance < rhs.distance ||
           (lhs.distance == rhs.distance && lhs.point_id < rhs.point_id);
}

void InvalidateSnapshot(SearchRuntime& runtime) noexcept { runtime.snapshot_dirty = true; }

[[nodiscard]] const QuerySnapshot& MakeSnapshot(SearchRuntime& runtime, std::uint32_t k) {
    if (runtime.snapshot_dirty) {
        runtime.snapshot_cache = QuerySnapshot{.radius = runtime.radius,
                                                .bound = runtime.bound,
                                                .k = k,
                                                .evaluated_candidates = runtime.evaluated,
                                                .projection_hits = runtime.hits,
                                                .current_round_complete = runtime.round_complete,
                                                .all_tables_exhausted =
                                                    runtime.exhausted_tables == runtime.table_exhausted.size(),
                                                .neighbors = runtime.neighbors};
        runtime.snapshot_dirty = false;
    }
    return runtime.snapshot_cache;
}

void InsertNeighbor(SearchRuntime& runtime, const Neighbor& candidate, std::uint32_t k) {
    const auto insertion = std::lower_bound(
        runtime.neighbors.begin(), runtime.neighbors.end(), candidate,
        [](const Neighbor& existing, const Neighbor& value) { return BetterNeighbor(existing, value); });
    if (runtime.neighbors.size() == k && insertion == runtime.neighbors.end()) {
        return;
    }
    runtime.neighbors.insert(insertion, candidate);
    if (runtime.neighbors.size() > k) {
        runtime.neighbors.pop_back();
    }
}

template <typename Strategy>
void EvaluateCandidate(SearchHitContext<Strategy>& context, PointId point_id) {
    SearchRuntime& runtime = *context.runtime;
    const IndexMetadata& metadata = *context.metadata;
    Require(point_id < metadata.num_points, "strategy requested an invalid point id");
    if (runtime.seen[point_id] == runtime.seen_epoch) {
        const EvaluationEvent event{.point_id = point_id,
                                    .status = EvaluationStatus::duplicate,
                                    .exact_distance = false,
                                    .distance = 0.0F};
        context.strategy->on_evaluation(event, MakeSnapshot(runtime, context.k));
        return;
    }

    runtime.seen[point_id] = runtime.seen_epoch;
    const PointView point = (*context.accessor)(point_id);
    ValidatePoint(point, metadata.num_dimensions, point_id);
    Distance upper_bound = std::numeric_limits<float>::infinity();
    if (context.options->bounded_distance && runtime.neighbors.size() == context.k) {
        upper_bound = runtime.neighbors.back().distance;
    }
    const BoundedDistance distance =
        BoundedDistanceValueUnchecked(point, context.query, metadata.metric, upper_bound);
    ++runtime.evaluated;
    if (distance.exact) {
        InsertNeighbor(runtime, Neighbor{.distance = distance.distance, .point_id = point_id}, context.k);
    }
    InvalidateSnapshot(runtime);
    const EvaluationEvent event{.point_id = point_id,
                                .status = EvaluationStatus::evaluated,
                                .exact_distance = distance.exact,
                                .distance = distance.distance};
    context.strategy->on_evaluation(event, MakeSnapshot(runtime, context.k));
}

void ResetBoundedSides(float bound, float& last_bound, bool& left_blocked, bool& right_blocked) {
    Require(bound >= last_bound, "scan bound cannot decrease for an existing cursor");
    if (bound > last_bound) {
        left_blocked = false;
        right_blocked = false;
        last_bound = bound;
    }
}

// Sorted outward ranges contain a prefix inside the current radius. Test the
// far endpoint first (the usual case), otherwise locate that prefix by binary
// search. This removes a window branch from every hit, without reading outside
// the already validated leaf/array extent.
void ClampToWindow(detail::HitRange& range, float bound) {
    const auto inside = [&](std::size_t i) {
        const auto* bytes = range.reverse ? range.first - i * 8U : range.first + i * 8U;
        Projection value;
        std::memcpy(&value, bytes, sizeof(value));
        return std::abs(range.query_value - value) <= bound;
    };
    if (range.count == 0 || inside(range.count - 1)) {
        return;
    }
    if (!inside(0)) {
        range.count = 0;
        return;
    }
    std::size_t lo = 1, hi = range.count;
    while (lo < hi) {
        const auto mid = lo + (hi - lo) / 2;
        if (inside(mid)) lo = mid + 1;
        else hi = mid;
    }
    range.count = lo;
}

// Bounded and unbounded calls use exactly the same accumulation order. A
// separate scalar bounded loop and SIMD unbounded loop can disagree by several
// float ULPs and incorrectly prune even identical high-dimensional vectors.
// Double intermediates also keep representable L2 distances from overflowing
// or underflowing merely because their squares do not fit in float32.
template <Metric Norm>
BoundedDistance DistanceKernel(PointView lhs, PointView rhs, Distance upper_bound) {
    constexpr auto infinity = std::numeric_limits<float>::infinity();
    const double expanded = std::nextafter(upper_bound, infinity);
    const double limit = Norm == Metric::l2 ? expanded * expanded : expanded;
    double sum = 0;
    for (std::size_t begin = 0; begin < lhs.size(); begin += 64) {
        const auto end = std::min(begin + 64, lhs.size());
        double block = 0;
#ifdef QALSH_USE_OPENMP_SIMD
#pragma omp simd reduction(+ : block)
#endif
        for (std::size_t i = begin; i < end; ++i) {
            const double difference = double(lhs[i]) - double(rhs[i]);
            if constexpr (Norm == Metric::l2) block += difference * difference;
            else block += std::abs(difference);
        }
        sum += block;
        if (sum > limit) return {.distance = infinity, .exact = false};
    }
    const double value = Norm == Metric::l2 ? std::sqrt(sum) : sum;
    if (value > std::numeric_limits<float>::max()) {
        if (std::isfinite(upper_bound)) return {.distance = infinity, .exact = false};
        throw std::overflow_error("distance is not representable as float32");
    }
    const auto distance = static_cast<float>(value);
    if (distance > upper_bound) return {.distance = infinity, .exact = false};
    return {.distance = distance, .exact = true};
}

[[nodiscard]] BoundedDistance BoundedDistanceValueUnchecked(PointView lhs, PointView rhs,
                                                            Metric metric, Distance upper_bound) {
    return metric == Metric::l1 ? DistanceKernel<Metric::l1>(lhs, rhs, upper_bound)
                               : DistanceKernel<Metric::l2>(lhs, rhs, upper_bound);
}

}  // namespace

QalshParameters DeriveQalshParameters(Metric metric, std::uint32_t num_points, float approximation_ratio,
                                      float error_probability, std::uint32_t candidate_budget) {
    Require(IsValidMetric(metric), "unsupported metric");
    Require(num_points > 0, "num_points must be positive");
    RequireFinite(approximation_ratio, "approximation ratio");
    Require(approximation_ratio > 1.0F, "approximation ratio must be greater than one");
    Require(IsFinite(error_probability) && error_probability > 0.0F && error_probability < 1.0F,
            "error probability must be in (0, 1)");
    Require(candidate_budget > 0, "candidate budget must be positive");

    const double beta = std::min(static_cast<double>(candidate_budget) /
                                     static_cast<double>(num_points),
                                 1.0);
    const double term1 = std::sqrt(std::log(2.0 / beta));
    const double term2 = std::sqrt(std::log(1.0 / static_cast<double>(error_probability)));
    const double numerator = std::pow(term1 + term2, 2.0);
    const double eta = term1 / term2;

    double bucket_width = 0.0;
    double p1 = 0.0;
    double p2 = 0.0;
    if (metric == Metric::l1) {
        bucket_width = 2.0 * std::sqrt(static_cast<double>(approximation_ratio));
        p1 = 2.0 / std::numbers::pi * std::atan(bucket_width / 2.0);
        p2 = 2.0 / std::numbers::pi *
             std::atan(bucket_width / (2.0 * static_cast<double>(approximation_ratio)));
    } else {
        const double c = static_cast<double>(approximation_ratio);
        bucket_width = std::sqrt((8.0 * c * c * std::log(c)) / (c * c - 1.0));
        constexpr double inverse_sqrt_two = 0.7071067811865475244;
        p1 = std::erf((bucket_width / 2.0) * inverse_sqrt_two);
        p2 = std::erf((bucket_width / (2.0 * c)) * inverse_sqrt_two);
    }

    const double denominator = 2.0 * (p1 - p2) * (p1 - p2);
    Require(std::isfinite(denominator) && denominator > 0.0,
            "could not derive QALSH table parameters");
    const double table_value = std::ceil(numerator / denominator);
    Require(std::isfinite(table_value) && table_value >= 1.0 &&
                table_value <= static_cast<double>(std::numeric_limits<std::uint32_t>::max()),
            "derived QALSH table count is not representable");
    const auto table_count = static_cast<std::uint32_t>(table_value);
    const double threshold_value = std::ceil(
        ((eta * p1 + p2) / (1.0 + eta)) * static_cast<double>(table_count));
    Require(std::isfinite(threshold_value) && threshold_value >= 1.0 &&
                threshold_value <= static_cast<double>(std::numeric_limits<std::uint32_t>::max()),
            "derived QALSH collision threshold is not representable");
    const auto threshold = static_cast<std::uint32_t>(threshold_value);
    Require(threshold <= table_count, "derived QALSH parameters are invalid");
    return QalshParameters{.approximation_ratio = approximation_ratio,
                           .bucket_width = static_cast<float>(bucket_width),
                           .error_probability = error_probability,
                           .num_hash_tables = table_count,
                           .collision_threshold = threshold,
                           .candidate_budget = candidate_budget,
                           .initial_radius = 1.0F,
                           .radius_growth = approximation_ratio,
                           .scan_quantum = 128};
}

std::shared_ptr<InMemoryIndex> InMemoryIndex::Build(IndexConfig config, const PointAccessor& accessor) {
    ValidateConfig(config);
    Require(static_cast<bool>(accessor), "point accessor is required");
    auto index = std::shared_ptr<InMemoryIndex>(new InMemoryIndex());
    index->metadata_ = MetadataFromConfig(config, 0);
    index->projection_vectors_ = MakeProjectionVectors(config);
    index->tables_.resize(config.qalsh.num_hash_tables);
    for (Table& table : index->tables_) {
        table.entries.reserve(config.num_points);
    }

    std::vector<Projection> projected(config.qalsh.num_hash_tables);
    for (PointId point_id = 0; point_id < config.num_points; ++point_id) {
        const PointView point = accessor(point_id);
        ValidatePoint(point, config.num_dimensions, point_id);
        ProjectPoint(point, index->projection_vectors_.data(), config.num_dimensions, projected);
        for (std::uint32_t table_id = 0; table_id < config.qalsh.num_hash_tables; ++table_id) {
            const float projected_value = projected[table_id];
            RequireFinite(projected_value, "projection value");
            index->tables_[table_id].entries.push_back(
                Entry{.value = projected_value, .point_id = point_id});
        }
    }
    SortTables(index->tables_, config.build_threads, [](auto& table) -> auto& { return table.entries; });
    return index;
}

const IndexMetadata& InMemoryIndex::metadata() const noexcept { return metadata_; }
std::span<const Coordinate> InMemoryIndex::projection_vectors() const noexcept {
    return projection_vectors_;
}

std::unique_ptr<ProjectionIndex::Cursor> InMemoryIndex::make_cursor(std::uint32_t table_id,
                                                                    Projection query_value) const {
    auto cursor = std::make_unique<MemoryCursor>();
    cursor->owner = this;
    reset_cursor(*cursor, table_id, query_value);
    return cursor;
}

void InMemoryIndex::reset_cursor(Cursor& base_cursor, std::uint32_t table_id,
                                 Projection query_value) const {
    Require(table_id < tables_.size(), "projection table id is out of range");
    RequireFinite(query_value, "projection query value");
    auto* cursor = dynamic_cast<MemoryCursor*>(&base_cursor);
    if (cursor == nullptr || cursor->owner != this) {
        throw std::invalid_argument("cursor does not belong to this in-memory index");
    }
    const auto& entries = tables_[table_id].entries;
    const auto it = std::lower_bound(entries.begin(), entries.end(), query_value,
                                     [](const Entry& entry, float value) {
                                         return entry.value < value;
                                     });
    const std::size_t position = static_cast<std::size_t>(std::distance(entries.begin(), it));
    const ProjectionIndex* owner = cursor->owner;
    *cursor = MemoryCursor{};
    cursor->owner = owner;
    cursor->table_id = table_id;
    cursor->query_value = query_value;
    cursor->left = position == 0 ? MemoryCursor::kNoIndex : position - 1;
    cursor->right = position == entries.size() ? MemoryCursor::kNoIndex : position;
}

ScanReport InMemoryIndex::scan(Cursor& base_cursor, float bound, std::size_t max_entries,
                               void* context, HitCallback callback, ScanScope scope) const {
    Require(callback != nullptr, "projection hit callback is required");
    Require(max_entries > 0, "scan quantum must be positive");
    RequireFinite(bound, "scan bound");
    Require(bound >= 0.0F, "scan bound must not be negative");
    const auto* cursor = dynamic_cast<MemoryCursor*>(&base_cursor);
    Require(cursor != nullptr && cursor->owner == this,
            "cursor does not belong to this in-memory index");
    return scan_impl(base_cursor, bound, max_entries,
                     [&](const detail::HitRange& range) {
                         detail::ForEachHit(range, [&](const ProjectionHit& hit) { callback(context, hit); });
                     }, scope);
}

template <typename Visitor>
ScanReport InMemoryIndex::scan_impl(Cursor& base_cursor, float bound, std::size_t max_entries,
                                    Visitor&& visitor, ScanScope scope) const {
    Require(scope == ScanScope::quantum || scope == ScanScope::range,
            "invalid projection scan scope");
    // Public scan validates foreign inputs. The engine owns cursors from this
    // index's factory/reset path and has already validated its numeric bounds.
    auto* cursor = static_cast<MemoryCursor*>(&base_cursor);

    // Keep the traversal position local across callbacks. The callback has no
    // access to this private state; retaining it in the externally allocated
    // cursor forced repeated alias-sensitive loads/stores in the hot loop.
    MemoryCursor position = *cursor;
    struct CommitPosition {
        MemoryCursor* destination;
        const MemoryCursor& position;
        ~CommitPosition() { *destination = position; }
    } commit{cursor, position};
    cursor = &position;
    const auto& entries = tables_[cursor->table_id].entries;
    static_assert(sizeof(Entry) == 8 && offsetof(Entry, point_id) == 4);
    const auto entry_bytes = std::as_bytes(std::span<const Entry>(entries));
    ResetBoundedSides(bound, cursor->last_bound, cursor->left_blocked, cursor->right_blocked);

    ScanReport report{.table_id = cursor->table_id};
    std::size_t entries_visited = 0;
    const std::size_t left_limit = scope == ScanScope::range
                                       ? max_entries
                                       : max_entries / 2U + max_entries % 2U;
    std::size_t right_limit = scope == ScanScope::range ? max_entries : max_entries / 2U;

    // reset_cursor partitions at lower_bound; the left position only decreases
    // and the right position only increases. The scans cannot meet or overlap.

    auto scan_left = [&]() {
        std::size_t scanned = 0;
        while (scanned < left_limit && cursor->left != MemoryCursor::kNoIndex &&
               !cursor->left_blocked) {
            const std::size_t index = cursor->left;
            const auto available = std::min(left_limit - scanned, index + 1);
            detail::HitRange range{entry_bytes.data() + index * sizeof(Entry),
                                   available, true, cursor->table_id, cursor->query_value};
            ClampToWindow(range, bound);
            cursor->left_blocked = range.count < available;
            if (range.count != 0) visitor(range);
            entries_visited += range.count;
            scanned += range.count;
            cursor->left = range.count == index + 1 ? MemoryCursor::kNoIndex : index - range.count;
        }
    };

    auto scan_right = [&]() {
        std::size_t scanned = 0;
        while (scanned < right_limit && cursor->right != MemoryCursor::kNoIndex &&
               !cursor->right_blocked) {
            const std::size_t index = cursor->right;
            const auto available = std::min(right_limit - scanned, entries.size() - index);
            detail::HitRange range{entry_bytes.data() + index * sizeof(Entry),
                                   available, false, cursor->table_id, cursor->query_value};
            ClampToWindow(range, bound);
            cursor->right_blocked = range.count < available;
            if (range.count != 0) visitor(range);
            entries_visited += range.count;
            scanned += range.count;
            cursor->right = index + range.count == entries.size()
                                ? MemoryCursor::kNoIndex : index + range.count;
        }
    };

    if (cursor->left != MemoryCursor::kNoIndex && !cursor->left_blocked) {
        scan_left();
    }
    if (cursor->left != MemoryCursor::kNoIndex || cursor->right != MemoryCursor::kNoIndex) {
        if (scope == ScanScope::quantum && right_limit == 0) {
            right_limit = max_entries - entries_visited;
        }
        scan_right();
    }

    report.entries_visited = entries_visited;
    report.table_exhausted = cursor->left == MemoryCursor::kNoIndex &&
                             cursor->right == MemoryCursor::kNoIndex;
    report.window_exhausted =
        (cursor->left == MemoryCursor::kNoIndex || cursor->left_blocked) &&
        (cursor->right == MemoryCursor::kNoIndex || cursor->right_blocked);
    return report;
}

struct BPlusTreeIndex::FileHeader {};

void BPlusTreeIndex::Build(const std::string& path, IndexConfig config,
                           const PointAccessor& accessor, std::uint32_t page_size) {
    Build(path, std::move(config), accessor, page_size, false);
}

void BPlusTreeIndex::Build(const std::string& path, IndexConfig config,
                           const PointAccessor& accessor, std::uint32_t page_size,
                           bool overwrite) {
    ValidateConfig(config);
    Require(static_cast<bool>(accessor), "point accessor is required");
    Require(!path.empty(), "index path must not be empty");
    Require(page_size >= 512 && page_size <= kMaxReasonablePageSize && (page_size % 8U == 0),
            "page size must be a multiple of 8 between 512 bytes and 64 MiB");

    const std::vector<Coordinate> projection_vectors = MakeProjectionVectors(config);
    std::vector<std::vector<DiskEntry>> entries(config.qalsh.num_hash_tables);
    for (auto& table : entries) {
        table.reserve(config.num_points);
    }
    std::vector<Projection> projected(config.qalsh.num_hash_tables);
    for (PointId point_id = 0; point_id < config.num_points; ++point_id) {
        const PointView point = accessor(point_id);
        ValidatePoint(point, config.num_dimensions, point_id);
        ProjectPoint(point, projection_vectors.data(), config.num_dimensions, projected);
        for (std::uint32_t table_id = 0; table_id < config.qalsh.num_hash_tables; ++table_id) {
            const float projected_value = projected[table_id];
            RequireFinite(projected_value, "projection value");
            entries[table_id].push_back(DiskEntry{.value = projected_value, .point_id = point_id});
        }
    }
    SortTables(entries, config.build_threads, [](auto& table) -> auto& { return table; });

    const std::size_t leaf_capacity =
        (static_cast<std::size_t>(page_size) - kPageHeaderSize) / sizeof(DiskEntry);
    const std::size_t internal_capacity =
        (static_cast<std::size_t>(page_size) - kPageHeaderSize) /
        (sizeof(std::uint64_t) + sizeof(float));
    Require(leaf_capacity > 0 && internal_capacity >= 2,
            "page size cannot hold a B+ tree node");

    std::uint64_t pages_per_table = 0;
    for (std::size_t count = (config.num_points + leaf_capacity - 1) / leaf_capacity;;) {
        pages_per_table = CheckedAddOrThrow(pages_per_table, count, "page count overflows");
        if (count == 1) break;
        count = (count + internal_capacity - 1) / internal_capacity;
    }
    const auto total_pages = CheckedAddOrThrow(
        CheckedMultiplyOrThrow(pages_per_table, config.qalsh.num_hash_tables,
                               "page count overflows"), 1, "page count overflows");
    std::vector<Page> pages(1);  // Page zero is the null-link sentinel.
    pages.reserve(CheckedSize(total_pages, "page count is too large"));
    std::vector<std::uint64_t> roots;
    roots.reserve(config.qalsh.num_hash_tables);
    for (auto& table_entries : entries) {
        std::vector<std::uint64_t> level_pages;
        std::vector<float> level_keys;
        for (std::size_t begin = 0; begin < table_entries.size(); begin += leaf_capacity) {
            const std::size_t end = std::min(begin + leaf_capacity, table_entries.size());
            Page page;
            page.header = PageHeader{.kind = kLeafPage,
                                     .count = static_cast<std::uint32_t>(end - begin)};
            page.entries = std::span<const DiskEntry>(table_entries).subspan(begin, end - begin);
            Require(!page.entries.empty(), "cannot build an empty projection table");
            page.first_key = page.entries.front().value;
            if (!level_pages.empty()) {
                pages[level_pages.back()].header.next = pages.size();
                page.header.previous = level_pages.back();
            }
            level_pages.push_back(pages.size());
            level_keys.push_back(page.first_key);
            pages.push_back(std::move(page));
        }
        Require(!level_pages.empty(), "cannot build an empty projection table");

        while (level_pages.size() > 1) {
            // Partition the level into the minimum number of groups while
            // distributing entries evenly.  Taking max children greedily can
            // leave a final unary internal page (for example 41 leaves with
            // capacity 40), which is not a valid B+ node.
            const std::size_t group_count =
                (level_pages.size() + internal_capacity - 1U) / internal_capacity;
            const std::size_t base_group_size = level_pages.size() / group_count;
            const std::size_t extra_groups = level_pages.size() % group_count;
            Require(base_group_size >= 2, "B+ tree level cannot be grouped without a unary node");
            std::vector<std::uint64_t> next_pages;
            std::vector<float> next_keys;
            next_pages.reserve(group_count);
            next_keys.reserve(group_count);
            std::size_t begin = 0;
            for (std::size_t group = 0; group < group_count; ++group) {
                const std::size_t group_size = base_group_size + (group < extra_groups ? 1U : 0U);
                const std::size_t end = begin + group_size;
                Page page;
                page.header = PageHeader{.kind = kInternalPage,
                                         .count = static_cast<std::uint32_t>(group_size)};
                page.children.assign(level_pages.begin() + static_cast<std::ptrdiff_t>(begin),
                                     level_pages.begin() + static_cast<std::ptrdiff_t>(end));
                page.separators.reserve(group_size - 1U);
                for (std::size_t child = begin + 1U; child < end; ++child) {
                    page.separators.push_back(level_keys[child]);
                }
                page.first_key = level_keys[begin];
                next_pages.push_back(pages.size());
                next_keys.push_back(page.first_key);
                pages.push_back(std::move(page));
                begin = end;
            }
            level_pages = std::move(next_pages);
            level_keys = std::move(next_keys);
        }
        roots.push_back(level_pages.front());
    }

    const std::uint64_t roots_bytes = CheckedMultiplyOrThrow(
        roots.size(), sizeof(std::uint64_t), "index metadata size overflows");
    const std::uint64_t projections_bytes = CheckedMultiplyOrThrow(
        projection_vectors.size(), sizeof(Coordinate), "projection metadata size overflows");
    const std::uint64_t roots_offset = sizeof(DiskFileHeader);
    const std::uint64_t projections_offset =
        CheckedAddOrThrow(roots_offset, roots_bytes, "index metadata offsets overflow");
    const std::uint64_t data_unaligned = CheckedAddOrThrow(
        projections_offset, projections_bytes, "index metadata offsets overflow");
    const std::uint64_t data_offset = AlignUp(data_unaligned, page_size);
    const std::uint64_t page_count = pages.size();
    const std::uint64_t page_bytes = CheckedMultiplyOrThrow(
        page_count, page_size, "index page layout overflows");
    const std::uint64_t file_size =
        CheckedAddOrThrow(data_offset, page_bytes, "index file size overflows");
    (void)CheckedSize(file_size, "index file is too large for this host");
    Require(file_size <= static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()),
            "index file is too large for the host file API");

    std::string temporary_template = path + ".tmp-XXXXXX";
    std::vector<char> temporary_name(temporary_template.begin(), temporary_template.end());
    temporary_name.push_back('\0');
    int fd = ::mkstemp(temporary_name.data());
    if (fd < 0) {
        throw std::runtime_error("failed to create temporary index " + temporary_template + ": " +
                                 std::strerror(errno));
    }
    const std::string temporary_path(temporary_name.data());
    bool published = false;
    try {
        if (::fchmod(fd, 0644) != 0) {
            throw std::runtime_error("failed to set permissions on temporary index " + temporary_path + ": " +
                                     std::strerror(errno));
        }
        if (::ftruncate(fd, static_cast<off_t>(file_size)) != 0) {
            throw std::runtime_error("failed to size index " + temporary_path + ": " +
                                     std::strerror(errno));
        }

        DiskFileHeader header{};
        std::copy(kBPlusMagic.begin(), kBPlusMagic.end(), std::begin(header.magic));
        header.version = kVersion;
        header.endian = kEndianMarker;
        header.header_size = sizeof(DiskFileHeader);
        header.metric = static_cast<std::uint32_t>(config.metric);
        header.num_points = config.num_points;
        header.num_dimensions = config.num_dimensions;
        header.num_hash_tables = config.qalsh.num_hash_tables;
        header.page_size = page_size;
        header.seed = config.seed;
        header.collision_threshold = config.qalsh.collision_threshold;
        header.scan_quantum = config.qalsh.scan_quantum;
        header.candidate_budget = config.qalsh.candidate_budget;
        header.approximation_ratio = config.qalsh.approximation_ratio;
        header.bucket_width = config.qalsh.bucket_width;
        header.error_probability = config.qalsh.error_probability;
        header.initial_radius = config.qalsh.initial_radius;
        header.radius_growth = config.qalsh.radius_growth;
        header.roots_offset = roots_offset;
        header.projections_offset = projections_offset;
        header.data_offset = data_offset;
        header.page_count = page_count;
        header.file_size = file_size;
        WriteAt(fd, &header, sizeof(header), 0, temporary_path);
        WriteAt(fd, roots.data(), CheckedSize(roots_bytes, "root metadata is too large"), roots_offset,
                temporary_path);
        WriteAt(fd, projection_vectors.data(), CheckedSize(projections_bytes, "projection metadata is too large"),
                projections_offset, temporary_path);
        std::vector<std::byte> page_bytes_buffer;
        for (std::size_t page_id = 1; page_id < pages.size(); ++page_id) {
            SerializePage(pages[page_id], page_size, page_bytes_buffer);
            const std::uint64_t page_offset = CheckedAddOrThrow(
                data_offset,
                CheckedMultiplyOrThrow(page_id, page_size, "index page offset overflows"),
                "index page offset overflows");
            WriteAt(fd, page_bytes_buffer.data(), page_bytes_buffer.size(), page_offset, temporary_path);
        }
        if (::fsync(fd) != 0) {
            throw std::runtime_error("failed to flush index " + temporary_path + ": " +
                                     std::strerror(errno));
        }
        if (::close(fd) != 0) {
            fd = -1;
            throw std::runtime_error("failed to close temporary index " + temporary_path + ": " +
                                     std::strerror(errno));
        }
        fd = -1;
        PublishIndexFile(temporary_path, path, overwrite);
        published = true;
    } catch (...) {
        if (fd >= 0) {
            (void)::close(fd);
        }
        if (!published) {
            (void)::unlink(temporary_path.c_str());
        }
        throw;
    }
}

void SortedArrayIndex::Build(const std::string& path, IndexConfig config,
                             const PointAccessor& accessor, std::uint32_t page_size) {
    Build(path, std::move(config), accessor, page_size, false);
}

void SortedArrayIndex::Build(const std::string& path, IndexConfig config,
                             const PointAccessor& accessor, std::uint32_t page_size,
                             bool overwrite) {
    ValidateConfig(config);
    Require(static_cast<bool>(accessor), "point accessor is required");
    Require(!path.empty(), "index path must not be empty");
    Require(page_size >= 512 && page_size <= kMaxReasonablePageSize && (page_size % 8U == 0),
            "sorted-array region size must be a multiple of 8 between 512 bytes and 64 MiB");

    const std::vector<Coordinate> projection_vectors = MakeProjectionVectors(config);
    std::vector<std::vector<DiskEntry>> entries(config.qalsh.num_hash_tables);
    for (auto& table : entries) {
        table.reserve(config.num_points);
    }
    std::vector<Projection> projected(config.qalsh.num_hash_tables);
    for (PointId point_id = 0; point_id < config.num_points; ++point_id) {
        const PointView point = accessor(point_id);
        ValidatePoint(point, config.num_dimensions, point_id);
        ProjectPoint(point, projection_vectors.data(), config.num_dimensions, projected);
        for (std::uint32_t table_id = 0; table_id < config.qalsh.num_hash_tables; ++table_id) {
            const float projected_value = projected[table_id];
            RequireFinite(projected_value, "projection value");
            entries[table_id].push_back(DiskEntry{.value = projected_value, .point_id = point_id});
        }
    }
    SortTables(entries, config.build_threads, [](auto& table) -> auto& { return table; });

    const std::uint64_t total_entries = CheckedMultiplyOrThrow(
        config.num_points, config.qalsh.num_hash_tables,
        "sorted-array entry count overflows");
    const std::uint64_t entry_bytes = CheckedMultiplyOrThrow(
        total_entries, sizeof(DiskEntry), "sorted-array data size overflows");
    const std::uint64_t region_capacity =
        (static_cast<std::uint64_t>(page_size) - kPageHeaderSize) / sizeof(DiskEntry);
    Require(region_capacity > 0, "sorted-array region cannot hold an entry");
    const std::uint64_t regions_per_table =
        (static_cast<std::uint64_t>(config.num_points) - 1U) / region_capacity + 1U;
    const std::uint64_t region_count = CheckedMultiplyOrThrow(
        regions_per_table, config.qalsh.num_hash_tables,
        "sorted-array region count overflows");

    const std::uint64_t roots_bytes = CheckedMultiplyOrThrow(
        entries.size(), sizeof(std::uint64_t), "index metadata size overflows");
    const std::uint64_t projections_bytes = CheckedMultiplyOrThrow(
        projection_vectors.size(), sizeof(Coordinate), "projection metadata size overflows");
    const std::uint64_t roots_offset = sizeof(DiskFileHeader);
    const std::uint64_t projections_offset =
        CheckedAddOrThrow(roots_offset, roots_bytes, "index metadata offsets overflow");
    const std::uint64_t data_unaligned = CheckedAddOrThrow(
        projections_offset, projections_bytes, "index metadata offsets overflow");
    const std::uint64_t data_offset = AlignUp(data_unaligned, page_size);
    const std::uint64_t file_size =
        CheckedAddOrThrow(data_offset, entry_bytes, "sorted-array file size overflows");
    (void)CheckedSize(file_size, "sorted-array file is too large for this host");
    Require(file_size <= static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()),
            "sorted-array file is too large for the host file API");

    std::vector<std::uint64_t> table_offsets;
    table_offsets.reserve(entries.size());
    std::uint64_t table_offset = data_offset;
    for (const auto& table : entries) {
        table_offsets.push_back(table_offset);
        table_offset = CheckedAddOrThrow(
            table_offset,
            CheckedMultiplyOrThrow(table.size(), sizeof(DiskEntry),
                                   "sorted-array table size overflows"),
            "sorted-array table offset overflows");
    }
    Require(table_offset == file_size, "sorted-array table layout is inconsistent");

    std::string temporary_template = path + ".tmp-XXXXXX";
    std::vector<char> temporary_name(temporary_template.begin(), temporary_template.end());
    temporary_name.push_back('\0');
    int fd = ::mkstemp(temporary_name.data());
    if (fd < 0) {
        throw std::runtime_error("failed to create temporary sorted-array index " +
                                 temporary_template + ": " + std::strerror(errno));
    }
    const std::string temporary_path(temporary_name.data());
    bool published = false;
    try {
        if (::fchmod(fd, 0644) != 0) {
            throw std::runtime_error("failed to set permissions on temporary sorted-array index " +
                                     temporary_path + ": " + std::strerror(errno));
        }
        if (::ftruncate(fd, static_cast<off_t>(file_size)) != 0) {
            throw std::runtime_error("failed to size sorted-array index " + temporary_path + ": " +
                                     std::strerror(errno));
        }

        DiskFileHeader header{};
        std::copy(kSortedArrayMagic.begin(), kSortedArrayMagic.end(), std::begin(header.magic));
        header.version = kVersion;
        header.endian = kEndianMarker;
        header.header_size = sizeof(DiskFileHeader);
        header.metric = static_cast<std::uint32_t>(config.metric);
        header.num_points = config.num_points;
        header.num_dimensions = config.num_dimensions;
        header.num_hash_tables = config.qalsh.num_hash_tables;
        header.page_size = page_size;
        header.seed = config.seed;
        header.collision_threshold = config.qalsh.collision_threshold;
        header.scan_quantum = config.qalsh.scan_quantum;
        header.candidate_budget = config.qalsh.candidate_budget;
        header.approximation_ratio = config.qalsh.approximation_ratio;
        header.bucket_width = config.qalsh.bucket_width;
        header.error_probability = config.qalsh.error_probability;
        header.initial_radius = config.qalsh.initial_radius;
        header.radius_growth = config.qalsh.radius_growth;
        header.roots_offset = roots_offset;
        header.projections_offset = projections_offset;
        header.data_offset = data_offset;
        header.page_count = region_count + 1U;
        header.file_size = file_size;
        WriteAt(fd, &header, sizeof(header), 0, temporary_path);
        WriteAt(fd, table_offsets.data(), CheckedSize(roots_bytes, "sorted-array table metadata is too large"),
                roots_offset, temporary_path);
        WriteAt(fd, projection_vectors.data(),
                CheckedSize(projections_bytes, "projection metadata is too large"),
                projections_offset, temporary_path);
        for (std::size_t table_id = 0; table_id < entries.size(); ++table_id) {
            const auto& table = entries[table_id];
            WriteAt(fd, table.data(),
                    CheckedSize(CheckedMultiplyOrThrow(table.size(), sizeof(DiskEntry),
                                                       "sorted-array table size overflows"),
                                "sorted-array table is too large"),
                    table_offsets[table_id], temporary_path);
        }
        if (::fsync(fd) != 0) {
            throw std::runtime_error("failed to flush sorted-array index " + temporary_path + ": " +
                                     std::strerror(errno));
        }
        if (::close(fd) != 0) {
            fd = -1;
            throw std::runtime_error("failed to close sorted-array index " + temporary_path + ": " +
                                     std::strerror(errno));
        }
        fd = -1;
        PublishIndexFile(temporary_path, path, overwrite);
        published = true;
    } catch (...) {
        if (fd >= 0) {
            (void)::close(fd);
        }
        if (!published) {
            (void)::unlink(temporary_path.c_str());
        }
        throw;
    }
}

BPlusTreeIndex::BPlusTreeIndex(int fd, std::string path) : fd_(fd), path_(std::move(path)) {}

BPlusTreeIndex::~BPlusTreeIndex() {
    if (mapping_ != nullptr && mapping_size_ > 0) {
        (void)::munmap(const_cast<std::byte*>(mapping_), mapping_size_);
    }
    if (fd_ >= 0) {
        (void)::close(fd_);
    }
}

std::shared_ptr<BPlusTreeIndex> BPlusTreeIndex::Open(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        throw std::runtime_error("failed to open index " + path + ": " + std::strerror(errno));
    }
    try {
        struct stat stat_result {};
        if (::fstat(fd, &stat_result) != 0 || stat_result.st_size < 0) {
            throw std::runtime_error("failed to stat index " + path + ": " + std::strerror(errno));
        }
        Require(static_cast<std::uintmax_t>(stat_result.st_size) <=
                    static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()),
                "index file is too large for this host");
        if (stat_result.st_size < static_cast<off_t>(sizeof(DiskFileHeader))) {
            throw std::runtime_error("index is missing or truncated: " + path);
        }
        DiskFileHeader envelope{};
        ReadAt(fd, &envelope, sizeof(envelope), 0, path);
        ValidateFileHeaderEnvelope(envelope, static_cast<std::uint64_t>(stat_result.st_size));
        auto index = std::shared_ptr<BPlusTreeIndex>(new BPlusTreeIndex(fd, path));
        fd = -1;
        // Map before full structural validation so validation and later
        // cursor scans share the same immutable pages instead of issuing one
        // pread per page during every independent open.
        index->mapping_size_ = static_cast<std::size_t>(stat_result.st_size);
        void* mapping = ::mmap(nullptr, index->mapping_size_, PROT_READ, MAP_PRIVATE, index->fd_, 0);
        if (mapping == MAP_FAILED) {
            throw std::runtime_error("failed to map index " + path + ": " + std::strerror(errno));
        }
        index->mapping_ = static_cast<const std::byte*>(mapping);
        index->load_header();
        return index;
    } catch (...) {
        if (fd >= 0) {
            (void)::close(fd);
        }
        throw;
    }
}

void BPlusTreeIndex::load_header() {
    DiskFileHeader header{};
    ReadAt(fd_, &header, sizeof(header), 0, path_);
    if (!std::equal(kBPlusMagic.begin(), kBPlusMagic.end(), std::begin(header.magic))) {
        throw std::runtime_error("index has an invalid QALSH magic header: " + path_);
    }
    Require(header.version == kVersion, "unsupported QALSH index format version");
    Require(header.endian == kEndianMarker, "QALSH index byte order is not supported on this host");
    Require(header.header_size == sizeof(DiskFileHeader), "QALSH index header size is invalid");
    Require(header.metric == static_cast<std::uint32_t>(Metric::l1) ||
                header.metric == static_cast<std::uint32_t>(Metric::l2),
            "QALSH index metric is invalid");
    Require(header.num_points > 0 && header.num_points <= kMaxReasonablePoints &&
                header.num_dimensions > 0 && header.num_dimensions <= kMaxReasonableDimensions &&
                header.num_hash_tables > 0 && header.num_hash_tables <= kMaxReasonableHashTables,
            "QALSH index dimensions are invalid");
    Require(header.page_size >= 512 && header.page_size <= kMaxReasonablePageSize &&
                (header.page_size % 8U == 0),
            "QALSH index page size is invalid");
    Require(header.page_count > 1, "QALSH index has no data pages");
    const std::uint64_t page_bytes = CheckedMultiplyOrThrow(
        header.page_count, header.page_size, "QALSH index page layout overflows");
    const std::uint64_t expected_file_size = CheckedAddOrThrow(
        header.data_offset, page_bytes, "QALSH index file layout overflows");
    Require(header.file_size == expected_file_size, "QALSH index file layout is invalid");
    struct stat stat_result {};
    Require(::fstat(fd_, &stat_result) == 0 &&
                static_cast<std::uint64_t>(stat_result.st_size) == header.file_size,
            "QALSH index file is incomplete");
    Require(header.file_size <= std::numeric_limits<std::size_t>::max(),
            "QALSH index file is too large for this host");
    const std::uint64_t roots_bytes = CheckedMultiplyOrThrow(
        header.num_hash_tables, sizeof(std::uint64_t), "QALSH index root metadata overflows");
    const std::uint64_t projection_values = CheckedMultiplyOrThrow(
        header.num_hash_tables, header.num_dimensions, "QALSH index projection metadata overflows");
    const std::uint64_t projection_bytes = CheckedMultiplyOrThrow(
        projection_values, sizeof(Coordinate), "QALSH index projection metadata overflows");
    const std::uint64_t roots_end = CheckedAddOrThrow(
        sizeof(DiskFileHeader), roots_bytes, "QALSH index metadata offsets overflow");
    const std::uint64_t projections_end = CheckedAddOrThrow(
        header.projections_offset, projection_bytes, "QALSH index metadata offsets overflow");
    Require(header.roots_offset == sizeof(DiskFileHeader) &&
                header.projections_offset == roots_end &&
                header.data_offset >= projections_end &&
                header.data_offset % header.page_size == 0 &&
                projections_end <= header.data_offset && header.data_offset <= header.file_size,
            "QALSH index metadata offsets are invalid");

    metadata_ = IndexMetadata{.metric = static_cast<Metric>(header.metric),
                              .num_points = header.num_points,
                              .num_dimensions = header.num_dimensions,
                              .num_hash_tables = header.num_hash_tables,
                              .page_size = header.page_size,
                              .seed = header.seed,
                              .file_size = header.file_size,
                              .qalsh = QalshParameters{.approximation_ratio = header.approximation_ratio,
                                                       .bucket_width = header.bucket_width,
                                                       .error_probability = header.error_probability,
                                                       .num_hash_tables = header.num_hash_tables,
                                                       .collision_threshold = header.collision_threshold,
                                                       .candidate_budget = header.candidate_budget,
                                                       .initial_radius = header.initial_radius,
                                                       .radius_growth = header.radius_growth,
                                                       .scan_quantum = header.scan_quantum},
                              .layout = IndexLayout::b_plus_tree};
    ValidateQalsh(metadata_.qalsh, false);
    roots_.resize(CheckedSize(header.num_hash_tables, "QALSH root count is too large"));
    ReadAt(fd_, roots_.data(), CheckedSize(roots_bytes, "QALSH root metadata is too large"),
           header.roots_offset, path_);
    for (std::uint64_t root : roots_) {
        Require(root > 0 && root < header.page_count,
                "QALSH index root page is out of range");
    }
    projection_vectors_.resize(CheckedSize(projection_values, "QALSH projection count is too large"));
    ReadAt(fd_, projection_vectors_.data(), CheckedSize(projection_bytes, "QALSH projection metadata is too large"),
           header.projections_offset, path_);
    for (float value : projection_vectors_) {
        RequireFinite(value, "QALSH index projection vector");
    }
    data_offset_ = header.data_offset;
    page_count_ = header.page_count;
    mapping_size_ = CheckedSize(header.file_size, "QALSH index file is too large for this host");

    const std::size_t page_size = metadata_.page_size;
    const std::size_t leaf_capacity = (page_size - kPageHeaderSize) / sizeof(DiskEntry);
    const std::size_t internal_capacity =
        (page_size - kPageHeaderSize) / (sizeof(std::uint64_t) + sizeof(float));
    Require(leaf_capacity > 0 && internal_capacity >= 2,
            "QALSH index page layout is invalid");

    std::vector<PageHeader> headers(CheckedSize(page_count_, "QALSH page count is too large"));
    std::vector<std::byte> page(page_size);
    for (std::uint64_t page_id = 1; page_id < page_count_; ++page_id) {
        read_page(page_id, page);
        const PageHeader page_header = ReadPageHeader(page);
        Require(page_header.kind == kLeafPage || page_header.kind == kInternalPage,
                "QALSH index contains an invalid page type");
        if (page_header.kind == kLeafPage) {
            const std::uint64_t bytes = CheckedAddOrThrow(
                kPageHeaderSize,
                CheckedMultiplyOrThrow(page_header.count, sizeof(DiskEntry),
                                       "QALSH leaf page size overflows"),
                "QALSH leaf page size overflows");
            Require(page_header.count > 0 && page_header.count <= leaf_capacity && bytes <= page_size,
                    "QALSH leaf page is invalid");
        } else {
            const std::uint64_t children_bytes = CheckedMultiplyOrThrow(
                page_header.count, sizeof(std::uint64_t), "QALSH internal page size overflows");
            const std::uint64_t separator_bytes = page_header.count == 0
                                                       ? 0
                                                       : CheckedMultiplyOrThrow(page_header.count - 1U,
                                                                                sizeof(float),
                                                                                "QALSH internal page size overflows");
            const std::uint64_t bytes = CheckedAddOrThrow(
                CheckedAddOrThrow(kPageHeaderSize, children_bytes,
                                  "QALSH internal page size overflows"),
                separator_bytes, "QALSH internal page size overflows");
            Require(page_header.count >= 2 && page_header.count <= internal_capacity &&
                        bytes <= page_size && page_header.previous == 0 && page_header.next == 0,
                    "QALSH internal page is invalid");
        }
        if (page_header.previous != 0) {
            Require(page_header.previous < page_count_, "QALSH page previous link is out of range");
        }
        if (page_header.next != 0) {
            Require(page_header.next < page_count_, "QALSH page next link is out of range");
        }
        headers[CheckedSize(page_id, "QALSH page id is too large")] = page_header;
    }

    // Validate every reachable tree with an iterative DFS.  In addition to
    // catching cycles, this checks that pages are not shared by tables and
    // that all children at an internal node have the same height.
    std::vector<std::uint8_t> color(headers.size(), 0);
    std::vector<std::uint32_t> owner(headers.size(), std::numeric_limits<std::uint32_t>::max());
    std::vector<std::uint32_t> heights(headers.size(), 0);
    std::vector<float> first_keys(headers.size(), 0.0F);
    std::vector<PointId> first_ids(headers.size(), 0);
    std::vector<float> last_keys(headers.size(), 0.0F);
    std::vector<PointId> last_ids(headers.size(), 0);
    std::vector<std::uint8_t> first_key_known(headers.size(), 0);
    std::vector<std::uint64_t> leaf_entry_counts(roots_.size(), 0);
    std::vector<std::uint32_t> point_marks(
        CheckedSize(metadata_.num_points, "QALSH point count is too large"), 0);
    std::vector<std::uint64_t> table_first_leaf(roots_.size(), 0);
    std::vector<std::uint64_t> table_leaf_count(roots_.size(), 0);

    struct Frame {
        std::uint64_t page_id{0};
        std::uint32_t table_id{0};
        std::vector<std::uint64_t> children;
        std::vector<float> separators;
        std::size_t next_child{0};
    };

    for (std::uint32_t table_id = 0; table_id < roots_.size(); ++table_id) {
        std::vector<Frame> stack;
        stack.push_back(Frame{.page_id = roots_[table_id],
                              .table_id = table_id,
                              .children = {},
                              .separators = {},
                              .next_child = 0});
        while (!stack.empty()) {
            Frame& frame = stack.back();
            const std::size_t frame_index =
                CheckedSize(frame.page_id, "QALSH page id is too large");
            if (color[frame_index] == 0) {
                color[frame_index] = 1;
                owner[frame_index] = table_id;
                const PageHeader page_header = headers[frame_index];
                read_page(frame.page_id, page);
                if (page_header.kind == kLeafPage) {
                    const DiskEntry first = ReadDiskEntry(page, page_header, 0);
                    Require(IsFinite(first.value), "QALSH leaf contains a non-finite key");
                    first_keys[frame_index] = first.value;
                    first_ids[frame_index] = first.point_id;
                    first_key_known[frame_index] = 1;
                    heights[frame_index] = 0;
                    ++table_leaf_count[table_id];
                    if (page_header.previous == 0) {
                        Require(table_first_leaf[table_id] == 0,
                                "QALSH table has multiple first leaves");
                        table_first_leaf[table_id] = frame.page_id;
                    }
                    PointId previous_id = first.point_id;
                    float previous_key = first.value;
                    Require(previous_id < metadata_.num_points,
                            "QALSH leaf contains an invalid point id");
                    const std::size_t mark_index = previous_id;
                    Require(point_marks[mark_index] != table_id + 1U,
                            "QALSH table contains a duplicate point id");
                    point_marks[mark_index] = table_id + 1U;
                    ++leaf_entry_counts[table_id];
                    for (std::uint32_t entry_index = 1; entry_index < page_header.count; ++entry_index) {
                        const DiskEntry entry = ReadDiskEntry(page, page_header, entry_index);
                        Require(IsFinite(entry.value) && entry.point_id < metadata_.num_points,
                                "QALSH leaf contains an invalid entry");
                        Require(previous_id != entry.point_id ||
                                    first.value <= entry.value,
                                "QALSH leaf entries are not ordered");
                        const DiskEntry previous_entry =
                            ReadDiskEntry(page, page_header, entry_index - 1U);
                        Require(previous_entry.value < entry.value ||
                                    (previous_entry.value == entry.value && previous_entry.point_id < entry.point_id),
                                "QALSH leaf entries are not strictly ordered");
                        Require(point_marks[entry.point_id] != table_id + 1U,
                                "QALSH table contains a duplicate point id");
                        point_marks[entry.point_id] = table_id + 1U;
                        previous_id = entry.point_id;
                        previous_key = entry.value;
                        ++leaf_entry_counts[table_id];
                    }
                    last_keys[frame_index] = previous_key;
                    last_ids[frame_index] = previous_id;
                    color[frame_index] = 2;
                    stack.pop_back();
                    continue;
                }

                frame.children.reserve(page_header.count);
                for (std::uint32_t child_index = 0; child_index < page_header.count; ++child_index) {
                    const std::uint64_t child = ReadChild(page, page_header, child_index);
                    Require(child > 0 && child < page_count_,
                            "QALSH child page id is out of range");
                    frame.children.push_back(child);
                }
                frame.separators.reserve(page_header.count - 1U);
                for (std::uint32_t separator_index = 0; separator_index + 1U < page_header.count;
                     ++separator_index) {
                    const float separator = ReadSeparator(page, page_header, separator_index);
                    Require(IsFinite(separator), "QALSH internal node has a non-finite separator");
                    if (!frame.separators.empty()) {
                        Require(frame.separators.back() <= separator,
                                "QALSH internal separators are not ordered");
                    }
                    frame.separators.push_back(separator);
                }
            }

            if (frame.next_child < frame.children.size()) {
                const std::uint64_t child = frame.children[frame.next_child++];
                const std::size_t child_index = CheckedSize(child, "QALSH child page id is too large");
                Require(color[child_index] == 0,
                        "QALSH tree contains a cycle or a shared child page");
                stack.push_back(Frame{.page_id = child,
                                      .table_id = table_id,
                                      .children = {},
                                      .separators = {},
                                      .next_child = 0});
                continue;
            }

            std::uint32_t child_height = 0;
            float child_first = 0.0F;
            PointId child_first_id = 0;
            float child_last = 0.0F;
            PointId child_last_id = 0;
            bool child_height_set = false;
            for (std::size_t child_index = 0; child_index < frame.children.size(); ++child_index) {
                const std::size_t child_page_index =
                    CheckedSize(frame.children[child_index], "QALSH child page id is too large");
                Require(color[child_page_index] == 2 && owner[child_page_index] == table_id,
                        "QALSH internal child is not reachable exactly once");
                if (!child_height_set) {
                    child_height = heights[child_page_index];
                    child_height_set = true;
                    child_first = first_keys[child_page_index];
                    child_first_id = first_ids[child_page_index];
                } else {
                    Require(heights[child_page_index] == child_height,
                            "QALSH internal node has children at different heights");
                    Require(KeyLess(last_keys[CheckedSize(frame.children[child_index - 1U],
                                                          "QALSH child page id is too large")],
                                     last_ids[CheckedSize(frame.children[child_index - 1U],
                                                          "QALSH child page id is too large")],
                                     first_keys[child_page_index], first_ids[child_page_index]),
                            "QALSH internal child ranges are out of order");
                }
                child_last = last_keys[child_page_index];
                child_last_id = last_ids[child_page_index];
                if (child_index > 0) {
                    Require(frame.separators[child_index - 1U] == first_keys[child_page_index],
                            "QALSH internal separator does not match its child");
                }
            }
            first_keys[frame_index] = child_first;
            first_ids[frame_index] = child_first_id;
            last_keys[frame_index] = child_last;
            last_ids[frame_index] = child_last_id;
            first_key_known[frame_index] = 1;
            heights[frame_index] = child_height + 1U;
            color[frame_index] = 2;
            stack.pop_back();
        }
        Require(leaf_entry_counts[table_id] == metadata_.num_points,
                "QALSH table does not contain exactly num_points entries");
    }

    for (std::size_t page_id = 1; page_id < headers.size(); ++page_id) {
        Require(color[page_id] == 2 && first_key_known[page_id] != 0,
                "QALSH index contains an unreachable page");
        const PageHeader page_header = headers[page_id];
        if (page_header.kind != kLeafPage) {
            continue;
        }
        if (page_header.previous != 0) {
            const PageHeader previous = headers[CheckedSize(page_header.previous, "QALSH page id is too large")];
            Require(previous.kind == kLeafPage && previous.next == page_id,
                    "QALSH leaf previous link is not reciprocal");
            Require(owner[CheckedSize(page_header.previous, "QALSH page id is too large")] == owner[page_id],
                    "QALSH leaf link crosses projection tables");
        }
        if (page_header.next != 0) {
            const PageHeader next = headers[CheckedSize(page_header.next, "QALSH page id is too large")];
            Require(next.kind == kLeafPage && next.previous == page_id,
                    "QALSH leaf next link is not reciprocal");
            Require(owner[CheckedSize(page_header.next, "QALSH page id is too large")] == owner[page_id],
                    "QALSH leaf link crosses projection tables");
        }
    }
    leaf_ranks_.assign(headers.size(), std::numeric_limits<std::uint64_t>::max());
    for (std::uint32_t table_id = 0; table_id < roots_.size(); ++table_id) {
        Require(table_first_leaf[table_id] != 0, "QALSH table has no first leaf");
        std::uint64_t current = table_first_leaf[table_id];
        std::uint64_t visited = 0;
        bool have_previous_leaf = false;
        float previous_leaf_last_key = 0.0F;
        PointId previous_leaf_last_id = 0;
        while (current != 0) {
            Require(current < page_count_, "QALSH leaf link is out of range");
            const std::size_t current_index = CheckedSize(current, "QALSH page id is too large");
            Require(headers[current_index].kind == kLeafPage && owner[current_index] == table_id,
                    "QALSH leaf chain is invalid");
            if (have_previous_leaf) {
                Require(previous_leaf_last_key < first_keys[current_index] ||
                            (previous_leaf_last_key == first_keys[current_index] &&
                             previous_leaf_last_id < first_ids[current_index]),
                        "QALSH leaf chain is not sorted");
            }
            ++visited;
            Require(visited <= table_leaf_count[table_id], "QALSH leaf chain contains a cycle");
            Require(leaf_ranks_[current_index] == std::numeric_limits<std::uint64_t>::max(),
                    "QALSH leaf belongs to multiple projection chains");
            leaf_ranks_[current_index] = visited - 1U;
            previous_leaf_last_key = last_keys[current_index];
            previous_leaf_last_id = last_ids[current_index];
            have_previous_leaf = true;
            current = headers[current_index].next;
        }
        Require(visited == table_leaf_count[table_id],
                "QALSH leaf chain does not cover the table");
    }

    page_headers_.resize(headers.size());
    for (std::size_t page_id = 0; page_id < headers.size(); ++page_id) {
        const PageHeader& page_header = headers[page_id];
        page_headers_[page_id] = CachedPageHeader{.kind = page_header.kind,
                                                   .count = page_header.count,
                                                   .previous = page_header.previous,
                                                   .next = page_header.next};
    }
}

void BPlusTreeIndex::read_page(std::uint64_t page_id, std::vector<std::byte>& page) const {
    Require(page_id > 0 && page_id < page_count_, "QALSH page id is out of range");
    page.resize(metadata_.page_size);
    const std::uint64_t page_offset = CheckedMultiplyOrThrow(
        page_id, metadata_.page_size, "QALSH page offset overflows");
    const std::uint64_t offset = CheckedAddOrThrow(
        data_offset_, page_offset, "QALSH page offset overflows");
    Require(offset <= mapping_size_ && page.size() <= mapping_size_ - static_cast<std::size_t>(offset),
            "QALSH page exceeds the index file");
    if (mapping_ != nullptr) {
        std::memcpy(page.data(), mapping_ + static_cast<std::size_t>(offset), page.size());
    } else {
        ReadAt(fd_, page.data(), page.size(), offset, path_);
    }
}

const IndexMetadata& BPlusTreeIndex::metadata() const noexcept { return metadata_; }
std::span<const Coordinate> BPlusTreeIndex::projection_vectors() const noexcept {
    return projection_vectors_;
}
const std::string& BPlusTreeIndex::path() const noexcept { return path_; }

std::unique_ptr<ProjectionIndex::Cursor> BPlusTreeIndex::make_cursor(std::uint32_t table_id,
                                                                     Projection query_value) const {
    auto cursor = std::make_unique<DiskCursor>();
    cursor->owner = this;
    reset_cursor(*cursor, table_id, query_value);
    return cursor;
}

void BPlusTreeIndex::reset_cursor(Cursor& base_cursor, std::uint32_t table_id,
                                  Projection query_value) const {
    Require(table_id < roots_.size(), "projection table id is out of range");
    RequireFinite(query_value, "projection query value");
    auto* cursor = dynamic_cast<DiskCursor*>(&base_cursor);
    if (cursor == nullptr || cursor->owner != this) {
        throw std::invalid_argument("cursor does not belong to this B+ tree index");
    }

    const ProjectionIndex* owner = cursor->owner;
    *cursor = DiskCursor{};
    cursor->owner = owner;
    cursor->table_id = table_id;
    cursor->query_value = query_value;

    std::vector<std::byte> page;
    std::span<const std::byte> page_view;
    std::uint64_t loaded_page = 0;
    bool page_loaded = false;
    auto load_page = [&](std::uint64_t page_id) {
        Require(page_id > 0 && page_id < page_count_, "QALSH cursor page is out of range");
        if (page_loaded && loaded_page == page_id) {
            return;
        }
        const std::uint64_t page_offset = CheckedAddOrThrow(
            data_offset_,
            CheckedMultiplyOrThrow(page_id, metadata_.page_size,
                                   "QALSH cursor page offset overflows"),
            "QALSH cursor page offset overflows");
        Require(page_offset <= mapping_size_ &&
                    metadata_.page_size <= mapping_size_ - static_cast<std::size_t>(page_offset),
                "QALSH cursor page exceeds the index file");
        if (mapping_ != nullptr) {
            page_view = std::span<const std::byte>(
                mapping_ + static_cast<std::size_t>(page_offset), metadata_.page_size);
        } else {
            read_page(page_id, page);
            page_view = page;
        }
        loaded_page = page_id;
        page_loaded = true;
    };
    std::uint64_t current_page = roots_[table_id];
    std::uint64_t descent_steps = 0;
    while (true) {
        Require(descent_steps++ < page_count_, "QALSH tree descent encountered a cycle");
        load_page(current_page);
        const auto cached_header = page_headers_[CheckedSize(current_page, "QALSH page id is too large")];
        const PageHeader header{.kind = cached_header.kind,
                                .count = cached_header.count,
                                .previous = cached_header.previous,
                                .next = cached_header.next};
        if (header.kind == kLeafPage) {
            Require(header.count > 0, "QALSH cursor reached an empty leaf");
            std::uint32_t lo = 0;
            std::uint32_t hi = header.count;
            while (lo < hi) {
                const std::uint32_t middle = lo + (hi - lo) / 2U;
                if (ReadDiskEntry(page_view, header, middle).value < query_value) {
                    lo = middle + 1U;
                } else {
                    hi = middle;
                }
            }
            if (lo > 0) {
                cursor->left_page = current_page;
                cursor->left_leaf_rank =
                    leaf_ranks_[CheckedSize(current_page, "QALSH leaf page id is too large")];
                cursor->left_index = lo - 1U;
                cursor->left_active = true;
            } else if (header.previous != 0) {
                cursor->left_page = header.previous;
                cursor->left_leaf_rank =
                    leaf_ranks_[CheckedSize(header.previous, "QALSH leaf page id is too large")];
                load_page(header.previous);
                const auto cached_previous_header =
                    page_headers_[CheckedSize(header.previous, "QALSH page id is too large")];
                const PageHeader previous_header{.kind = cached_previous_header.kind,
                                                 .count = cached_previous_header.count,
                                                 .previous = cached_previous_header.previous,
                                                 .next = cached_previous_header.next};
                Require(previous_header.kind == kLeafPage && previous_header.count > 0,
                        "QALSH leaf previous link is invalid");
                cursor->left_index = previous_header.count - 1U;
                cursor->left_active = true;
            }
            if (lo < header.count) {
                cursor->right_page = current_page;
                cursor->right_leaf_rank =
                    leaf_ranks_[CheckedSize(current_page, "QALSH leaf page id is too large")];
                cursor->right_index = lo;
                cursor->right_active = true;
            } else if (header.next != 0) {
                cursor->right_page = header.next;
                cursor->right_leaf_rank =
                    leaf_ranks_[CheckedSize(header.next, "QALSH leaf page id is too large")];
                cursor->right_index = 0;
                cursor->right_active = true;
            }
            return;
        }
        Require(header.kind == kInternalPage && header.count >= 2,
                "QALSH tree contains an invalid internal node");
        std::uint32_t child_index = 0;
        // Separators are the first key of each child.  Select the first child
        // whose key range can contain the lower bound; using a strict
        // comparison keeps equal projection values in the leftmost child so
        // the outward cursor order matches the array backend.
        while (child_index + 1U < header.count &&
               query_value > ReadSeparator(page_view, header, child_index)) {
            ++child_index;
        }
        current_page = ReadChild(page_view, header, child_index);
        Require(current_page > 0 && current_page < page_count_,
                "QALSH child page id is out of range");
    }
}

ScanReport BPlusTreeIndex::scan(Cursor& base_cursor, float bound, std::size_t max_entries,
                                void* context, HitCallback callback, ScanScope scope) const {
    Require(callback != nullptr, "projection hit callback is required");
    Require(max_entries > 0, "scan quantum must be positive");
    RequireFinite(bound, "scan bound");
    Require(bound >= 0.0F, "scan bound must not be negative");
    const auto* cursor = dynamic_cast<DiskCursor*>(&base_cursor);
    Require(cursor != nullptr && cursor->owner == this,
            "cursor does not belong to this B+ tree index");
    return scan_impl(base_cursor, bound, max_entries,
                     [&](const detail::HitRange& range) {
                         detail::ForEachHit(range, [&](const ProjectionHit& hit) { callback(context, hit); });
                     }, scope);
}

template <typename Visitor>
ScanReport BPlusTreeIndex::scan_impl(Cursor& base_cursor, float bound, std::size_t max_entries,
                                     Visitor&& visitor, ScanScope scope) const {
    Require(scope == ScanScope::quantum || scope == ScanScope::range,
            "invalid projection scan scope");
    // Same owned-cursor precondition as the in-memory private scanner.
    auto* cursor = static_cast<DiskCursor*>(&base_cursor);
    ResetBoundedSides(bound, cursor->last_bound, cursor->left_blocked, cursor->right_blocked);

    std::vector<std::byte> page;
    std::span<const std::byte> page_view;
    const CachedPageHeader* loaded_header = nullptr;
    std::uint64_t loaded_page = 0;
    bool loaded = false;
    const auto load_page = [&](std::uint64_t page_id) -> std::span<const std::byte> {
        Require(page_id > 0 && page_id < page_count_, "QALSH cursor page is out of range");
        if (!loaded || loaded_page != page_id) {
            if (mapping_ != nullptr && cursor->cached_page == page_id) {
                page_view = std::span<const std::byte>(cursor->cached_page_data,
                                                       metadata_.page_size);
            } else if (mapping_ != nullptr) {
                const std::uint64_t page_offset = CheckedAddOrThrow(
                    data_offset_,
                    CheckedMultiplyOrThrow(page_id, metadata_.page_size,
                                           "QALSH scan page offset overflows"),
                    "QALSH scan page offset overflows");
                Require(page_offset <= mapping_size_ &&
                            metadata_.page_size <= mapping_size_ - static_cast<std::size_t>(page_offset),
                        "QALSH scan page exceeds the index file");
                cursor->cached_page = page_id;
                cursor->cached_page_data = mapping_ + static_cast<std::size_t>(page_offset);
                page_view = std::span<const std::byte>(cursor->cached_page_data,
                                                       metadata_.page_size);
            } else {
                read_page(page_id, page);
                page_view = page;
            }
            loaded_header = &page_headers_[CheckedSize(page_id, "QALSH page id is too large")];
            loaded_page = page_id;
            loaded = true;
        }
        return page_view;
    };

    ScanReport report{.table_id = cursor->table_id};
    const std::uint32_t table_id = cursor->table_id;
    const Projection query_value = cursor->query_value;
    std::uint64_t left_page = cursor->left_page;
    std::uint64_t left_leaf_rank = cursor->left_leaf_rank;
    std::uint64_t right_page = cursor->right_page;
    std::uint64_t right_leaf_rank = cursor->right_leaf_rank;
    std::uint32_t left_index = cursor->left_index;
    std::uint32_t right_index = cursor->right_index;
    bool left_active = cursor->left_active;
    bool right_active = cursor->right_active;
    bool left_blocked = cursor->left_blocked;
    bool right_blocked = cursor->right_blocked;
    std::size_t entries_visited = 0;
    const std::size_t left_limit = scope == ScanScope::range
                                       ? max_entries
                                       : max_entries / 2U + max_entries % 2U;
    std::size_t right_limit = scope == ScanScope::range ? max_entries : max_entries / 2U;

    const auto sync_cursor = [&] {
        cursor->left_page = left_page;
        cursor->left_leaf_rank = left_leaf_rank;
        cursor->right_page = right_page;
        cursor->right_leaf_rank = right_leaf_rank;
        cursor->left_index = left_index;
        cursor->right_index = right_index;
        cursor->left_active = left_active;
        cursor->right_active = right_active;
        cursor->left_blocked = left_blocked;
        cursor->right_blocked = right_blocked;
    };
    // The validated leaf chain and lower_bound partition put the positions
    // on opposite sides of the query. They move outward and cannot cross.

    const auto scan_left = [&] {
        if (!left_active || left_blocked) return;
        (void)load_page(left_page);
        Require(loaded_header->kind == kLeafPage && left_index < loaded_header->count,
                "QALSH left cursor is invalid");
        const std::byte* entry_data = page_view.data() + kPageHeaderSize;
        std::size_t scanned = 0;
        while (scanned < left_limit && left_active && !left_blocked) {
            const CachedPageHeader& header = *loaded_header;
            const auto available = std::min(left_limit - scanned,
                                            static_cast<std::size_t>(left_index) + 1);
            detail::HitRange range{entry_data + static_cast<std::size_t>(left_index) * sizeof(DiskEntry),
                                   available, true, table_id, query_value};
            ClampToWindow(range, bound);
            left_blocked = range.count < available;
            if (range.count != 0) visitor(range);
            scanned += range.count;
            entries_visited += range.count;
            if (range.count <= left_index) {
                left_index -= static_cast<std::uint32_t>(range.count);
            } else if (header.previous != 0) {
                left_page = header.previous;
                left_leaf_rank = leaf_ranks_[CheckedSize(header.previous,
                                                         "QALSH leaf page id is too large")];
                (void)load_page(left_page);
                entry_data = page_view.data() + kPageHeaderSize;
                const CachedPageHeader& previous_header = *loaded_header;
                Require(previous_header.kind == kLeafPage && previous_header.count > 0,
                        "QALSH left leaf link is invalid");
                left_index = previous_header.count - 1U;
                if (scope == ScanScope::range) {
                    break;
                }
            } else {
                left_active = false;
            }
        }
    };

    const auto scan_right = [&] {
        if (!right_active || right_blocked) return;
        (void)load_page(right_page);
        Require(loaded_header->kind == kLeafPage && right_index < loaded_header->count,
                "QALSH right cursor is invalid");
        const std::byte* entry_data = page_view.data() + kPageHeaderSize;
        std::size_t scanned = 0;
        while (scanned < right_limit && right_active && !right_blocked) {
            const CachedPageHeader& header = *loaded_header;
            const auto available = std::min(right_limit - scanned,
                                            static_cast<std::size_t>(header.count - right_index));
            detail::HitRange range{entry_data + static_cast<std::size_t>(right_index) * sizeof(DiskEntry),
                                   available, false, table_id, query_value};
            ClampToWindow(range, bound);
            right_blocked = range.count < available;
            if (range.count != 0) visitor(range);
            scanned += range.count;
            entries_visited += range.count;
            if (range.count < header.count - right_index) {
                right_index += static_cast<std::uint32_t>(range.count);
            } else if (header.next != 0) {
                right_page = header.next;
                right_leaf_rank = leaf_ranks_[CheckedSize(header.next,
                                                          "QALSH leaf page id is too large")];
                right_index = 0;
                (void)load_page(right_page);
                Require(loaded_header->kind == kLeafPage && loaded_header->count > 0,
                        "QALSH right leaf link is invalid");
                entry_data = page_view.data() + kPageHeaderSize;
                if (scope == ScanScope::range) {
                    break;
                }
            } else {
                right_active = false;
            }
        }
    };

    if (left_active && !left_blocked) {
        scan_left();
    }
    if (left_active || right_active) {
        if (scope == ScanScope::quantum && right_limit == 0) {
            right_limit = max_entries - entries_visited;
        }
        scan_right();
    }

    sync_cursor();
    report.entries_visited = entries_visited;
    report.table_exhausted = !left_active && !right_active;
    report.window_exhausted = (!left_active || left_blocked) &&
                              (!right_active || right_blocked);
    return report;
}

SortedArrayIndex::SortedArrayIndex(int fd, std::string path)
    : fd_(fd), path_(std::move(path)) {}

SortedArrayIndex::~SortedArrayIndex() {
    if (mapping_ != nullptr && mapping_size_ > 0) {
        (void)::munmap(const_cast<std::byte*>(mapping_),
                       static_cast<std::size_t>(mapping_size_));
    }
    if (fd_ >= 0) {
        (void)::close(fd_);
    }
}

std::shared_ptr<SortedArrayIndex> SortedArrayIndex::Open(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        throw std::runtime_error("failed to open sorted-array index " + path + ": " +
                                 std::strerror(errno));
    }
    try {
        struct stat stat_result {};
        if (::fstat(fd, &stat_result) != 0 || stat_result.st_size < 0) {
            throw std::runtime_error("failed to stat sorted-array index " + path + ": " +
                                     std::strerror(errno));
        }
        Require(static_cast<std::uintmax_t>(stat_result.st_size) <=
                    static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()),
                "sorted-array index is too large for this host");
        if (stat_result.st_size < static_cast<off_t>(sizeof(DiskFileHeader))) {
            throw std::runtime_error("sorted-array index is missing or truncated: " + path);
        }
        DiskFileHeader envelope{};
        ReadAt(fd, &envelope, sizeof(envelope), 0, path);
        ValidateSortedArrayFileHeaderEnvelope(
            envelope, static_cast<std::uint64_t>(stat_result.st_size));
        auto index = std::shared_ptr<SortedArrayIndex>(
            new SortedArrayIndex(fd, path));
        fd = -1;
        index->mapping_size_ = envelope.file_size;
        void* mapping = ::mmap(nullptr, static_cast<std::size_t>(index->mapping_size_),
                               PROT_READ, MAP_PRIVATE, index->fd_, 0);
        if (mapping == MAP_FAILED) {
            throw std::runtime_error("failed to map sorted-array index " + path + ": " +
                                     std::strerror(errno));
        }
        index->mapping_ = static_cast<const std::byte*>(mapping);
        index->load_header();
        return index;
    } catch (...) {
        if (fd >= 0) {
            (void)::close(fd);
        }
        throw;
    }
}

void SortedArrayIndex::load_header() {
    DiskFileHeader header{};
    ReadAt(fd_, &header, sizeof(header), 0, path_);
    ValidateSortedArrayFileHeaderEnvelope(header, mapping_size_);
    metadata_ = IndexMetadata{.metric = static_cast<Metric>(header.metric),
                              .num_points = header.num_points,
                              .num_dimensions = header.num_dimensions,
                              .num_hash_tables = header.num_hash_tables,
                              .page_size = header.page_size,
                              .seed = header.seed,
                              .file_size = header.file_size,
                              .qalsh = QalshParameters{.approximation_ratio = header.approximation_ratio,
                                                       .bucket_width = header.bucket_width,
                                                       .error_probability = header.error_probability,
                                                       .num_hash_tables = header.num_hash_tables,
                                                       .collision_threshold = header.collision_threshold,
                                                       .candidate_budget = header.candidate_budget,
                                                       .initial_radius = header.initial_radius,
                                                       .radius_growth = header.radius_growth,
                                                       .scan_quantum = header.scan_quantum},
                              .layout = IndexLayout::sorted_array};
    ValidateQalsh(metadata_.qalsh, false);

    const std::uint64_t roots_bytes = CheckedMultiplyOrThrow(
        header.num_hash_tables, sizeof(std::uint64_t),
        "QALSH sorted-array table metadata overflows");
    const std::uint64_t projection_values = CheckedMultiplyOrThrow(
        header.num_hash_tables, header.num_dimensions,
        "QALSH sorted-array projection metadata overflows");
    const std::uint64_t projection_bytes = CheckedMultiplyOrThrow(
        projection_values, sizeof(Coordinate),
        "QALSH sorted-array projection metadata overflows");
    table_offsets_.resize(CheckedSize(header.num_hash_tables,
                                      "QALSH sorted-array table count is too large"));
    ReadAt(fd_, table_offsets_.data(), CheckedSize(roots_bytes,
           "QALSH sorted-array table metadata is too large"), header.roots_offset, path_);
    projection_vectors_.resize(CheckedSize(projection_values,
                                             "QALSH sorted-array projection count is too large"));
    ReadAt(fd_, projection_vectors_.data(), CheckedSize(projection_bytes,
           "QALSH sorted-array projection metadata is too large"),
           header.projections_offset, path_);
    for (float value : projection_vectors_) {
        RequireFinite(value, "QALSH sorted-array projection vector");
    }

    data_offset_ = header.data_offset;
    const std::uint64_t table_bytes = CheckedMultiplyOrThrow(
        header.num_points, sizeof(DiskEntry),
        "QALSH sorted-array table size overflows");
    std::uint64_t expected_offset = data_offset_;
    std::vector<std::uint32_t> point_marks(
        CheckedSize(header.num_points, "QALSH point count is too large"), 0);
    for (std::uint32_t table_id = 0; table_id < header.num_hash_tables; ++table_id) {
        const std::uint64_t table_offset = table_offsets_[table_id];
        Require(table_offset == expected_offset,
                "QALSH sorted-array table offsets are not contiguous");
        const std::uint64_t table_end = CheckedAddOrThrow(
            table_offset, table_bytes, "QALSH sorted-array table extent overflows");
        Require(table_end <= header.file_size,
                "QALSH sorted-array table exceeds the index file");
        const auto* data = mapping_ + CheckedSize(table_offset,
                                                   "QALSH sorted-array table offset is too large");
        DiskEntry previous{};
        for (std::size_t entry_index = 0; entry_index < header.num_points; ++entry_index) {
            const DiskEntry entry = ReadArrayEntry(data, CheckedSize(table_bytes,
                                                                       "QALSH sorted-array table is too large"),
                                                    entry_index);
            Require(IsFinite(entry.value) && entry.point_id < header.num_points,
                    "QALSH sorted-array table contains an invalid entry");
            if (entry_index != 0) {
                Require(KeyLess(previous.value, previous.point_id,
                                entry.value, entry.point_id),
                        "QALSH sorted-array table entries are not strictly ordered");
            }
            Require(point_marks[entry.point_id] != table_id + 1U,
                    "QALSH sorted-array table contains a duplicate point id");
            point_marks[entry.point_id] = table_id + 1U;
            previous = entry;
        }
        for (std::uint32_t point_id = 0; point_id < header.num_points; ++point_id) {
            Require(point_marks[point_id] == table_id + 1U,
                    "QALSH sorted-array table does not contain every point exactly once");
        }
        expected_offset = table_end;
    }
    Require(expected_offset == header.file_size,
            "QALSH sorted-array file contains an unexpected data tail");
}

const IndexMetadata& SortedArrayIndex::metadata() const noexcept { return metadata_; }

std::span<const Coordinate> SortedArrayIndex::projection_vectors() const noexcept {
    return projection_vectors_;
}

const std::string& SortedArrayIndex::path() const noexcept { return path_; }

std::unique_ptr<ProjectionIndex::Cursor> SortedArrayIndex::make_cursor(
    std::uint32_t table_id, Projection query_value) const {
    auto cursor = std::make_unique<ArrayCursor>();
    cursor->owner = this;
    reset_cursor(*cursor, table_id, query_value);
    return cursor;
}

void SortedArrayIndex::reset_cursor(Cursor& base_cursor, std::uint32_t table_id,
                                    Projection query_value) const {
    Require(table_id < table_offsets_.size(),
            "projection table id is out of range");
    RequireFinite(query_value, "projection query value");
    auto* cursor = dynamic_cast<ArrayCursor*>(&base_cursor);
    if (cursor == nullptr || cursor->owner != this) {
        throw std::invalid_argument("cursor does not belong to this sorted-array index");
    }
    const std::size_t count = metadata_.num_points;
    const std::uint64_t table_bytes = CheckedMultiplyOrThrow(
        count, sizeof(DiskEntry), "QALSH sorted-array table size overflows");
    const auto* data = mapping_ + CheckedSize(table_offsets_[table_id],
                                               "QALSH sorted-array table offset is too large");
    std::size_t lo = 0;
    std::size_t hi = count;
    while (lo < hi) {
        const std::size_t middle = lo + (hi - lo) / 2U;
        if (ReadArrayEntry(data, CheckedSize(table_bytes,
                                              "QALSH sorted-array table is too large"),
                           middle).value < query_value) {
            lo = middle + 1U;
        } else {
            hi = middle;
        }
    }
    const ProjectionIndex* owner = cursor->owner;
    *cursor = ArrayCursor{};
    cursor->owner = owner;
    cursor->table_id = table_id;
    cursor->query_value = query_value;
    cursor->left = lo == 0 ? ArrayCursor::kNoIndex : lo - 1U;
    cursor->right = lo == count ? ArrayCursor::kNoIndex : lo;
}

ScanReport SortedArrayIndex::scan(Cursor& base_cursor, float bound,
                                  std::size_t max_entries, void* context,
                                  HitCallback callback, ScanScope scope) const {
    Require(callback != nullptr, "projection hit callback is required");
    Require(max_entries > 0, "scan quantum must be positive");
    RequireFinite(bound, "scan bound");
    Require(bound >= 0.0F, "scan bound must not be negative");
    const auto* cursor = dynamic_cast<ArrayCursor*>(&base_cursor);
    Require(cursor != nullptr && cursor->owner == this,
            "cursor does not belong to this sorted-array index");
    return scan_impl(base_cursor, bound, max_entries,
                     [&](const detail::HitRange& range) {
                         detail::ForEachHit(range, [&](const ProjectionHit& hit) {
                             callback(context, hit);
                         });
                     }, scope);
}

template <typename Visitor>
ScanReport SortedArrayIndex::scan_impl(Cursor& base_cursor, float bound,
                                       std::size_t max_entries, Visitor&& visitor,
                                       ScanScope scope) const {
    Require(scope == ScanScope::quantum || scope == ScanScope::range,
            "invalid projection scan scope");
    auto* cursor = static_cast<ArrayCursor*>(&base_cursor);
    ArrayCursor position = *cursor;
    struct CommitPosition {
        ArrayCursor* destination;
        const ArrayCursor& position;
        ~CommitPosition() { *destination = position; }
    } commit{cursor, position};
    cursor = &position;

    const std::size_t entry_count = metadata_.num_points;
    const auto* data = mapping_ + CheckedSize(table_offsets_[cursor->table_id],
                                               "QALSH sorted-array table offset is too large");
    const std::size_t region_capacity = static_cast<std::size_t>(
        (static_cast<std::uint64_t>(metadata_.page_size) - kPageHeaderSize) /
        sizeof(DiskEntry));
    Require(region_capacity > 0, "QALSH sorted-array region cannot hold an entry");
    ResetBoundedSides(bound, cursor->last_bound, cursor->left_blocked,
                      cursor->right_blocked);

    ScanReport report{.table_id = cursor->table_id};
    std::size_t entries_visited = 0;
    const std::size_t left_limit = scope == ScanScope::range
                                       ? max_entries
                                       : max_entries / 2U + max_entries % 2U;
    std::size_t right_limit = scope == ScanScope::range ? max_entries : max_entries / 2U;

    const auto scan_left = [&] {
        std::size_t scanned = 0;
        while (scanned < left_limit && cursor->left != ArrayCursor::kNoIndex &&
               !cursor->left_blocked) {
            const std::size_t index = cursor->left;
            const std::size_t region_start = (index / region_capacity) * region_capacity;
            const std::size_t region_available = index - region_start + 1U;
            const std::size_t available = std::min(
                left_limit - scanned,
                scope == ScanScope::range ? region_available : index + 1U);
            detail::HitRange range{data + index * sizeof(DiskEntry), available, true,
                                   cursor->table_id, cursor->query_value};
            ClampToWindow(range, bound);
            cursor->left_blocked = range.count < available;
            if (range.count != 0) visitor(range);
            entries_visited += range.count;
            scanned += range.count;
            const bool finished_region = scope == ScanScope::range &&
                                         range.count == available &&
                                         available == region_available;
            cursor->left = range.count == index + 1U
                               ? ArrayCursor::kNoIndex
                               : index - range.count;
            if (finished_region) {
                break;
            }
        }
    };

    const auto scan_right = [&] {
        std::size_t scanned = 0;
        while (scanned < right_limit && cursor->right != ArrayCursor::kNoIndex &&
               !cursor->right_blocked) {
            const std::size_t index = cursor->right;
            const std::size_t region_start = (index / region_capacity) * region_capacity;
            const std::size_t region_end = std::min(entry_count, region_start + region_capacity);
            const std::size_t region_available = region_end - index;
            const std::size_t available = std::min(
                right_limit - scanned,
                scope == ScanScope::range ? region_available : entry_count - index);
            detail::HitRange range{data + index * sizeof(DiskEntry), available, false,
                                   cursor->table_id, cursor->query_value};
            ClampToWindow(range, bound);
            cursor->right_blocked = range.count < available;
            if (range.count != 0) visitor(range);
            entries_visited += range.count;
            scanned += range.count;
            const bool finished_region = scope == ScanScope::range &&
                                         range.count == available &&
                                         available == region_available;
            cursor->right = index + range.count == entry_count
                                ? ArrayCursor::kNoIndex
                                : index + range.count;
            if (finished_region) {
                break;
            }
        }
    };

    if (cursor->left != ArrayCursor::kNoIndex && !cursor->left_blocked) {
        scan_left();
    }
    if (cursor->left != ArrayCursor::kNoIndex || cursor->right != ArrayCursor::kNoIndex) {
        if (scope == ScanScope::quantum && right_limit == 0U) {
            right_limit = max_entries - entries_visited;
        }
        scan_right();
    }

    report.entries_visited = entries_visited;
    report.table_exhausted = cursor->left == ArrayCursor::kNoIndex &&
                             cursor->right == ArrayCursor::kNoIndex;
    report.window_exhausted =
        (cursor->left == ArrayCursor::kNoIndex || cursor->left_blocked) &&
        (cursor->right == ArrayCursor::kNoIndex || cursor->right_blocked);
    return report;
}

void PersistentIndex::Build(const std::string& path, IndexConfig config,
                            const PointAccessor& accessor,
                            PersistentBuildOptions options) {
    switch (options.layout) {
        case IndexLayout::b_plus_tree:
            BPlusTreeIndex::Build(path, std::move(config), accessor, options.page_size,
                                  options.overwrite);
            return;
        case IndexLayout::sorted_array:
            SortedArrayIndex::Build(path, std::move(config), accessor, options.page_size,
                                    options.overwrite);
            return;
        case IndexLayout::in_memory:
            throw std::invalid_argument("in-memory layout cannot be persisted");
    }
    throw std::invalid_argument("unsupported persistent index layout");
}

std::shared_ptr<ProjectionIndex> PersistentIndex::Open(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        throw std::runtime_error("failed to open persistent index " + path + ": " +
                                 std::strerror(errno));
    }
    std::array<char, 8> magic{};
    try {
        ReadAt(fd, magic.data(), magic.size(), 0, path);
    } catch (...) {
        (void)::close(fd);
        throw;
    }
    (void)::close(fd);
    if (std::equal(kBPlusMagic.begin(), kBPlusMagic.end(), magic.begin())) {
        return BPlusTreeIndex::Open(path);
    }
    if (std::equal(kSortedArrayMagic.begin(), kSortedArrayMagic.end(), magic.begin())) {
        return SortedArrayIndex::Open(path);
    }
    throw std::invalid_argument("persistent index has an unsupported QALSH layout header");
}

Distance L1Distance(PointView lhs, PointView rhs) {
    ValidateDistanceOperands(lhs, rhs);
    return DistanceKernel<Metric::l1>(lhs, rhs, std::numeric_limits<float>::infinity()).distance;
}

Distance L2Distance(PointView lhs, PointView rhs) {
    ValidateDistanceOperands(lhs, rhs);
    return DistanceKernel<Metric::l2>(lhs, rhs, std::numeric_limits<float>::infinity()).distance;
}

Distance DistanceValue(PointView lhs, PointView rhs, Metric metric) {
    Require(IsValidMetric(metric), "unsupported metric");
    if (metric == Metric::l1) {
        return L1Distance(lhs, rhs);
    }
    return L2Distance(lhs, rhs);
}

BoundedDistance BoundedDistanceValue(PointView lhs, PointView rhs, Metric metric,
                                     Distance upper_bound) {
    ValidateDistanceOperands(lhs, rhs);
    Require(IsValidMetric(metric), "unsupported metric");
    Require(IsFinite(upper_bound) || std::isinf(upper_bound),
            "distance upper bound must be finite or infinity");
    Require(upper_bound >= 0.0F, "distance upper bound must not be negative");
    // The shared kernel keeps bounded L2 conservative at a rounded equality
    // and is also used directly by the search engine.
    return BoundedDistanceValueUnchecked(lhs, rhs, metric, upper_bound);
}

void TableScanSchedule::start(std::uint32_t table_count) {
    table_count_ = table_count;
    exhausted_tables_ = 0;
    completed_tables_ = 0;
    table_exhausted_.assign(table_count_, 0);
    table_completed_.assign(table_count_, 0);
    pending_tables_.clear();
    for (std::uint32_t table = 0; table < table_count_; ++table) {
        pending_tables_.push_back(table);
    }
}

void TableScanSchedule::on_scan_boundary(const ScanBoundaryEvent& event) {
    Require(event.report.table_id < table_count_,
            "scan boundary references an invalid projection table");
    const std::uint32_t table = event.report.table_id;
    if (event.report.table_exhausted && table_exhausted_[table] == 0) {
        table_exhausted_[table] = 1;
        ++exhausted_tables_;
    }
    const bool completed_window = event.report.window_exhausted || event.report.table_exhausted;
    if (completed_window) {
        if (table_completed_[table] == 0) {
            table_completed_[table] = 1;
            ++completed_tables_;
        }
        return;
    }
    if (table_exhausted_[table] == 0) {
        pending_tables_.push_back(table);
    }
}

void TableScanSchedule::on_radius_advanced() {
    pending_tables_.clear();
    completed_tables_ = exhausted_tables_;
    for (std::uint32_t table = 0; table < table_count_; ++table) {
        if (table_exhausted_[table] != 0) {
            table_completed_[table] = 1;
        } else {
            table_completed_[table] = 0;
            pending_tables_.push_back(table);
        }
    }
}

DefaultQalshStrategy::DefaultQalshStrategy(std::uint32_t collision_threshold)
    : collision_threshold_(collision_threshold) {}

void DefaultQalshStrategy::set_candidate_rule(CandidateRule rule) {
    candidate_rule_ = std::move(rule);
}

void DefaultQalshStrategy::set_termination_rule(TerminationRule rule) {
    termination_rule_ = std::move(rule);
}

void DefaultQalshStrategy::set_candidate_budget(std::optional<std::size_t> budget) {
    if (budget.has_value()) {
        Require(*budget > 0, "candidate budget must be positive");
    }
    candidate_budget_override_set_ = true;
    candidate_budget_override_ = budget;
}

void DefaultQalshStrategy::set_check_after_evaluation(bool enabled) {
    check_after_evaluation_ = enabled;
}

void DefaultQalshStrategy::set_check_at_round_boundary(bool enabled) {
    check_at_round_boundary_ = enabled;
}

StrategyAction DefaultQalshStrategy::next(const QuerySnapshot& state) {
    if (finish_requested_) {
        return StrategyAction::Finish();
    }
    if (radius_advanced_) {
        radius_advanced_ = false;
        if (check_at_round_boundary_ && should_terminate(state)) {
            finish_requested_ = true;
            return StrategyAction::Finish();
        }
    }
    if (const auto table = table_schedule_.next_table(); table.has_value()) {
        return StrategyAction::Scan(*table);
    }
    if (table_schedule_.all_tables_exhausted() || state.all_tables_exhausted) {
        finish_requested_ = true;
        return StrategyAction::Finish();
    }
    if (!state.current_round_complete) {
        return StrategyAction::AdvanceRadius();
    }
    if (check_at_round_boundary_ && should_terminate(state)) {
        finish_requested_ = true;
        return StrategyAction::Finish();
    }
    return StrategyAction::AdvanceRadius();
}

void DefaultQalshStrategy::start(const QueryStart& query) {
    query_ = query;
    active_collision_threshold_ = collision_threshold_ == 0 ? query.collision_threshold
                                                            : collision_threshold_;
    if (!candidate_rule_) {
        Require(active_collision_threshold_ > 0 &&
                    active_collision_threshold_ <= query.num_hash_tables,
                "default QALSH strategy has an invalid collision threshold");
    }
    collision_state_.assign(query.num_points, candidate_rule_ ? 0U : active_collision_threshold_);
    table_schedule_.start(query.num_hash_tables);

    if (candidate_budget_override_set_) {
        active_candidate_budget_ = candidate_budget_override_;
    } else {
        // The paper's top-k budget is beta*n + k - 1, capped by the number
        // of indexed points.  set_candidate_budget() is the explicit escape
        // hatch for callers that want an absolute cap (or no cap).
        const std::size_t base_budget = query.candidate_budget;
        const std::size_t additional = static_cast<std::size_t>(query.k - 1U);
        const std::size_t uncapped =
            base_budget > std::numeric_limits<std::size_t>::max() - additional
                ? std::numeric_limits<std::size_t>::max()
                : base_budget + additional;
        active_candidate_budget_ = std::min<std::size_t>(query.num_points, uncapped);
    }
    if (termination_rule_) {
        // A supplied termination rule is a complete replacement, not an
        // additional predicate behind the standard budget or distance rule.
        active_candidate_budget_.reset();
    }
    radius_advanced_ = false;
    finish_requested_ = false;
}

SearchEngine::SearchEngine(std::shared_ptr<const ProjectionIndex> index,
                           PointAccessor accessor, SearchOptions options)
    : index_(std::move(index)), accessor_(std::move(accessor)), options_(options) {
    if (!index_) {
        throw std::invalid_argument("search engine requires an index");
    }
    if (!accessor_) {
        throw std::invalid_argument("search engine requires a point accessor");
    }
}

SearchResult SearchEngine::search(PointView query, std::uint32_t k,
                                  SearchStrategy& strategy) const {
    return search_impl(query, k, strategy, &strategy, &detail::DeliverRange<SearchStrategy>);
}

template <typename Strategy>
SearchResult SearchEngine::search_impl_typed(PointView query, std::uint32_t k,
                                             Strategy& strategy, void* strategy_address,
                                             detail::RangeDriver driver) const {
    const IndexMetadata& metadata = index_->metadata();
    Require(IsValidMetric(metadata.metric), "unsupported index metric");
    Require(k > 0, "k must be positive");
    Require(k <= metadata.num_points, "k exceeds the number of indexed points");
    Require(query.size() == metadata.num_dimensions, "query has the wrong dimensionality");
    for (float value : query) {
        RequireFinite(value, "query coordinate");
    }
    const std::uint32_t table_count = options_.num_hash_tables.value_or(metadata.num_hash_tables);
    Require(table_count > 0 && table_count <= metadata.num_hash_tables,
            "invalid query table count");
    const std::uint32_t scan_quantum =
        options_.scan_quantum == 0 ? metadata.qalsh.scan_quantum : options_.scan_quantum;
    Require(scan_quantum > 0, "scan quantum must be positive");

    const std::uint64_t projection_values = CheckedMultiplyOrThrow(
        table_count, metadata.num_dimensions, "query projection layout overflows");
    Require(index_->projection_vectors().size() >=
                CheckedSize(projection_values, "query projection layout is too large"),
            "projection index has insufficient projection vectors");

    QueryScratch* scratch = nullptr;
    for (const auto& candidate : query_scratch_pool) {
        if (!candidate->in_use) {
            scratch = candidate.get();
            break;
        }
    }
    if (scratch == nullptr) {
        query_scratch_pool.push_back(std::make_unique<QueryScratch>());
        scratch = query_scratch_pool.back().get();
    }
    scratch->in_use = true;
    QueryScratchLease scratch_lease{.scratch = scratch};
    if (scratch->index_identity.lock().get() != index_.get()) {
        scratch->cursors.clear();
        scratch->index_identity = index_;
    }
    scratch->cursors.resize(table_count);
    if (scratch->seen_epochs.size() < metadata.num_points) {
        scratch->seen_epochs.resize(metadata.num_points, 0);
    }
    ++scratch->epoch;
    if (scratch->epoch == 0) {
        std::fill(scratch->seen_epochs.begin(), scratch->seen_epochs.end(), 0);
        scratch->epoch = 1;
    }

    SearchRuntime runtime;
    runtime.radius = metadata.qalsh.initial_radius;
    runtime.bound = WindowBound(metadata.qalsh.bucket_width, runtime.radius);
    runtime.seen = std::span<std::uint32_t>(scratch->seen_epochs);
    runtime.seen_epoch = scratch->epoch;
    runtime.table_exhausted.assign(table_count, 0);
    runtime.table_round_complete.assign(table_count, 0);
    runtime.neighbors.reserve(k);

    auto& cursors = scratch->cursors;
    const auto projections = index_->projection_vectors();
    scratch->query_projections.resize(table_count);
    ProjectPoint(query, projections.data(), metadata.num_dimensions, scratch->query_projections);
    for (std::uint32_t table_id = 0; table_id < table_count; ++table_id) {
        const float query_projection = scratch->query_projections[table_id];
        RequireFinite(query_projection, "query projection");
        if (cursors[table_id] == nullptr) {
            cursors[table_id] = index_->make_cursor(table_id, query_projection);
        } else {
            index_->reset_cursor(*cursors[table_id], table_id, query_projection);
        }
    }

    const QueryStart start{.num_points = metadata.num_points,
                           .num_dimensions = metadata.num_dimensions,
                           .num_hash_tables = table_count,
                           .k = k,
                           .metric = metadata.metric,
                           .radius = runtime.radius,
                           .radius_growth = metadata.qalsh.radius_growth,
                           .bucket_width = metadata.qalsh.bucket_width,
                           .collision_threshold = metadata.qalsh.collision_threshold,
                           .candidate_budget = metadata.qalsh.candidate_budget,
                           .approximation_ratio = metadata.qalsh.approximation_ratio};
    strategy.start(start);

    SearchHitContext<Strategy> hit_context{.runtime = &runtime,
                                           .strategy = &strategy,
                                           .accessor = &accessor_,
                                           .options = &options_,
                                           .metadata = &metadata,
                                           .query = query,
                                           .k = k};

    std::size_t default_step_limit = 10000;
    const std::uint64_t estimated_steps = CheckedMultiplyOrThrow(
        CheckedMultiplyOrThrow(metadata.num_points, table_count,
                               "default search step limit overflows"),
        32, "default search step limit overflows");
    if (estimated_steps > default_step_limit) {
        default_step_limit = CheckedSize(std::min<std::uint64_t>(
                                             estimated_steps,
                                             std::numeric_limits<std::size_t>::max()),
                                         "default search step limit is too large");
    }
    const std::size_t step_limit = options_.max_steps == 0 ? default_step_limit : options_.max_steps;
    Require(step_limit > 0, "max_steps must be positive");

    // Choose implementation types once per query, not once per hit. Both the
    // public cursor API and engine use the same scanner bodies. A known final
    // strategy can be inlined without adding a second public strategy seam.
    const auto* memory_index = dynamic_cast<const InMemoryIndex*>(index_.get());
    const auto* disk_index = dynamic_cast<const BPlusTreeIndex*>(index_.get());
    const auto* sorted_array_index = dynamic_cast<const SortedArrayIndex*>(index_.get());
    const auto scan = [&](ProjectionIndex::Cursor& cursor, std::size_t limit,
                          ScanScope scope, auto&& visit) {
        if (memory_index) {
            return memory_index->scan_impl(cursor, runtime.bound, limit, visit, scope);
        }
        if (disk_index) {
            return disk_index->scan_impl(cursor, runtime.bound, limit, visit, scope);
        }
        if (sorted_array_index) {
            return sorted_array_index->scan_impl(cursor, runtime.bound, limit, visit, scope);
        }
        // An external index retains the ordinary type-erased cursor contract.
        using Visitor = std::remove_reference_t<decltype(visit)>;
        struct ExternalVisitor {
            Visitor* visit;
            std::uint64_t num_points;
        } external{&visit, metadata.num_points};
        return index_->scan(cursor, runtime.bound, limit, &external,
                            [](void* context, const ProjectionHit& hit) {
                                const auto& external = *static_cast<ExternalVisitor*>(context);
                                Require(hit.point_id < external.num_points,
                                        "projection hit point id is out of range");
                                const DiskEntry entry{hit.projected_value, hit.point_id};
                                (*external.visit)(detail::HitRange{
                                    reinterpret_cast<const std::byte*>(&entry), 1, false,
                                    hit.table_id, hit.query_value});
                            }, scope);
    };
    TerminationReason reason = TerminationReason::strategy;
    bool finished = false;
    for (std::size_t step = 0; step < step_limit && !finished; ++step) {
        const QuerySnapshot& before = MakeSnapshot(runtime, k);
        const StrategyAction action = strategy.next(before);
        switch (action.kind) {
            case StrategyActionKind::scan: {
                Require(action.table_id < table_count,
                        "strategy requested an invalid projection table");
                const std::size_t scan_limit = action.scan_scope == ScanScope::range
                                                   ? std::numeric_limits<std::size_t>::max()
                                                   : static_cast<std::size_t>(scan_quantum);
                detail::RangeExecution execution{
                    strategy_address, MakeSnapshot(runtime, k), &hit_context,
                    [](detail::RangeExecution& execution, PointId id) {
                        auto& context = *static_cast<SearchHitContext<Strategy>*>(execution.context);
                        context.runtime->hits = execution.state.projection_hits;
                        context.runtime->snapshot_cache.projection_hits = execution.state.projection_hits;
                        EvaluateCandidate(context, id);
                        execution.state = MakeSnapshot(*context.runtime, context.k);
                    }};
                const ScanReport report = scan(*cursors[action.table_id], scan_limit, action.scan_scope,
                    [&](const detail::HitRange& range) { driver(execution, range); });
                runtime.hits = execution.state.projection_hits;
                if (report.table_exhausted && runtime.table_exhausted[action.table_id] == 0) {
                    runtime.table_exhausted[action.table_id] = 1;
                    ++runtime.exhausted_tables;
                }
                if ((report.window_exhausted || report.table_exhausted) &&
                    runtime.table_round_complete[action.table_id] == 0) {
                    runtime.table_round_complete[action.table_id] = 1;
                    ++runtime.completed_tables;
                }
                runtime.round_complete = runtime.completed_tables == table_count;
                InvalidateSnapshot(runtime);
                ScanReport with_global = report;
                with_global.round_complete = runtime.round_complete;
                with_global.all_tables_exhausted = runtime.exhausted_tables == table_count;
                const ScanBoundaryEvent boundary_event{.report = with_global,
                                                        .radius = runtime.radius};
                const QuerySnapshot& boundary_state = MakeSnapshot(runtime, k);
                strategy.on_scan_boundary(boundary_event, boundary_state);
                break;
            }
            case StrategyActionKind::evaluate:
                EvaluateCandidate(hit_context, action.point_id);
                break;
            case StrategyActionKind::advance_radius: {
                const float next_radius = runtime.radius * metadata.qalsh.radius_growth;
                Require(IsFinite(next_radius) && next_radius > runtime.radius,
                        "strategy requested an invalid radius progression");
                runtime.radius = next_radius;
                runtime.bound = WindowBound(metadata.qalsh.bucket_width, runtime.radius);
                // Exhausted tables stay complete in every later radius window.
                runtime.completed_tables = runtime.exhausted_tables;
                runtime.round_complete = runtime.completed_tables == table_count;
                InvalidateSnapshot(runtime);
                for (std::uint32_t table_id = 0; table_id < table_count; ++table_id) {
                    runtime.table_round_complete[table_id] = runtime.table_exhausted[table_id];
                }
                const QuerySnapshot& radius_state = MakeSnapshot(runtime, k);
                strategy.on_radius_advanced(radius_state);
                break;
            }
            case StrategyActionKind::finish:
                finished = true;
                reason = before.all_tables_exhausted ? TerminationReason::scan_exhausted
                                                      : TerminationReason::strategy;
                break;
            default:
                reason = TerminationReason::invalid_strategy_action;
                finished = true;
                break;
        }
    }
    if (!finished) {
        reason = TerminationReason::step_limit;
    }
    SearchResult result{.neighbors = runtime.neighbors,
                        .reason = reason,
                        .evaluated_candidates = runtime.evaluated,
                        .projection_hits = runtime.hits,
                        .complete = finished && runtime.neighbors.size() == k &&
                                    (reason == TerminationReason::strategy ||
                                     reason == TerminationReason::scan_exhausted)};
    return result;
}

SearchResult SearchEngine::search_impl(PointView query, std::uint32_t k,
                                       SearchStrategy& strategy, void* strategy_address,
                                       detail::RangeDriver driver) const {
    return search_impl_typed<SearchStrategy>(query, k, strategy, strategy_address, driver);
}

SearchResult SearchEngine::search_impl_default(PointView query, std::uint32_t k,
                                               DefaultQalshStrategy& strategy) const {
    return search_impl_typed<DefaultQalshStrategy>(query, k, strategy, &strategy,
                                                   &detail::DeliverRange<DefaultQalshStrategy>);
}

template SearchResult SearchEngine::search_impl_typed<SearchStrategy>(
    PointView, std::uint32_t, SearchStrategy&, void*, detail::RangeDriver) const;
template SearchResult SearchEngine::search_impl_typed<DefaultQalshStrategy>(
    PointView, std::uint32_t, DefaultQalshStrategy&, void*, detail::RangeDriver) const;

const char* ToString(TerminationReason reason) noexcept {
    switch (reason) {
        case TerminationReason::strategy:
            return "strategy";
        case TerminationReason::scan_exhausted:
            return "scan_exhausted";
        case TerminationReason::invalid_strategy_action:
            return "invalid_strategy_action";
        case TerminationReason::step_limit:
            return "step_limit";
    }
    return "unknown";
}

const char* ToString(Metric metric) noexcept {
    switch (metric) {
        case Metric::l1:
            return "l1";
        case Metric::l2:
            return "l2";
    }
    return "unknown";
}

}  // namespace qalsh
