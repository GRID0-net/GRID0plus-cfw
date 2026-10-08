#pragma once
#include <cstddef>
#include <cstdint>

namespace ztnx {
// A subtraction-based check avoids overflow from malformed or changing limits.
// Preserve the 2 MiB work area Atmosphere's fatal/display path has needed in
// captured low-memory failures; this is a safety floor, not a boot guarantee.
constexpr std::int64_t SystemMemoryReserve = 2 * 1024 * 1024;
constexpr bool MemoryBudgetAllows(std::int64_t limit, std::int64_t used,
                                  std::size_t extra) {
    if (limit <= 0 || used < 0 || used > limit) return false;
    const auto free = limit - used;
    return free >= SystemMemoryReserve &&
        extra <= static_cast<std::uint64_t>(free - SystemMemoryReserve);
}
}
