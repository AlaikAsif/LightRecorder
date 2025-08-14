#ifndef SPSC_RING_H
#define SPSC_RING_H

#include <atomic>
#include <cstddef>
#include <vector>
#include <stdexcept>

template<typename T>
class SPSC_Ring {
public:
    explicit SPSC_Ring(size_t capacity)
        : buffer(capacity), head(0), tail(0), mask(capacity - 1) {
        if (capacity == 0 || (capacity & (capacity - 1)) != 0) {
            throw std::invalid_argument("Capacity must be a power of 2");
        }
    }

    bool push(const T& item) {
        size_t current_head = head.load(std::memory_order_relaxed);
        size_t next_head = (current_head + 1) & mask;

        if (next_head == tail.load(std::memory_order_acquire)) {
            return false; // Buffer is full
        }

        buffer[current_head] = item;
        head.store(next_head, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        size_t current_tail = tail.load(std::memory_order_relaxed);

        if (current_tail == head.load(std::memory_order_acquire)) {
            return false; // Buffer is empty
        }

        item = buffer[current_tail];
        tail.store((current_tail + 1) & mask, std::memory_order_release);
        return true;
    }

    bool is_empty() const {
        return head.load(std::memory_order_acquire) == tail.load(std::memory_order_acquire);
    }

    bool is_full() const {
        size_t next_head = (head.load(std::memory_order_relaxed) + 1) & mask;
        return next_head == tail.load(std::memory_order_acquire);
    }

    // New helpers for monitoring
    // The usable capacity (max items storable) is buffer.size() - 1
    size_t capacity() const {
        return buffer.size() - 1;
    }

    // Current number of items in the ring (0 .. capacity())
    size_t size() const {
        size_t h = head.load(std::memory_order_acquire);
        size_t t = tail.load(std::memory_order_acquire);
        return (h + buffer.size() - t) & mask;
    }

    // Fill factor in range [0.0, 1.0]
    double fillFactor() const {
        size_t cap = capacity();
        if (cap == 0) return 0.0;
        return static_cast<double>(size()) / static_cast<double>(cap);
    }

private:
    std::vector<T> buffer;
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
    const size_t mask;
};

#endif // SPSC_RING_H