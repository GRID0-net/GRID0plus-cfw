#include "../source/bridge_policy.hpp"
#include <cassert>
#include <cstdio>
int main() {
    ztnx::BridgePolicy p;
    unsigned selected = 0;
    int reads = 0;
    auto read = [&] { ++reads; return selected; };
    auto live = [](std::uint64_t) { return false; };
    assert(p.ForProcess(0, read, live) == 0 && reads == 0);
    assert(p.ForProcess(1, read, live) == 0);
    selected = 3;
    assert(p.ForProcess(1, read, live) == 0 && reads == 1);
    assert(p.ForProcess(2, read, live) == 3);
    selected = 0;
    assert(p.ForProcess(2, read, live) == 3 && reads == 2);
    assert(p.ForProcess(3, read, live) == 0);
    selected = 1;
    assert(p.ForProcess(4, read, live) == 1);
    selected = 2;
    assert(p.ForProcess(4, read, live) == 1);
    assert(p.ForProcess(5, read, live) == 2);
    for (std::uint64_t pid = 6; pid <= 16; ++pid)
        assert(p.ForProcess(pid, read, live) == 2);
    const int before_full = reads;
    assert(p.ForProcess(17, read, live) == 0 && reads == before_full);
    assert(p.ForProcess(2, read, live) == 3);
    assert(p.ForProcess(17, read, [](std::uint64_t pid) { return pid == 1; }) == 2);
    selected = 0;
    assert(p.ForProcess(17, read, live) == 2);
    std::puts("Bridge policy: online/LAN decisions stay fixed until process exit; live entries never evicted");
}
