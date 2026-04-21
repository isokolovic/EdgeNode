#pragma once

#include <stdint.h>
#include <string.h>

namespace epcore {

constexpr uint8_t start_byte = 0xAA;
constexpr uint8_t max_payload = 32;
constexpr int overhead = 4;

/// @brief Shared wire message type values used on both Raspberry Pi and Arduino.
enum MessageType : uint8_t
{
    MSG_PING = 0x01,
    MSG_PONG = 0x02,
    MSG_SENSOR_DATA = 0x10,
    MSG_GPIO_COMMAND = 0x20,
    MSG_ACK = 0xFE,
    MSG_ERROR = 0xFF,
};

/// @brief Shared wire message layout used by the core codec helpers.
struct WireMessage
{
    uint8_t type;
    uint8_t length;
    uint8_t payload[max_payload];
    uint8_t checksum;
};

/// @brief Compute XOR checksum over type, length, and payload bytes.
inline uint8_t compute_checksum(uint8_t type, uint8_t length, const uint8_t* payload)
{
    uint8_t checksum = type ^ length;
    for (uint8_t i = 0; i < length && i < max_payload; ++i)
        checksum ^= payload[i];
    return checksum;
}

/// @brief Serialize a message into START, TYPE, LENGTH, PAYLOAD, CHECKSUM wire format.
inline int serialize(uint8_t type, uint8_t length, const uint8_t* payload, uint8_t* buffer, int buffer_size)
{
    if (!buffer || length > max_payload)
        return -1;

    int total = overhead + length;
    if (total > buffer_size)
        return -1;

    int pos = 0;
    buffer[pos++] = start_byte;
    buffer[pos++] = type;
    buffer[pos++] = length;

    if (length > 0)
    {
        memcpy(&buffer[pos], payload, length);
        pos += length;
    }

    buffer[pos++] = compute_checksum(type, length, payload);
    return pos;
}

/// @brief Deserialize a wire buffer into a shared wire message and validate the checksum.
inline bool deserialize(const uint8_t* buffer, int length, WireMessage& msg)
{
    if (!buffer || length < overhead)
        return false;

    int pos = 0;
    if (buffer[pos++] != start_byte)
        return false;

    msg.type = buffer[pos++];
    msg.length = buffer[pos++];

    if (msg.length > max_payload)
        return false;

    if (length < overhead + msg.length)
        return false;

    if (msg.length > 0)
    {
        memcpy(msg.payload, &buffer[pos], msg.length);
        pos += msg.length;
    }

    msg.checksum = buffer[pos];
    return msg.checksum == compute_checksum(msg.type, msg.length, msg.payload);
}

} // namespace epcore
