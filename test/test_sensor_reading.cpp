#include "core/sensor_reading.h"
#include "core/ring_buffer.h"

#include <gtest/gtest.h>
#include <type_traits>

namespace tests {

using coretypes::ReadingQuality;
using coretypes::SensorReading;
using coretypes::SensorSource;

// Test that the aggregate constructor correctly initialises all fields.
TEST(SensorReading, AggregateConstruction)
{
	SensorReading r{ 1234, SensorSource::DHT11_TEMPERATURE, 21.5f, ReadingQuality::GOOD }; // aggregate initialisation

	EXPECT_EQ(r.timestamp_ms, 1234u);
	EXPECT_EQ(r.source, SensorSource::DHT11_TEMPERATURE);
	EXPECT_FLOAT_EQ(r.value, 21.5f);
	EXPECT_EQ(r.quality, ReadingQuality::GOOD);
}

// Test the default constructor and value initialisation of SensorReading.
TEST(SensorReading, ValueInitialisesToZero)
{
	SensorReading r{}; // uninitialised aggregate

	EXPECT_EQ(r.timestamp_ms, 0u);
	EXPECT_EQ(r.source, SensorSource::UNKNOWN);
	EXPECT_FLOAT_EQ(r.value, 0.0f);
	EXPECT_EQ(r.quality, ReadingQuality::GOOD);
}

// Guards against someone later adding a constructor, destructor or std::string
// member: that would break the RingBuffer static_assert and the lock-free copy.
TEST(SensorReading, IsTriviallyCopyable)
{
	// Required so the reading can travel through the lock-free ring buffer.
	EXPECT_TRUE(std::is_trivially_copyable_v<SensorReading>);
}

// End to end check of the actual message bus usage: a reading pushed into the
// ring buffer must come back out field for field identical.
TEST(SensorReading, TravelsThroughRingBuffer)
{
	coretypes::RingBuffer<SensorReading, 4> bus;

	SensorReading in{42, SensorSource::HALL_EFFECT, 99.0f, ReadingQuality::ESTIMATED};
	bus.push(in);

	auto out = bus.pop();

	ASSERT_TRUE(out.has_value());

	EXPECT_EQ(out->timestamp_ms, in.timestamp_ms);
	EXPECT_EQ(out->source, in.source);
	EXPECT_FLOAT_EQ(out->value, in.value);
	EXPECT_EQ(out->quality, in.quality);
}

} // namespace tests
