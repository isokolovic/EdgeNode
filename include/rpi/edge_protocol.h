#pragma once

#include <cstdint>
#include "edge_protocol_core.h"

// RPi-side codec wrapping the shared protocol frame. The frame type that crosses
// this boundary is the shared protocol::WireMessage; the message kind is carried
// in its id field (protocol::MessageId).
namespace rpi::protocol {

// Constants from the shared protocol core, for convenience
constexpr uint8_t max_payload = ::protocol::max_payload;
constexpr uint8_t stx = ::protocol::stx;
constexpr uint8_t etx = ::protocol::etx;
constexpr int uart_overhead = ::protocol::uart_overhead;
constexpr int uart_max_frame = ::protocol::uart_max_frame;

/// @brief Compute the CRC-8/CCITT integrity check over id, dlc, seq, and payload.
uint8_t compute_crc(const ::protocol::WireMessage& msg);

/// @brief Serialise a frame into the UART envelope (STX/ETX framing).
/// Recomputes the CRC, then writes the frame. Returns bytes written or -1 on error.
int serialize_uart(const ::protocol::WireMessage& msg, uint8_t* buffer, int buffer_size);

/// @brief Deserialise and validate a UART-framed frame. Returns true when valid.
bool deserialize_uart(const uint8_t* buffer, int length, ::protocol::WireMessage& msg);

/// @brief Pack a frame into a CAN data field ([SEQ][payload][CRC]).
/// Recomputes the CRC. Returns the CAN DLC and the CAN id, or -1 on error.
int pack_can(const ::protocol::WireMessage& msg, uint16_t& out_can_id, uint8_t* data, int data_size);

/// @brief Unpack and validate a CAN data field into a frame. Returns true when valid.
bool unpack_can(uint16_t can_id, const uint8_t* data, int can_dlc, ::protocol::WireMessage& msg);

}
