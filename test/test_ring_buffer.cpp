#include "core/ring_buffer.h"

#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>

namespace tests {

using coretypes::RingBuffer;

// A fresh buffer must have head == tail: empty, size 0, and pop yields nullopt
// rather than a stale slot value.
TEST(RingBuffer, StartsEmpty)
{
	RingBuffer<int, 4> rb;

	EXPECT_TRUE(rb.empty());
	EXPECT_EQ(rb.size(), 0u);
	EXPECT_FALSE(rb.pop().has_value());
}

// Basic FIFO order: items come out in push order, and size tracks head - tail.
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

// Pins the drop-oldest overflow policy. Pushing past capacity must advance tail
// rather than block or corrupt the buffer, so the newest Capacity items survive
// in order and size never exceeds Capacity.
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

} // namespace tests
