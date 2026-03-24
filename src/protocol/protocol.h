#pragma once
#include <cstdint>

// Wire protocol for RPi <-> Arduino communication.
// Format: [START][TYPE][LENGTH][PAYLOAD...][CHECKSUM]

namespace edgenode::protocol {

constexpr uint8_t START_BYTE  = 0xAA;
constexpr uint8_t MAX_PAYLOAD = 32;

/// @brief Message types for the protocol.
enum class MsgType : uint8_t {
    PING         = 0x01,
    PONG         = 0x02,
    SENSOR_DATA  = 0x10,
    GPIO_COMMAND = 0x20,
    ACK          = 0xFE,
    ERROR        = 0xFF,
};

/// @brief Message structure for the protocol.
struct Message {
    MsgType type;
    uint8_t length;
    uint8_t payload[MAX_PAYLOAD];
    uint8_t checksum;
};

/// @brief Compute the checksum for a message (XOR of type, length, and payload).
uint8_t compute_checksum(const Message& msg);

/// @brief Pack a Message into a byte buffer. Returns bytes written, or -1 if buffer too small.
int serialize(const Message& msg, uint8_t* buffer, int buffer_size);

/// @brief Unpack a byte buffer into a Message. Returns false on bad frame or checksum mismatch.
bool deserialize(const uint8_t* buffer, int length, Message& msg);

} // namespace edgenode::protocol