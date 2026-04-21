#ifndef EDGENODE_ARDUINO_EDGE_PROTOCOL_H
#define EDGENODE_ARDUINO_EDGE_PROTOCOL_H

// Arduino-side wire protocol wrapper.
// All constants and codec logic come from the shared edge_protocol_core.h.

#include "edge_protocol_core.h"
#include <stdint.h>

static const uint8_t EP_START_BYTE = epcore::start_byte;
static const uint8_t EP_MAX_PAYLOAD = epcore::max_payload;
static const int EP_OVERHEAD = epcore::overhead;

/// @brief Wire message types shared by the Raspberry Pi and Arduino.
enum EPMsgType : uint8_t
{
    EP_PING = epcore::MSG_PING,
    EP_PONG = epcore::MSG_PONG,
    EP_SENSOR_DATA = epcore::MSG_SENSOR_DATA,
    EP_GPIO_COMMAND = epcore::MSG_GPIO_COMMAND,
    EP_ACK = epcore::MSG_ACK,
    EP_ERROR = epcore::MSG_ERROR,
};

/// @brief Wire message payload and metadata.
struct EPMessage
{
    uint8_t type;
    uint8_t length;
    uint8_t payload[EP_MAX_PAYLOAD];
    uint8_t checksum;
};

/// @brief Compute XOR checksum over type + length + payload.
uint8_t ep_compute_checksum(const EPMessage& msg);

/// @brief Serialize a message into a byte buffer and return bytes written or -1 on error.
int ep_serialize(const EPMessage& msg, uint8_t* buffer, int buffer_size);

/// @brief Deserialize a byte buffer and return true when the frame is valid.
bool ep_deserialize(const uint8_t* buffer, int length, EPMessage& msg);

#endif
