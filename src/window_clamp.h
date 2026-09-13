#pragma once

#include "qalsh/qalsh.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>

#if !defined(QALSH_WINDOW_FORCE_SCALAR) && \
    (defined(__x86_64__) || defined(_M_X64))
#include <emmintrin.h>
#define QALSH_WINDOW_HAS_SSE2 1
#else
#define QALSH_WINDOW_HAS_SSE2 0
#endif

namespace qalsh::window_detail {

inline constexpr std::size_t kEntryStride = sizeof(Projection) + sizeof(PointId);
static_assert(kEntryStride == 8U);

/**
 * Whether ClampToWindow is compiled with its conservative x86-64 SSE2 path.
 * This is intentionally a private-header seam for focused tests only.
 */
[[nodiscard]] static inline constexpr bool UsesSse2() noexcept {
#if QALSH_WINDOW_HAS_SSE2
    return true;
#else
    return false;
#endif
}

[[nodiscard]] static inline Projection ReadProjection(const std::byte* bytes) noexcept {
    Projection value{0.0F};
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

[[nodiscard]] static inline bool Inside(const detail::HitRange& range, float bound,
                                 std::size_t position) noexcept {
    const auto* bytes = range.reverse
                            ? range.first - position * kEntryStride
                            : range.first + position * kEntryStride;
    return std::abs(range.query_value - ReadProjection(bytes)) <= bound;
}

#if QALSH_WINDOW_HAS_SSE2
namespace sse2 {

[[nodiscard]] static inline __m128 LoadFourForward(const std::byte* first) noexcept {
    // Each load covers two complete entries.  The caller only invokes this
    // for a four-entry block wholly contained in the supplied HitRange.
    __m128i first_pair{};
    __m128i second_pair{};
    std::memcpy(&first_pair, first, sizeof(first_pair));
    std::memcpy(&second_pair, first + 2U * kEntryStride, sizeof(second_pair));
    const __m128i first_values = _mm_shuffle_epi32(first_pair, _MM_SHUFFLE(2, 0, 2, 0));
    const __m128i second_values = _mm_shuffle_epi32(second_pair, _MM_SHUFFLE(2, 0, 2, 0));
    return _mm_castsi128_ps(_mm_unpacklo_epi64(first_values, second_values));
}

[[nodiscard]] static inline __m128 LoadFourReverse(const std::byte* first) noexcept {
    // `first` is the first logical entry, so the two physical pairs are
    // [entry(-1), entry(0)] and [entry(-3), entry(-2)].  Reverse each pair
    // after extracting its value words, then join the pairs in logical order.
    __m128i first_pair{};
    __m128i second_pair{};
    std::memcpy(&first_pair, first - kEntryStride, sizeof(first_pair));
    std::memcpy(&second_pair, first - 3U * kEntryStride, sizeof(second_pair));
    const __m128i first_values = _mm_shuffle_epi32(first_pair, _MM_SHUFFLE(2, 0, 2, 0));
    const __m128i second_values = _mm_shuffle_epi32(second_pair, _MM_SHUFFLE(2, 0, 2, 0));
    const __m128i first_logical = _mm_shuffle_epi32(first_values, _MM_SHUFFLE(0, 1, 0, 1));
    const __m128i second_logical = _mm_shuffle_epi32(second_values, _MM_SHUFFLE(0, 1, 0, 1));
    return _mm_castsi128_ps(_mm_unpacklo_epi64(first_logical, second_logical));
}

[[nodiscard]] static inline unsigned InsideMask(const detail::HitRange& range,
                                         float bound, std::size_t position) noexcept {
    const __m128 values = range.reverse
                              ? LoadFourReverse(range.first - position * kEntryStride)
                              : LoadFourForward(range.first + position * kEntryStride);
    const __m128 query = _mm_set1_ps(range.query_value);
    const __m128 difference = _mm_sub_ps(query, values);
    const __m128 absolute_difference = _mm_andnot_ps(_mm_set1_ps(-0.0F), difference);
    const __m128 inside = _mm_cmple_ps(absolute_difference, _mm_set1_ps(bound));
    return static_cast<unsigned>(_mm_movemask_ps(inside));
}

[[nodiscard]] static inline std::size_t FirstZero(unsigned mask) noexcept {
    for (std::size_t lane = 0; lane != 4U; ++lane) {
        if ((mask & (1U << lane)) == 0U) return lane;
    }
    return 4U;
}

}  // namespace sse2
#endif

/**
 * Clamp a sorted outward range to the exact float32 abs-difference prefix.
 *
 * The far endpoint and first entry preserve the cheap common-case checks from
 * the scalar implementation.  For a partial range, the SSE2 build probes
 * four adjacent complete entries at a time while retaining a binary search
 * over the monotone prefix; the fallback is the original scalar binary
 * search.  No probe reads beyond the supplied range.
 */
static inline void ClampToWindow(detail::HitRange& range, float bound) noexcept {
    if (range.count == 0U || Inside(range, bound, range.count - 1U)) return;
    if (!Inside(range, bound, 0U)) {
        range.count = 0U;
        return;
    }

    std::size_t lo = 1U;
    std::size_t hi = range.count;
#if QALSH_WINDOW_HAS_SSE2
    while (hi - lo > 4U) {
        const std::size_t middle = lo + (hi - lo) / 2U;
        const std::size_t start = middle <= hi - 4U ? middle : hi - 4U;
        const unsigned mask = sse2::InsideMask(range, bound, start);
        if (mask == 0xFU) {
            lo = start + 4U;
        } else if (mask == 0U) {
            hi = start;
        } else {
            range.count = start + sse2::FirstZero(mask);
            return;
        }
    }
#else
    while (lo < hi) {
        const std::size_t middle = lo + (hi - lo) / 2U;
        if (Inside(range, bound, middle)) {
            lo = middle + 1U;
        } else {
            hi = middle;
        }
    }
    range.count = lo;
    return;
#endif

    while (lo < hi) {
        const std::size_t middle = lo + (hi - lo) / 2U;
        if (Inside(range, bound, middle)) {
            lo = middle + 1U;
        } else {
            hi = middle;
        }
    }
    range.count = lo;
}

}  // namespace qalsh::window_detail

#undef QALSH_WINDOW_HAS_SSE2
