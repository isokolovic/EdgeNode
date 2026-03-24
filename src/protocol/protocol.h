#pragma once
#include <cstdint>

// Wire protocol for RPi <-> Arduino communication.
// Format: [START][TYPE][LENGTH][PAYLOAD...][CHECKSUM]
// Portable: no STL, no exceptions — compiles on C++23 and Arduino C++11.

namespace edgenode::protocol {

constexpr uint8_t START_BYTE  = 0xAA;
constexpr uint8_t MAX_PAYLOAD = 32;

enum class MsgType : uint8_t {
    PING         = 0x01,
    PONG         = 0x02,
    SENSOR_DATA  = 0x10,
    GPIO_COMMAND = 0x20,
    ACK          = 0xFE,
    ERROR        = 0xFF,
};

struct Message {
    MsgType type;
    uint8_t length;
    uint8_t payload[MAX_PAYLOAD];
    uint8_t checksum;
};

uint8_t compute_checksum(const Message& msg);
int  serialize(const Message& msg, uint8_t* buffer, int buffer_size);
bool deserialize(const uint8_t* buffer, int length, Message& msg);

} // namespace edgenode::protocol