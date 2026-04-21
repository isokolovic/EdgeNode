#pragma once

#include <cstdint>
#include "edge_protocol_core.h"

namespace edgenode::protocol {

constexpr uint8_t START_BYTE = epcore::start_byte;
constexpr uint8_t MAX_PAYLOAD = epcore::max_payload;
constexpr int OVERHEAD = epcore::overhead;

/// @brief Wire message types shared by the Raspberry Pi and Arduino.
enum class MsgType : uint8_t
{
    PING = epcore::MSG_PING, // Ping message to check connectivity.
    PONG = epcore::MSG_PONG, // Response to a ping message.
    SENSOR_DATA = epcore::MSG_SENSOR_DATA, // Sensor data message.
    GPIO_COMMAND = epcore::MSG_GPIO_COMMAND, // Command to control GPIO pins.
    ACK = epcore::MSG_ACK, // Acknowledgment message for successful receipt.
    ERROR = epcore::MSG_ERROR, // Error message.
};

/// @brief Wire message payload and metadata.
struct Message
{
    MsgType type; // Message type
    uint8_t length; // Number of bytes in the payload
    uint8_t payload[MAX_PAYLOAD]; // Payload data (up to MAX_PAYLOAD bytes)
    uint8_t checksum; // Checksum for validating the message integrity
};

/// @brief Compute XOR checksum over type + length + payload.
uint8_t compute_checksum(const Message& msg);

/// @brief Pack a message into a byte buffer and return bytes written or -1 on error.
int serialize(const Message& msg, uint8_t* buffer, int buffer_size);

/// @brief Unpack a byte buffer and return true when the frame is valid.
bool deserialize(const uint8_t* buffer, int length, Message& msg);

}
