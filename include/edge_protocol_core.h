// Inline functions defined in .h file because they live in two separate systems (Arduino and Raspberry Pi) 
// that share the same protocol.

#pragma once

#include <stdint.h>
#include <string.h>

namespace protocol {

// Logical frame size limits. The CAN data field is 8 bytes, but it must carry the sequence counter (1 byte) 
// and the CRC (1 byte), leaving 6 payload bytes.
constexpr uint8_t max_payload = 6;

// CAN uses the standard 11-bit identifier space (0x000 through 0x7FF).
// Mask zeroes the upper 5 bits of a 16-bit logical frame id to ensure it fits in the CAN id field.
constexpr uint16_t id_mask = 0x07FF; // 0000011111111111

// UART envelope markers - start and end of a frame - bit patter is exact oposite of each other.
// Used when packing a CAN frame into a UART envelope, and when unpacking a UART envelope back into a CAN frame.
// STX opens a frame, ETX closes it.
constexpr uint8_t stx = 0xAA; //10101010
constexpr uint8_t etx = 0x55; //01010101

// Number of bytes in UART frame that are always present besides the payload.
// UART overhead = STX + ID_HI + ID_LO + DLC + SEQ + CRC + ETX (payload excluded).
// (ID_HI and ID_LO are the two bytes of the 11-bit CAN identifier).
constexpr int uart_overhead = 7;

// Largest UART frame: 2 markers + 2 ID + DLC + SEQ + 6 payload + CRC.
constexpr int uart_max_frame = uart_overhead + max_payload;

// CRC-8/CCITT polynomial (no reflection, no final XOR).
constexpr uint8_t crc_poly = 0x07; // 00000111

/// @brief Reserved message identifiers carried in the 2-byte logical frame id.
/// The logical frame borrows the CAN identifier as the message-kind selector,
/// so there is no separate type byte on the wire.
enum MessageId : uint16_t
{
    MSG_PING = 0x001,
    MSG_PONG = 0x002,
    MSG_SENSOR_DATA = 0x010,
    MSG_GPIO_COMMAND = 0x020,
    MSG_ACK = 0x7FE,
    MSG_ERROR = 0x7FF,
};

/// @brief Transport-independent logical frame shared by Raspberry Pi and Arduino.
struct WireMessage
{
	uint16_t id; // used to store 11-bit CAN identifier (0..0x7FF)
	uint8_t dlc; // data length code (0..6) - number of bytes in payload
	uint8_t seq; // sequence counter (0..255) - increments with each message sent
	uint8_t payload[max_payload]; // uint8_t array of length dlc
	uint8_t crc; // CRC-8/CCITT over id, dlc, seq, and payload
};

/// @brief Compute the CRC-8/CCITT (poly 0x07, init 0x00, bytes order not reversed).
// Different messages -> different CRC paths -> different final CRC -> error detection.
inline uint8_t crc8_ccitt(const uint8_t* data, int length)
{
    uint8_t crc = 0x00;
    for (int i = 0; i < length; ++i)
    {
        crc ^= data[i]; // XOR byte into 
        for (int bit = 0; bit < 8; ++bit)
        {
			if (crc & 0x80) // 10000000 -> if leftmost ON?
				// Shift one left (drop MSB) and XOR with 00000111 (0x07)
                // ^ flips the lowest 3 bits -> "records" dropped MSB was 1
                crc = static_cast<uint8_t>((crc << 1) ^ crc_poly);
            else
				// Drop MSB = multiply by 2
                crc = static_cast<uint8_t>(crc << 1); 
        }
    }

    return crc;
}

/// @brief Compute the logical-frame CRC over id, dlc, seq, and payload.
/// This is the integrity check; it is identical regardless of transport (UART or CAN).
inline uint8_t compute_crc(uint16_t id, uint8_t dlc, uint8_t seq, const uint8_t* payload)
{
    uint8_t scratch[2 + 1 + 1 + max_payload];// id(2) + dlc(1) + seq(1) + payload
    int pos = 0;

    // Split 16-bit id into bytes so CRC is byte-order independent
	// Mask with 0xFF (11111111) extracts lowest 8 bits of id, regardless of size or sign of id.
	scratch[pos++] = static_cast<uint8_t>((id >> 8) & 0xFF);// id high byte.
	scratch[pos++] = static_cast<uint8_t>(id & 0xFF);// id low byte.

    scratch[pos++] = dlc;// declared length -> included so a forged/mismatched dlc is caught
    scratch[pos++] = seq;// sequence number -> included so reordered/duplicated frames are caught

    // Copy payload bytes, but never past dlc or the scratch buffer's capacity
    // (dlc is untrusted input, so the max_payload bound protects against overflow)
    for (uint8_t i = 0; i < dlc && i < max_payload; ++i)
        scratch[pos++] = payload[i];

    // pos now holds the exact number of bytes actually written (header + payload)
    return crc8_ccitt(scratch, pos);
}

/// @brief Fill in the CRC field of a logical frame from its current contents.
inline void finalize_crc(WireMessage& msg)
{
    msg.crc = compute_crc(msg.id, msg.dlc, msg.seq, msg.payload);
}

/// @brief Serialise a logical frame into the UART envelope:
/// [STX][ID_HI][ID_LO][DLC][SEQ][payload...][CRC][ETX].
/// Returns the number of bytes written, or -1 on error. Does not recompute the CRC. 
/// Callers set msg.crc (typically via finalize_crc) before serialising.
inline int serialize_uart(const WireMessage& msg, uint8_t* buffer, int buffer_size)
{
	// Validate inputs: buffer must be non-null, msg.dlc must be <= max_payload, msg.id must fit in 11 bits.
    //~id_mask = 0xF800 = 1111100000000000, so this keeps bits outside the 11-bit range -> Does msg.id have 
    // any bits set outside the 11-bit range? If so, return -1.
	if (!buffer || msg.dlc > max_payload || (msg.id & ~id_mask) != 0) 
        return -1;

	// Check that the buffer is large enough to hold the entire UART frame (overhead + payload).
    int total = uart_overhead + msg.dlc;
    if (total > buffer_size)
        return -1;

    int pos = 0;
    buffer[pos++] = stx;

    buffer[pos++] = static_cast<uint8_t>((msg.id >> 8) & 0xFF); 
    buffer[pos++] = static_cast<uint8_t>(msg.id & 0xFF);

    buffer[pos++] = msg.dlc;
    buffer[pos++] = msg.seq;

	// Copy payload bytes, but never past msg.dlc or the buffer's capacity
	// checked above if msg.dlc > max_payload
    for (uint8_t i = 0; i < msg.dlc; ++i)
        buffer[pos++] = msg.payload[i];
    
    buffer[pos++] = msg.crc;
    buffer[pos++] = etx;
    return pos;
}

/// @brief Deserialise a UART envelope back into a logical frame and validate it.
/// Checks the start/end markers, the DLC bound, and the CRC. Returns true only
/// when the frame is well-formed and the CRC matches.
inline bool deserialize_uart(const uint8_t* buffer, int length, WireMessage& msg)
{
	// buffer must be non-null, length must be at least the minimum UART frame size (overhead).
    if (!buffer || length < uart_overhead)
        return false;

    int pos = 0;

	// Check that the first byte is the start-of-frame marker (STX).
    if (buffer[pos++] != stx)
        return false;

	// Extract the 2-byte id from the buffer
    // Yield first byte and increment pos. Shift left 8 bits to make room for the second byte.
	uint16_t id = static_cast<uint16_t>(buffer[pos++]) << 8; 
	id |= buffer[pos++]; // Yield second byte. Incorporate it into the id by ORing it with the first byte.
    
    uint8_t dlc = buffer[pos++];

    if (dlc > max_payload)
        return false;

    if (length < uart_overhead + dlc)
        return false;

    msg.id = id;
    msg.dlc = dlc;
    msg.seq = buffer[pos++];

    for (uint8_t i = 0; i < dlc; ++i)
        msg.payload[i] = buffer[pos++];

    msg.crc = buffer[pos++];

    if (buffer[pos++] != etx)
        return false;

    return msg.crc == compute_crc(msg.id, msg.dlc, msg.seq, msg.payload);
}

/// @brief Pack a logical frame into a CAN data field: [SEQ][payload...][CRC].
/// CAN hardware handles framing, so there are no STX/ETX markers. The logical
/// id is returned through out_can_id to be programmed into the CAN frame.
/// Returns the CAN DLC (1 + payload_len + 1), or -1 on error.
inline int pack_can(const WireMessage& msg, uint16_t& out_can_id, uint8_t* data, int data_size)
{
    // Validate inputs: buffer must be non-null, msg.dlc must be <= max_payload, msg.id must fit in 11 bits.
	// (any bits outside the 11-bit range must be zero). ~id_mask = 1111100000000000
    if (!data || msg.dlc > max_payload || (msg.id & ~id_mask) != 0)
        return -1;

	int can_dlc = 1 + msg.dlc + 1; // 1 byte for SEQ, msg.dlc bytes for payload, 1 byte for CRC
    if (can_dlc > data_size || can_dlc > 8) 
        return -1;

    out_can_id = msg.id;

    int pos = 0;
    data[pos++] = msg.seq;

    for (uint8_t i = 0; i < msg.dlc; ++i)
        data[pos++] = msg.payload[i];

    data[pos++] = msg.crc;

    return can_dlc;
}

/// @brief Unpack a CAN data field back into a logical frame and validate the CRC.
/// The CAN id supplies the logical id; the data field supplies SEQ, payload, CRC.
/// Returns true only when the frame is well-formed and the CRC matches.
inline bool unpack_can(uint16_t can_id, const uint8_t* data, int can_dlc, WireMessage& msg)
{
	// data must be non-null, can_dlc must be between 2 and 8 (inclusive).
	// 2 is the minimum because we need at least SEQ and CRC, and 8 is the maximum CAN data field size.
    if (!data || can_dlc < 2 || can_dlc > 8)
        return false;

    int payload_len = can_dlc - 2;
    if (payload_len > max_payload)
        return false;

	// CAN id is 11 bits. msg.id is 16 bits, so the upper 5 bits of msg.id must be zero. 
    // This is done by masking 0x07FF -> 0000011111111111
    msg.id = static_cast<uint16_t>(can_id & id_mask);
    msg.dlc = static_cast<uint8_t>(payload_len);

    int pos = 0;
    msg.seq = data[pos++];

    for (int i = 0; i < payload_len; ++i)
        msg.payload[i] = data[pos++];

    msg.crc = data[pos++];

    return msg.crc == compute_crc(msg.id, msg.dlc, msg.seq, msg.payload);
}

} // namespace protocol
