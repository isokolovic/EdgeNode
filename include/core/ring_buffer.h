#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace edgenode::core {

/// @brief Fixed-capacity lock-free single-producer single-consumer ring buffer.
///
/// Capacity must be a power of two and is specified at compile time.
/// Overflow policy is drop-oldest: when full, push overwrites the oldest entry.
/// Thread safety: exactly one producer thread may call push(); exactly one
/// consumer thread may call pop(). No other synchronisation is required.
template <typename T, std::size_t Capacity>
class RingBuffer
{
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
        "Capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>,
        "T must be trivially copyable for lock-free operation");

public:
    RingBuffer() = default;

    // Non-copyable, non-movable.
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    /// @brief Push an item. If the buffer is full, the oldest item is overwritten.
    void push(const T& item)
    {
        // Load current write index (head)
		// relaxed = just read 64 bit value, no bit ordering constraints here (producer is the only one writing to head)
        std::size_t h = head.load(std::memory_order_relaxed);

        // Write the item into the ring buffer slot
		// (h & mask) = h % Capacity -> wrap the index into [0, Capacity-1]
		buffer[h & mask] = item; 

        // Publish the new head index
		// Release = Everything before this store becomes visible to others -> 
        // make sure previous write to buffer is visible before updating head index (publish prior writes)
        head.store(h + 1, std::memory_order_release);

		// Overflow check: if producer has advanced more than Capacity 
        // -> buffer is full -> oldest item must be dropped by advancing tail index
        std::size_t t = tail.load(std::memory_order_relaxed);
        if (h + 1 - t > Capacity)
            tail.store(h + 1 - Capacity, std::memory_order_release);
    }

    /// @brief Pop the oldest item, or return std::nullopt if empty.
    std::optional<T> pop()
    {
        std::size_t t = tail.load(std::memory_order_relaxed);

		// Acquire = everything after this load waits until we see 
		// what another thread has published before this load (consume published writes)
        std::size_t h = head.load(std::memory_order_acquire);
        if (t == h)
			return std::nullopt; // Buffer is empty

        T item = buffer[t & mask];
		// Publish the new tail index (consume the item)
        tail.store(t + 1, std::memory_order_release);
        return item;
    }

    /// @brief Return the number of items currently available to the consumer.
    std::size_t size() const
    {
        std::size_t h = head.load(std::memory_order_acquire);
        std::size_t t = tail.load(std::memory_order_acquire);
        return h - t;
    }

    /// @brief Return true when no items are available.
    bool empty() const { return size() == 0; }

private:
    static constexpr std::size_t mask = Capacity - 1;
    T buffer[Capacity]{};
    alignas(64) std::atomic<std::size_t> head{0};
    alignas(64) std::atomic<std::size_t> tail{0};
};

} // namespace edgenode::core
