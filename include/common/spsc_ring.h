//
// Created by Nainil Patel on 6/16/26.
//

#ifndef TRADING_INFRASTRUCTURE_SPSCRING_H
#define TRADING_INFRASTRUCTURE_SPSCRING_H

#include <array>
#include <atomic>
#include <cstddef>
#include <utility>

template<typename T, std::size_t capacity>
class spsc_ring {

    static_assert(capacity > 0 && (capacity & (capacity - 1)) == 0, "Capacity Must Be a Power of 2");
    static constexpr std::size_t cacheline_size = 64;

public:
    bool produce(T t) {
        auto write = writeIndex.load(std::memory_order_relaxed);
        const auto next_write = (write + 1) & (capacity - 1);
        const auto read = readIndex.load(std::memory_order_acquire);

        if (read == next_write) {
            return false;
        }

        ring[write] = std::move(t);
        writeIndex.store(next_write, std::memory_order_release);
        return true;
    }

    bool consume(T& out) {
        const auto read = readIndex.load(std::memory_order_relaxed);
        const auto write = writeIndex.load(std::memory_order_acquire);

        if (read == write) {
            return false;
        }

        out = std::move(ring[read]);
        const auto next_read = (read + 1) & (capacity - 1);
        readIndex.store(next_read, std::memory_order_release);

        return true;
    }

private:
    std::array<T, capacity> ring{};
    alignas(cacheline_size) std::atomic<std::size_t> readIndex{0};
    alignas(cacheline_size) std::atomic<std::size_t> writeIndex{0};
};

#endif