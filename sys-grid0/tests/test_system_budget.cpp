#include "../source/system_budget.hpp"
#include <cassert>
#include <cstdio>
#include <limits>

int main() {
    using ztnx::MemoryBudgetAllows;
    constexpr auto reserve = ztnx::SystemMemoryReserve;
    assert(MemoryBudgetAllows(reserve, 0, 0));
    assert(!MemoryBudgetAllows(reserve - 1, 0, 0));
    assert(MemoryBudgetAllows(reserve * 2, 0, reserve));
    assert(!MemoryBudgetAllows(reserve * 2 - 1, 0, reserve));
    assert(MemoryBudgetAllows(reserve * 3, reserve, reserve));
    assert(!MemoryBudgetAllows(reserve * 3, reserve + 1, reserve));
    assert(!MemoryBudgetAllows(0, 0, 0));
    assert(!MemoryBudgetAllows(-1, 0, 0));
    assert(!MemoryBudgetAllows(reserve, -1, 0));
    assert(!MemoryBudgetAllows(reserve, reserve + 1, 0));
    assert(!MemoryBudgetAllows(std::numeric_limits<std::int64_t>::max(), 0,
                               std::numeric_limits<std::size_t>::max()));
    assert(MemoryBudgetAllows(std::numeric_limits<std::int64_t>::max(), 0, 0));
    std::puts("12 system memory budget cases passed");
}
