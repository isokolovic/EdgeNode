#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace coretypes {

/// @brief Fixed-capacity lock-free single-producer single-consumer ring buffer.
/// Capacity must be a power of two because the implementation uses a bitmask to 
/// convert ever-increasing head/tail counters into the indexes within the buffer's 
/// Capacity (fixed-size array).
/// To map unbounded head/tail counters to the fixed-size array, buffer[t & mask] is used
/// in push() and pop() -> see details in the implementation.
/// Capacity is specified at compile time.
/// Overflow policy is drop-oldest: when full, push overwrites the oldest entry.
/// Thread safety: exactly one producer thread may call push(); exactly one
/// consumer thread may call pop(). No other synchronisation is required.
template <typename T, std::size_t Capacity>
class RingBuffer
{
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
	// T must be trivially copyable (e.g. with memcpy). No user-defined copy/move constructors or destructors.
    // Because the buffer copies objects using plain memory copies, without any mutexes or locks.
    // And that works only if if construction, copying and destruction can't do anything "clever".
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable for lock-free operation");

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
        // (producer is the only one writing to head)
        // Relaxed = just read 64 bit value, no bit ordering constraints here, since only the producer thread writes to head
		// std::atomic<std::size_t> load() same as T x = y, but explicit
        std::size_t h = head.load(std::memory_order_relaxed);

        // Write the item into the ring buffer slot
		// (h & mask) = h % Capacity -> wrap the index into [0, Capacity-1]
		// E.g. For Capacity = 8, mask = 7 (0b111).
		// If h = 10 (0b1010), then h & mask = 0b1010 & 0b0111 = 0b0010 = 2 -> write to buffer[2].
		buffer[h & mask] = item;

        // Publish the new head index
        // Release ensures that buffer write (above) cannot be reordered with this store by the
        // compiler or CPU.
        // h+1 = publish a new head index - comsumer sees new item is available
		// std::atomic<std::size_t> store same as y = x, but explicit
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
		// what another thread has published before this load.
        std::size_t h = head.load(std::memory_order_acquire);
        if (t == h)
			return std::nullopt; // Buffer is empty

        T item = buffer[t & mask];
		// Publish the new tail index
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
    // Bitmask for wrapping elements into the buffer. 
	// Capacity must be a power of two so that mask wraps the index 
    // into the range [0, Capacity-1] using bitwise AND.
    static constexpr std::size_t mask = Capacity - 1;

    T buffer[Capacity]{};

    // Alignas forces head and tail onto separate 64-byte cache lines. 
    // Without this they'd likely share one line at some point 
	// Since the producer writes head on every push() and the consumer writes tail
    // on every pop(), this would cause false sharing.
	// Downside: 64 bytes instead of 8 bytes for each atomic variable
    alignas(64) std::atomic<std::size_t> head{0};
    alignas(64) std::atomic<std::size_t> tail{0};
};

} // namespace coretypes
