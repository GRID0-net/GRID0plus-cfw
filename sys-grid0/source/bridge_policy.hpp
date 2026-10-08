#pragma once
#include <cstddef>
#include <cstdint>

namespace ztnx {
// One decision for both services and every session of a game. Changing the
// overlay preference must not change the meaning of already-open sockets.
class BridgePolicy {
    struct Entry { std::uint64_t pid{}; unsigned mask{}; };
    Entry entries[16]{};
public:
    template<class Read, class Exited>
    unsigned ForProcess(std::uint64_t pid, Read read, Exited exited) {
        if (pid == 0) return 0;
        for (const auto &entry : entries)
            if (entry.pid == pid) return entry.mask;
        for (auto &entry : entries) {
            if (entry.pid == 0 || exited(entry.pid)) {
                entry = {pid, read()};
                return entry.mask;
            }
        }
        // Never evict a possibly live process and change its socket semantics.
        return 0;
    }
};
}
