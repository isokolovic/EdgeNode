// Shared SENSOR_DATA payload layout. Defined in the header because both the
// Raspberry Pi and the Arduino build compile the same encoding.

#pragma once

#include <stdint.h>
#include <string.h>

#include "core/sensor_reading.h"
#include "edge_protocol_core.h"

namespace protocol {

// SENSOR_DATA payload: [SOURCE][QUALITY][VALUE 0..3] = 6 bytes, exactly max_payload.
// The value (float, 4 bytes) is stored in little-endian order (least significant byte is first).
constexpr uint8_t sensor_payload_size = 6;

/// @brief Fill a frame with a SENSOR_DATA payload. The caller supplies the
/// sequence number; the CRC is computed here. Returns false on a null frame field.
/// SensorReading -> WireMessage
inline bool encode_sensor_reading(const coretypes::SensorReading& reading, uint8_t seq, WireMessage& msg)
{
	msg.id = MSG_SENSOR_DATA;
	msg.dlc = sensor_payload_size;
	msg.seq = seq;

	msg.payload[0] = static_cast<uint8_t>(reading.source);
	msg.payload[1] = static_cast<uint8_t>(reading.quality);

	// Copy the float bit pattern
	memcpy(&msg.payload[2], &reading.value, sizeof(float));

	finalize_crc(msg);
	return true;
}

/// @brief Decode a SENSOR_DATA frame into a reading - WireMessage -> SensorReading
/// The timestamp is not carried on the wire - caller stamps it on arrival. 
/// Returns false when the frame is not SENSOR_DATA, has the wrong length, or carries an unknown enum value.
inline bool decode_sensor_reading(const WireMessage& msg, coretypes::SensorReading& reading)
{
	if (msg.id != MSG_SENSOR_DATA || msg.dlc != sensor_payload_size)
		return false;

	if (msg.payload[0] >= static_cast<uint8_t>(coretypes::SensorSource::COUNT))
		return false;

	if (msg.payload[1] >= static_cast<uint8_t>(coretypes::ReadingQuality::COUNT))
		return false;

	reading.source = static_cast<coretypes::SensorSource>(msg.payload[0]);
	reading.quality = static_cast<coretypes::ReadingQuality>(msg.payload[1]);

	memcpy(&reading.value, &msg.payload[2], sizeof(float));
	return true;
}

} // namespace protocol
