#include "rpi/edge_protocol.h"

#include <gtest/gtest.h>
#include <cstring>

namespace tests {

	using namespace rpi::protocol;
	using WireMessage = ::protocol::WireMessage;


# pragma Region CRC tests

	// Do the CRC-8/CCITT calculation separately from the production code	
	// This is used to verify that the production code's CRC calculation is correct
	static uint8_t reference_crc(const uint8_t* data, int length)
	{
		uint8_t crc = 0x00;

		for (int i = 0; i < length; ++i)
		{
			crc ^= data[i];

			// Shift out the top bit
			// XOR the polynomial only when that bit was set.
			for (int bit = 0; bit < 8; ++bit)
				// 0x80 is the top bit of a byte (1000 0000)
				// 0x07 is the polynomial for CRC-8/CCITT (000 0111)
				crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x07)
								   : static_cast<uint8_t>(crc << 1);
		}
		return crc;
	}

	// Test verifies that the production code's CRC matches the reference implementatio (used for validation)
	TEST(Protocol, CrcMatchesCcittReference)
	{
		// 0x010 = SENSOR_DATA id, dlc=2, seq=5, payload {0xAB, 0xCD} 
		const uint8_t sample[] = { 0x00, 0x10, 0x02, 0x05, 0xAB, 0xCD };

		// Test the reference implementation itself, to make sure it is correct.
		EXPECT_EQ(reference_crc(sample, sizeof(sample)), 0x3C); 

		WireMessage msg{};
		msg.id = ::protocol::MSG_SENSOR_DATA;
		msg.dlc = 2;
		msg.seq = 5;
		msg.payload[0] = 0xAB;
		msg.payload[1] = 0xCD;

		// Test that the production code's CRC matches the reference implementation.
		EXPECT_EQ(compute_crc(msg), reference_crc(sample, sizeof(sample)));
	}

	// Tests that the CRC is not a simple XOR of the bytes, which would be a weak checksum.
	TEST(Protocol, CrcIsNotPlainXor)
	{
		WireMessage msg{};
		msg.id = ::protocol::MSG_SENSOR_DATA;
		msg.dlc = 2;
		msg.seq = 0;
		msg.payload[0] = 0xAB;
		msg.payload[1] = 0xCD;

		uint8_t xor_checksum = static_cast<uint8_t>(::protocol::MSG_SENSOR_DATA) ^ 0x02 ^ 0xAB ^ 0xCD;
		EXPECT_NE(compute_crc(msg), xor_checksum);
	}

# pragma endregion CRC tests


