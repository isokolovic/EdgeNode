#include "core/ring_buffer.h"

#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>

namespace edgenode::tests {

using core::RingBuffer;

/// @brief Test checks that a newly created RingBuffer is empty, has a size of zero, and that popping from it returns no value, ensuring that the initial state of the buffer is correctly set up for use.
TEST(RingBuffer, StartsEmpty)
{
	RingBuffer<int, 4> rb;

	EXPECT_TRUE(rb.empty());
	EXPECT_EQ(rb.size(), 0u);
	EXPECT_FALSE(rb.pop().has_value());
}

/// @brief Test checks that after pushing items into the RingBuffer, the size reflects the number of items, and that popping returns the items in the order they were pushed, ensuring that the buffer maintains correct FIFO behavior.
TEST(RingBuffer, PushThenPop)
{
	RingBuffer<int, 4> rb;
	rb.push(11);
	rb.push(22);

	EXPECT_EQ(rb.size(), 2u);
	EXPECT_EQ(rb.pop().value(), 11);
	EXPECT_EQ(rb.pop().value(), 22);
	EXPECT_TRUE(rb.empty());
}

/// @brief Test checks that when more items are pushed into the RingBuffer than its capacity, the oldest items are overwritten according to the drop-oldest policy, and that popping returns the most recent items in the correct order, ensuring that the buffer correctly handles overflow situations without crashing or losing synchronization.
TEST(RingBuffer, DropsOldestOnOverflow)
{
	RingBuffer<int, 4> rb;
	for (int i = 0; i < 6; ++i)
		rb.push(i);

	// Capacity is 4, so the two oldest (0, 1) were overwritten.
	EXPECT_EQ(rb.size(), 4u);
	EXPECT_EQ(rb.pop().value(), 2);
	EXPECT_EQ(rb.pop().value(), 3);
	EXPECT_EQ(rb.pop().value(), 4);
	EXPECT_EQ(rb.pop().value(), 5);
	EXPECT_FALSE(rb.pop().has_value());
}

// One producer, one consumer, no data loss.
// The producer applies backpressure (waits while full) so the drop-oldest
// policy never discards an item, letting us assert every value arrives in order.
TEST(RingBuffer, SingleProducerSingleConsumerNoLoss)
{
	constexpr int total = 1'000'000;
	RingBuffer<int, 1024> rb;

	std::thread producer([&]
	{
		for (int i = 0; i < total; ++i)
		{
			// Wait while the buffer is full to preserve every item.
			while (rb.size() == 1024)
				std::this_thread::yield();
			rb.push(i);
		}
	});

	int expected = 0;
	while (expected < total)
	{
		auto item = rb.pop();
		if (!item.has_value())
		{
			std::this_thread::yield();
			continue;
		}
		ASSERT_EQ(item.value(), expected);
		++expected;
	}

	producer.join();
	EXPECT_EQ(expected, total);
	EXPECT_TRUE(rb.empty());
}

} // namespace edgenode::tests
