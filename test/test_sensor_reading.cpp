#include "core/sensor_reading.h"
#include "core/ring_buffer.h"

#include <gtest/gtest.h>
#include <type_traits>

namespace edgenode::tests {

using core::ReadingQuality;
using core::SensorReading;
using core::SensorSource;

/// @brief Test checks that a SensorReading can be constructed with specific values for all fields, and that those values are correctly stored and accessible through the struct's members, ensuring that the constructor initializes the struct as expected.
TEST(SensorReading, AggregateConstruction)
{
	SensorReading r{1234, SensorSource::DHT11_TEMPERATURE, 21.5f, ReadingQuality::GOOD};

	EXPECT_EQ(r.timestamp_ms, 1234u);
	EXPECT_EQ(r.source, SensorSource::DHT11_TEMPERATURE);
	EXPECT_FLOAT_EQ(r.value, 21.5f);
	EXPECT_EQ(r.quality, ReadingQuality::GOOD);
}

/// @brief Test checks that when a SensorReading is default-initialized (using aggregate initialization with empty braces), all fields are set to their default values (timestamp_ms = 0, source = UNKNOWN, value = 0.0f, quality = GOOD), ensuring that the struct's default state is well-defined and consistent with expectations.
TEST(SensorReading, ValueInitialisesToZero)
{
	SensorReading r{};

	EXPECT_EQ(r.timestamp_ms, 0u);
	EXPECT_EQ(r.source, SensorSource::UNKNOWN);
	EXPECT_FLOAT_EQ(r.value, 0.0f);
	EXPECT_EQ(r.quality, ReadingQuality::GOOD);
}

/// @brief Test checks that the SensorReading struct is trivially copyable, meaning it can be copied with a simple memory copy operation without invoking any user-defined copy constructors or assignment operators, ensuring that it can be safely used in contexts that require trivial copyability (like lock-free data structures).
TEST(SensorReading, IsTriviallyCopyable)
{
	// Required so the reading can travel through the lock-free ring buffer.
	EXPECT_TRUE(std::is_trivially_copyable_v<SensorReading>);
}

/// @brief Test checks that a SensorReading can be pushed into a RingBuffer and then popped out, with all field values remaining consistent throughout the process, ensuring that the SensorReading struct can be correctly stored and retrieved from the buffer without data corruption or loss of information.
TEST(SensorReading, TravelsThroughRingBuffer)
{
	core::RingBuffer<SensorReading, 4> bus;

	SensorReading in{42, SensorSource::HALL_EFFECT, 99.0f, ReadingQuality::ESTIMATED};
	bus.push(in);

	auto out = bus.pop();
	ASSERT_TRUE(out.has_value());
	EXPECT_EQ(out->timestamp_ms, in.timestamp_ms);
	EXPECT_EQ(out->source, in.source);
	EXPECT_FLOAT_EQ(out->value, in.value);
	EXPECT_EQ(out->quality, in.quality);
}

} // namespace edgenode::tests