# pragma Region Serialization tests

	// Test that the UART serialization produces the expected byte layout for a known message.
	TEST(Protocol, UartFrameLayout)
	{
		WireMessage msg{};
		msg.id = ::protocol::MSG_SENSOR_DATA; // id 0x010
		msg.dlc = 2;
		msg.seq = 0x07;
		msg.payload[0] = 0xDE;
		msg.payload[1] = 0xAD;

		uint8_t buffer[uart_max_frame]{};
		int len = rpi::protocol::serialize_uart(msg, buffer, sizeof(buffer));
		ASSERT_EQ(len, uart_overhead + msg.dlc); 

		EXPECT_EQ(buffer[0], stx);
		EXPECT_EQ(buffer[1], 0x00); // ID high byte
		EXPECT_EQ(buffer[2], 0x10); // ID low byte
		EXPECT_EQ(buffer[3], 0x02); // DLC
		EXPECT_EQ(buffer[4], 0x07); // SEQ
		EXPECT_EQ(buffer[5], 0xDE); // payload[0]
		EXPECT_EQ(buffer[6], 0xAD); // payload[1]
		EXPECT_EQ(buffer[len - 1], etx);
	}

	// Test that the UART serialization and deserialization roundtrip works for a known message.
	TEST(Protocol, UartRoundtrip)
	{
		WireMessage original{};
		original.id = ::protocol::MSG_SENSOR_DATA;
		original.dlc = 4;
		original.seq = 200;
		original.payload[0] = 0x41;
		original.payload[1] = 0xC8;
		original.payload[2] = 0x00;
		original.payload[3] = 0x00;

		uint8_t buffer[uart_max_frame]{};
		int len = rpi::protocol::serialize_uart(original, buffer, sizeof(buffer));
		ASSERT_EQ(len, uart_overhead + original.dlc);

		WireMessage decoded{};
		ASSERT_TRUE(rpi::protocol::deserialize_uart(buffer, len, decoded));
		EXPECT_EQ(decoded.id, original.id);
		EXPECT_EQ(decoded.dlc, original.dlc);
		EXPECT_EQ(decoded.seq, original.seq);
		EXPECT_EQ(std::memcmp(decoded.payload, original.payload, decoded.dlc), 0);
	}

	// Test UART serialization and deserialization for all message types, ensuring that the ID is preserved.
	TEST(Protocol, AllMessageTypesUartRoundtrip)
	{
		uint16_t ids[] = {
			::protocol::MSG_PING, ::protocol::MSG_PONG, ::protocol::MSG_SENSOR_DATA,
			::protocol::MSG_GPIO_COMMAND, ::protocol::MSG_ACK, ::protocol::MSG_ERROR
		};

		for (uint16_t id : ids)
		{
			WireMessage msg{};
			msg.id = id;
			msg.dlc = 0;
			msg.seq = 0;

			uint8_t buffer[uart_max_frame]{};
			int len = rpi::protocol::serialize_uart(msg, buffer, sizeof(buffer));
			ASSERT_EQ(len, uart_overhead);

			WireMessage decoded{};
			ASSERT_TRUE(rpi::protocol::deserialize_uart(buffer, len, decoded));
			EXPECT_EQ(decoded.id, id);
		}
	}

	// Test that the CAN serialization and deserialization roundtrip works for a known message.
	TEST(Protocol, CanRoundtrip)
	{
		WireMessage original{};
		original.id = ::protocol::MSG_GPIO_COMMAND;
		original.dlc = 3;
		original.seq = 17;
		original.payload[0] = 0x01;
		original.payload[1] = 0x02;
		original.payload[2] = 0x03;

		uint16_t can_id = 0;
		uint8_t data[8]{};
		int can_dlc = rpi::protocol::pack_can(original, can_id, data, sizeof(data));
		ASSERT_EQ(can_dlc, 1 + original.dlc + 1);
		EXPECT_EQ(can_id, static_cast<uint16_t>(::protocol::MSG_GPIO_COMMAND));
		EXPECT_EQ(data[0], 17); // SEQ first in the CAN data field.

		WireMessage decoded{};
		ASSERT_TRUE(rpi::protocol::unpack_can(can_id, data, can_dlc, decoded)); // unpack_can successful
		EXPECT_EQ(decoded.id, original.id);
		EXPECT_EQ(decoded.dlc, original.dlc);
		EXPECT_EQ(decoded.seq, original.seq);

		// compare payloads in the length of the dlc. 
		// memcmp returns 0 if equal
		EXPECT_EQ(std::memcmp(decoded.payload, original.payload, decoded.dlc), 0); 
	}

	// Test ensures that a message serialized over UART and CAN retains the same logical content, including CRC, when deserialized. 
	// This verifies that the protocol is transport-agnostic
	TEST(Protocol, UartAndCanCarryIdenticalLogicalContent)
	{
		// build a message
		WireMessage msg{};
		msg.id = ::protocol::MSG_SENSOR_DATA;
		msg.dlc = 5;
		msg.seq = 123;
		for (uint8_t i = 0; i < 5; ++i)
			msg.payload[i] = static_cast<uint8_t>(0x10 + i);

		// serialize and deserialize UART
		uint8_t uart_buf[uart_max_frame]{};
		int uart_len = rpi::protocol::serialize_uart(msg, uart_buf, sizeof(uart_buf));
		ASSERT_GT(uart_len, 0);
		WireMessage from_uart{};
		ASSERT_TRUE(rpi::protocol::deserialize_uart(uart_buf, uart_len, from_uart));

		// serialize and deserialize CAN
		uint16_t can_id = 0;
		uint8_t can_data[8]{};
		int can_dlc = rpi::protocol::pack_can(msg, can_id, can_data, sizeof(can_data));
		ASSERT_GT(can_dlc, 0);
		WireMessage from_can{};
		ASSERT_TRUE(rpi::protocol::unpack_can(can_id, can_data, can_dlc, from_can));

		// compare the two deserialized messages
		EXPECT_EQ(from_uart.id, from_can.id);
		EXPECT_EQ(from_uart.dlc, from_can.dlc);
		EXPECT_EQ(from_uart.seq, from_can.seq);
		EXPECT_EQ(from_uart.crc, from_can.crc);
		EXPECT_EQ(std::memcmp(from_uart.payload, from_can.payload, from_uart.dlc), 0);
	}

	// Check that deserialize_uart rejects a frame with a bad STX marker.
	// Ensures that the decoder does not accept frames with corrupted STX
	TEST(Protocol, RejectsBadStx)
	{
		WireMessage msg{};
		msg.id = ::protocol::MSG_PING;
		uint8_t buffer[uart_max_frame]{};
		int len = rpi::protocol::serialize_uart(msg, buffer, sizeof(buffer));
		ASSERT_GT(len, 0);

		buffer[0] = 0x00; // Corrupt the STX marker.
		WireMessage decoded{};
		EXPECT_FALSE(rpi::protocol::deserialize_uart(buffer, len, decoded));
	}

	// Check that deserialize_uart rejects a frame with a bad ETX marker.
	TEST(Protocol, RejectsBadEtx)
	{
		WireMessage msg{};
		msg.id = ::protocol::MSG_PING;
		uint8_t buffer[uart_max_frame]{};
		int len = rpi::protocol::serialize_uart(msg, buffer, sizeof(buffer));
		ASSERT_GT(len, 0);

		buffer[len - 1] = 0x00; // Corrupt the ETX marker.
		WireMessage decoded{};
		EXPECT_FALSE(rpi::protocol::deserialize_uart(buffer, len, decoded));
	}

	// Check that deserialize_uart rejects a frame that is too short to contain a complete header.
	TEST(Protocol, RejectsTruncatedFrame)
	{
		uint8_t bad[] = { stx, 0x00, 0x01 };
		WireMessage decoded{};
		EXPECT_FALSE(rpi::protocol::deserialize_uart(bad, sizeof(bad), decoded));
	}

	// Check that deserialize_uart rejects a frame with a DLC that exceeds the maximum allowed payload size.
	TEST(Protocol, RejectsOversizedDlc)
	{
		uint8_t bad[uart_max_frame]{};
		bad[0] = stx;
		bad[1] = 0x00;
		bad[2] = 0x01;
		bad[3] = 0xFF; // DLC far above max_payload.
		WireMessage decoded{};
		EXPECT_FALSE(rpi::protocol::deserialize_uart(bad, sizeof(bad), decoded));
	}

	// Check that deserialize_uart rejects a frame with a bad CRC, even if the framing is correct.
	TEST(Protocol, RejectsBadCrc)
	{
		WireMessage msg{};
		msg.id = ::protocol::MSG_SENSOR_DATA;
		msg.dlc = 2;
		msg.seq = 1;
		msg.payload[0] = 0x11;
		msg.payload[1] = 0x22;

		uint8_t buffer[uart_max_frame]{};
		int len = rpi::protocol::serialize_uart(msg, buffer, sizeof(buffer));
		ASSERT_GT(len, 0);

		buffer[len - 2] ^= 0xFF; // Flip the CRC byte (just before ETX).
		WireMessage decoded{};
		EXPECT_FALSE(rpi::protocol::deserialize_uart(buffer, len, decoded));
	}

	// Check that a message with the maximum allowed payload size can be serialized and deserialized over UART without errors.
	TEST(Protocol, MaxPayloadUartRoundtrip)
	{
		WireMessage msg{};
		msg.id = ::protocol::MSG_SENSOR_DATA;
		msg.dlc = max_payload;
		msg.seq = 9;
		for (uint8_t i = 0; i < max_payload; ++i)
			msg.payload[i] = i;

		uint8_t buffer[uart_max_frame]{};
		int len = rpi::protocol::serialize_uart(msg, buffer, sizeof(buffer));
		ASSERT_EQ(len, uart_overhead + max_payload);

		WireMessage decoded{};
		ASSERT_TRUE(rpi::protocol::deserialize_uart(buffer, len, decoded));
		EXPECT_EQ(decoded.dlc, max_payload);
		EXPECT_EQ(std::memcmp(decoded.payload, msg.payload, max_payload), 0);
	}

	// Check that serialize_uart returns -1 when the provided buffer is too small to hold the serialized message.
	TEST(Protocol, SerializeFailsWhenBufferTooSmall)
	{
		WireMessage msg{};
		msg.id = ::protocol::MSG_PING;
		msg.dlc = 0;

		uint8_t tiny[2]{};
		EXPECT_EQ(rpi::protocol::serialize_uart(msg, tiny, sizeof(tiny)), -1);
	}

# pragma endregion Serialization tests

} // namespace tests
