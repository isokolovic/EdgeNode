#include "rpi/edge_protocol.h"

#include <gtest/gtest.h>
#include <cstring>

namespace edgenode::tests {

	using namespace edgenode::protocol;

	/// @brief Test checksum computation for messages with empty and non-empty payloads.
	TEST(Protocol, ChecksumEmptyPayload)
	{
		Message msg{};
		msg.type = MsgType::PING;
		msg.length = 0;

		EXPECT_EQ(compute_checksum(msg), 0x01);
	}

	/// @brief Test checks computation for a message with a non-empty payload, ensuring it correctly incorporates the type, length, and payload bytes.
	TEST(Protocol, ChecksumNonEmptyPayload)
	{
		Message msg{};
		msg.type = MsgType::SENSOR_DATA;
		msg.length = 2;
		msg.payload[0] = 0xAB;
		msg.payload[1] = 0xCD;

		EXPECT_EQ(compute_checksum(msg), (0x10 ^ 0x02 ^ 0xAB ^ 0xCD));
	}

	/// @brief Test checks that a message can be serialized into a byte buffer and then deserialized back into a Message struct, with all fields (type, length, payload) remaining consistent throughout the process.
	TEST(Protocol, SerializeDeserializeRoundtrip)
	{
		Message original{};
		original.type = MsgType::SENSOR_DATA;
		original.length = 4;
		original.payload[0] = 0x41;
		original.payload[1] = 0xC8;
		original.payload[2] = 0x00;
		original.payload[3] = 0x00;

		uint8_t buffer[64]{};
		int len = serialize(original, buffer, sizeof(buffer));
		ASSERT_EQ(len, OVERHEAD + original.length);

		Message decoded{};
		ASSERT_TRUE(deserialize(buffer, len, decoded));
		EXPECT_EQ(decoded.type, original.type);
		EXPECT_EQ(decoded.length, original.length);
		EXPECT_EQ(std::memcmp(decoded.payload, original.payload, decoded.length), 0);
	}

	/// @brief Test checks that a simple PING message can be serialized and deserialized correctly, ensuring that even messages with no payload are handled properly by the protocol implementation.
	TEST(Protocol, PingRoundtrip)
	{
		Message ping{};
		ping.type = MsgType::PING;
		ping.length = 0;

		uint8_t buffer[64]{};
		int len = serialize(ping, buffer, sizeof(buffer));
		ASSERT_EQ(len, OVERHEAD);

		Message decoded{};
		ASSERT_TRUE(deserialize(buffer, len, decoded));
		EXPECT_EQ(decoded.type, MsgType::PING);
		EXPECT_EQ(decoded.length, 0);
	}

	/// @brief Test checks that the deserialization function correctly identifies and rejects messages with an invalid start byte, ensuring that the protocol implementation is robust against malformed input.
	TEST(Protocol, RejectsBadStartByte)
	{
		Message msg{};
		uint8_t bad[] = { 0xBB, 0x01, 0x00, 0x01 };

		EXPECT_FALSE(deserialize(bad, sizeof(bad), msg));
	}

	/// @brief Test checks that the deserialization function correctly identifies and rejects messages that are truncated (i.e., shorter than the minimum expected length), ensuring that the protocol implementation can handle incomplete data gracefully.
	TEST(Protocol, RejectsTruncatedFrame)
	{
		Message msg{};
		uint8_t bad[] = { 0xAA, 0x01 };

		EXPECT_FALSE(deserialize(bad, sizeof(bad), msg));
	}

	/// @brief Test checks that the deserialization function correctly identifies and rejects messages with an invalid checksum, ensuring that the protocol implementation can detect data corruption.
	TEST(Protocol, RejectsBadChecksum)
	{
		Message msg{};
		uint8_t bad[] = { 0xAA, 0x01, 0x00, 0xFF };

		EXPECT_FALSE(deserialize(bad, sizeof(bad), msg));
	}
	/// @brief Test checks that the deserialization function correctly identifies and rejects messages that claim to have a payload length exceeding the maximum allowed, ensuring that the protocol implementation enforces payload size limits.
	TEST(Protocol, RejectsOversizedLength)
	{
		Message msg{};
		uint8_t bad[] = { 0xAA, 0x01, 0xFF, 0x00 };

		EXPECT_FALSE(deserialize(bad, sizeof(bad), msg));
	}

	/// @brief Test checks that all message types can be serialized and deserialized correctly, ensuring that the protocol implementation handles each message type as expected.
	TEST(Protocol, AllMessageTypesRoundtrip)
	{
		MsgType types[] = {
			MsgType::PING, MsgType::PONG, MsgType::SENSOR_DATA,
			MsgType::GPIO_COMMAND, MsgType::ACK, MsgType::ERROR
		};

		for (MsgType t : types)
		{
			Message msg{};
			msg.type = t;
			msg.length = 0;

			uint8_t buffer[64]{};
			int len = serialize(msg, buffer, sizeof(buffer));
			ASSERT_EQ(len, OVERHEAD);

			Message decoded{};
			ASSERT_TRUE(deserialize(buffer, len, decoded));
			EXPECT_EQ(decoded.type, t);
		}
	}

	/// @brief Test Test checks that a message with the maximum allowed payload size can be serialized and deserialized correctly, ensuring that the protocol implementation can handle edge cases involving large payloads without data loss or corruption.
	TEST(Protocol, MaxPayloadRoundtrip)
	{
		Message msg{};
		msg.type = MsgType::SENSOR_DATA;
		msg.length = MAX_PAYLOAD;
		for (uint8_t i = 0; i < MAX_PAYLOAD; ++i)
			msg.payload[i] = i;

		uint8_t buffer[MAX_PAYLOAD + OVERHEAD]{};
		int len = serialize(msg, buffer, sizeof(buffer));
		ASSERT_EQ(len, OVERHEAD + MAX_PAYLOAD);

		Message decoded{};
		ASSERT_TRUE(deserialize(buffer, len, decoded));
		EXPECT_EQ(decoded.length, MAX_PAYLOAD);
		EXPECT_EQ(std::memcmp(decoded.payload, msg.payload, MAX_PAYLOAD), 0);
	}

	/// @brief Test checks that the serialization function correctly identifies and rejects attempts to serialize a message into a buffer that is too small to hold the entire message, ensuring that the protocol implementation can prevent buffer overflows and handle insufficient buffer sizes gracefully.
	TEST(Protocol, SerializeFailsWhenBufferTooSmall)
	{
		Message msg{};
		msg.type = MsgType::PING;
		msg.length = 0;

		uint8_t tiny[2]{};
		EXPECT_EQ(serialize(msg, tiny, sizeof(tiny)), -1);
	}

} // namespace edgenode::tests