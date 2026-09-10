// SENSOR_DATA payload codec tests. No hardware, runs on every platform.

#include <gtest/gtest.h>

#include "sensor_payload.h"

namespace tests {

// @brief Verify encoding and decoding a SensorReading preserves all fields.
TEST(SensorPayload, RoundtripPreservesFields)
{
	coretypes::SensorReading sent;
	sent.source = coretypes::SensorSource::DHT11_TEMPERATURE;
	sent.value = 21.5f;
	sent.quality = coretypes::ReadingQuality::GOOD;

	protocol::WireMessage msg{};
	ASSERT_TRUE(protocol::encode_sensor_reading(sent, 42, msg));

	EXPECT_EQ(msg.id, protocol::MSG_SENSOR_DATA);
	EXPECT_EQ(msg.dlc, protocol::sensor_payload_size);
	EXPECT_EQ(msg.seq, 42);

	coretypes::SensorReading received;
	ASSERT_TRUE(protocol::decode_sensor_reading(msg, received));
	EXPECT_EQ(received.source, sent.source);
	EXPECT_EQ(received.quality, sent.quality);
	EXPECT_FLOAT_EQ(received.value, sent.value);
}

// @brief Verify that sensor_payload_size does not exceed max_payload.
TEST(SensorPayload, FitsInMaxPayload)
{
	EXPECT_EQ(protocol::sensor_payload_size, protocol::max_payload);
}

// @brief Verify that an encoded SensorReading frame survives UART framing.
TEST(SensorPayload, EncodedFrameSurvivesUartFraming)
{
	coretypes::SensorReading sent;
	sent.source = coretypes::SensorSource::LIGHT;
	sent.value = -3.25f;
	sent.quality = coretypes::ReadingQuality::STALE;

	protocol::WireMessage msg{};
	ASSERT_TRUE(protocol::encode_sensor_reading(sent, 7, msg));

	uint8_t buffer[protocol::uart_max_frame];
	const int length = protocol::serialize_uart(msg, buffer, sizeof(buffer));
	ASSERT_GT(length, 0);

	protocol::WireMessage wire{};
	ASSERT_TRUE(protocol::deserialize_uart(buffer, length, wire));

	coretypes::SensorReading received;
	ASSERT_TRUE(protocol::decode_sensor_reading(wire, received));
	EXPECT_EQ(received.source, sent.source);
	EXPECT_EQ(received.quality, sent.quality);
	EXPECT_FLOAT_EQ(received.value, sent.value);
}

// @brief Verify that decode_sensor_reading rejects non-SENSOR_DATA message IDs.
TEST(SensorPayload, RejectsWrongMessageId)
{
	protocol::WireMessage msg{};
	msg.id = protocol::MSG_PING;
	msg.dlc = protocol::sensor_payload_size;
	protocol::finalize_crc(msg);

	coretypes::SensorReading received;
	EXPECT_FALSE(protocol::decode_sensor_reading(msg, received));
}

// @brief Verify that decode_sensor_reading rejects incorrect payload length.
TEST(SensorPayload, RejectsWrongLength)
{
	protocol::WireMessage msg{};
	msg.id = protocol::MSG_SENSOR_DATA;
	msg.dlc = 3;
	protocol::finalize_crc(msg);

	coretypes::SensorReading received;
	EXPECT_FALSE(protocol::decode_sensor_reading(msg, received));
}

// @brief Verify that decode_sensor_reading rejects invalid enum values.
TEST(SensorPayload, RejectsUnknownEnumValues)
{
	protocol::WireMessage msg{};
	msg.id = protocol::MSG_SENSOR_DATA;
	msg.dlc = protocol::sensor_payload_size;
	msg.payload[0] = 200; // no such SensorSource
	protocol::finalize_crc(msg);

	coretypes::SensorReading received;
	EXPECT_FALSE(protocol::decode_sensor_reading(msg, received));

	msg.payload[0] = static_cast<uint8_t>(coretypes::SensorSource::SOUND);
	msg.payload[1] = 9; // no such ReadingQuality
	protocol::finalize_crc(msg);
	EXPECT_FALSE(protocol::decode_sensor_reading(msg, received));
}

} // namespace tests
