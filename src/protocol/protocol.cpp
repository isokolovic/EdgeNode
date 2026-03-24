#include "protocol/protocol.h"

namespace edgenode::protocol {

// XOR over type + length + payload bytes
uint8_t compute_checksum(const Message& msg)
{
    uint8_t cs = static_cast<uint8_t>(msg.type);
    cs ^= msg.length;
    for (uint8_t i = 0; i < msg.length; ++i)
        cs ^= msg.payload[i];
    return cs;
}

int serialize(const Message& msg, uint8_t* buffer, int buffer_size)
{
    int total = 4 + msg.length; // START + TYPE + LEN + payload + CHECKSUM
    if (buffer_size < total)
        return -1;

    int i = 0;
    buffer[i++] = START_BYTE;
    buffer[i++] = static_cast<uint8_t>(msg.type);
    buffer[i++] = msg.length;
    for (uint8_t j = 0; j < msg.length; ++j)
        buffer[i++] = msg.payload[j];
    buffer[i++] = msg.checksum;

    return i;
}

bool deserialize(const uint8_t* buffer, int length, Message& msg)
{
    if (length < 4) // START + TYPE + LEN + CHECKSUM
        return false;
    if (buffer[0] != START_BYTE)
        return false;

    msg.type   = static_cast<MsgType>(buffer[1]);
    msg.length = buffer[2];

    if (msg.length > MAX_PAYLOAD) // Payload exceeds protocol limit
        return false;
    if (length < 4 + msg.length) // Not enough bytes for payload + checksum
        return false;

    for (uint8_t i = 0; i < msg.length; ++i)
        msg.payload[i] = buffer[3 + i];

    msg.checksum = buffer[3 + msg.length];
    return msg.checksum == compute_checksum(msg);
}

} // namespace edgenode::protocol