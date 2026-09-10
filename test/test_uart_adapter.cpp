// UART sensor adapter tests. A fake IPort feeds bytes, so no hardware is needed.

#include <gtest/gtest.h>

#include <vector>

#include "rpi/uart_sensor_adapter.h"
#include "sensor_payload.h"

namespace tests {

// Minimal IPort stand-in: read() hands out queued bytes, write() records them.
class FakePort : public rpi::serial::IPort
{
public:
	bool open(const std::string&, int) override { opened = true; return true; }
	void close() override { opened = false; }
	bool is_open() const override { return opened; }

	int write(const uint8_t* data, int length) override
	{
		written.insert(written.end(), data, data + length);
		return length;
	}

	int read(uint8_t* buffer, int max_length, int) override
	{
		int count = 0;
		while (count < max_length && read_pos < queued.size())
			buffer[count++] = queued[read_pos++];

		return count;
	}

	void queue(const uint8_t* data, int length) { queued.insert(queued.end(), data, data + length); }

	std::vector<uint8_t> queued;
	std::vector<uint8_t> written;
	size_t read_pos = 0;
	bool opened = true;
};

namespace {

// Serialise a SENSOR_DATA frame and push it into the fake port's receive queue.
void queue_reading(FakePort& port, coretypes::SensorSource source, float value, uint8_t seq)
{
	coretypes::SensorReading reading;
	reading.source = source;
	reading.value = value;
	reading.quality = coretypes::ReadingQuality::GOOD;

	protocol::WireMessage msg{};
	protocol::encode_sensor_reading(reading, seq, msg);

	uint8_t buffer[protocol::uart_max_frame];
	const int length = protocol::serialize_uart(msg, buffer, sizeof(buffer));
	port.queue(buffer, length);
}

}

// @brief Verify that UartSensorAdapter correctly decodes a single incoming SensorReading frame.
TEST(UartSensorAdapter, DecodesSingleFrame)
{
	FakePort port;
	queue_reading(port, coretypes::SensorSource::LIGHT, 55.0f, 0);

	rpi::protocol::UartSensorAdapter adapter(port);
	coretypes::SensorReading reading;
	ASSERT_TRUE(adapter.poll(reading, 0));
	EXPECT_EQ(reading.source, coretypes::SensorSource::LIGHT);
	EXPECT_FLOAT_EQ(reading.value, 55.0f);
	EXPECT_GT(reading.timestamp_ms, 0u);
}

// @brief Verify that UartSensorAdapter decodes consecutive incoming frames correctly.
TEST(UartSensorAdapter, DecodesBackToBackFrames)
{
	FakePort port;
	queue_reading(port, coretypes::SensorSource::LIGHT, 1.0f, 0);
	queue_reading(port, coretypes::SensorSource::JOYSTICK, 2.0f, 1);

	rpi::protocol::UartSensorAdapter adapter(port);
	coretypes::SensorReading first;
	coretypes::SensorReading second;
	ASSERT_TRUE(adapter.poll(first, 0));
	ASSERT_TRUE(adapter.poll(second, 0));

	EXPECT_EQ(first.source, coretypes::SensorSource::LIGHT);
	EXPECT_EQ(second.source, coretypes::SensorSource::JOYSTICK);
	EXPECT_EQ(adapter.sequence_gaps(), 0u);
}

// @brief Verify that UartSensorAdapter resynchronizes after encountering leading garbage bytes.
TEST(UartSensorAdapter, ResyncsAfterLeadingGarbage)
{
	FakePort port;
	const uint8_t noise[] = { 0x00, 0x12, 0xFF };
	port.queue(noise, 3);
	queue_reading(port, coretypes::SensorSource::SOUND, 9.0f, 0);

	rpi::protocol::UartSensorAdapter adapter(port);
	coretypes::SensorReading reading;
	ASSERT_TRUE(adapter.poll(reading, 0));
	EXPECT_EQ(reading.source, coretypes::SensorSource::SOUND);
}

// @brief Verify that UartSensorAdapter increments CRC error count on corrupted frames.
TEST(UartSensorAdapter, CountsCrcErrors)
{
	FakePort port;
	queue_reading(port, coretypes::SensorSource::LIGHT, 3.0f, 0);
	port.queued[5] ^= 0xFF; // corrupt a payload byte, CRC no longer matches

	rpi::protocol::UartSensorAdapter adapter(port);
	coretypes::SensorReading reading;
	EXPECT_FALSE(adapter.poll(reading, 0));
	EXPECT_EQ(adapter.crc_errors(), 1u);
}

// @brief Verify that UartSensorAdapter detects and counts sequence gaps.
TEST(UartSensorAdapter, CountsSequenceGaps)
{
	FakePort port;
	queue_reading(port, coretypes::SensorSource::LIGHT, 1.0f, 0);
	queue_reading(port, coretypes::SensorSource::LIGHT, 2.0f, 5); // frames 1..4 lost

	rpi::protocol::UartSensorAdapter adapter(port);
	coretypes::SensorReading reading;
	ASSERT_TRUE(adapter.poll(reading, 0));
	ASSERT_TRUE(adapter.poll(reading, 0));
	EXPECT_EQ(adapter.sequence_gaps(), 1u);
}

// @brief Verify that poll() returns false when no data is available on transport.
TEST(UartSensorAdapter, ReturnsFalseWhenNoData)
{
	FakePort port;
	rpi::protocol::UartSensorAdapter adapter(port);
	coretypes::SensorReading reading;
	EXPECT_FALSE(adapter.poll(reading, 0));
}

// @brief Verify that send_gpio_command formats and transmits a valid GPIO command frame.
TEST(UartSensorAdapter, SendsGpioCommandFrame)
{
	FakePort port;
	rpi::protocol::UartSensorAdapter adapter(port);
	ASSERT_TRUE(adapter.send_gpio_command(13, true));

	protocol::WireMessage msg{};
	ASSERT_TRUE(protocol::deserialize_uart(port.written.data(), static_cast<int>(port.written.size()), msg));
	EXPECT_EQ(msg.id, protocol::MSG_GPIO_COMMAND);
	EXPECT_EQ(msg.dlc, 2);
	EXPECT_EQ(msg.payload[0], 13);
	EXPECT_EQ(msg.payload[1], 1);
}

} // namespace tests
