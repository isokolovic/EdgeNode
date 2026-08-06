#pragma once

// Arduino-side codec wrapping the shared protocol frame.
// All constants and codec logic come from the shared edge_protocol_core.h.
// The frame type that crosses this boundary is the shared protocol::WireMessage;
// the message kind is carried in its id field (protocol::MessageId).

#include "edge_protocol_core.h"
#include <stdint.h>

// Split namespace form (not the C++17 "namespace arduino::protocol") because the
// Arduino target is built with avr-gcc at C++11, which lacks nested namespaces.
namespace arduino {
namespace protocol {

// Constants from the shared protocol core, for convenience
static const uint8_t max_payload = ::protocol::max_payload;
static const uint8_t stx = ::protocol::stx;
static const uint8_t etx = ::protocol::etx;
static const int uart_overhead = ::protocol::uart_overhead;
static const int uart_max_frame = ::protocol::uart_max_frame;

/// @brief Compute the CRC-8/CCITT integrity check over id, dlc, seq, and payload.
uint8_t compute_crc(const ::protocol::WireMessage& msg);

/// @brief Serialize a frame into the UART envelope. Recomputes the CRC.
/// Returns bytes written or -1 on error.
int serialize_uart(const ::protocol::WireMessage& msg, uint8_t* buffer, int buffer_size);

/// @brief Deserialize and validate a UART-framed frame. Returns true when valid.
bool deserialize_uart(const uint8_t* buffer, int length, ::protocol::WireMessage& msg);

/// @brief Pack a frame into a CAN data field ([SEQ][payload][CRC]). Recomputes the CRC.
/// Returns the CAN DLC and the CAN id, or -1 on error.
int pack_can(const ::protocol::WireMessage& msg, uint16_t& out_can_id, uint8_t* data, int data_size);

/// @brief Unpack and validate a CAN data field into a frame. Returns true when valid.
bool unpack_can(uint16_t can_id, const uint8_t* data, int can_dlc, ::protocol::WireMessage& msg);

} // namespace protocol
} // namespace arduino
