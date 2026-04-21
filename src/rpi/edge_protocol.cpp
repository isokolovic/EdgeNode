#include "rpi/edge_protocol.h"

namespace edgenode::protocol {

// Forward the RPi-facing API to the shared codec so protocol changes live in one place.
uint8_t compute_checksum(const Message& msg)
{
    return epcore::compute_checksum(static_cast<uint8_t>(msg.type), msg.length, msg.payload);
}

int serialize(const Message& msg, uint8_t* buffer, int buffer_size)
{
    return epcore::serialize(static_cast<uint8_t>(msg.type), msg.length, msg.payload, buffer, buffer_size);
}

bool deserialize(const uint8_t* buffer, int length, Message& msg)
{
    epcore::WireMessage wire_msg{};
    if (!epcore::deserialize(buffer, length, wire_msg))
        return false;

    msg.type = static_cast<MsgType>(wire_msg.type);
    msg.length = wire_msg.length;
    for (uint8_t i = 0; i < wire_msg.length; ++i)
        msg.payload[i] = wire_msg.payload[i];
    msg.checksum = wire_msg.checksum;
    return true;
}

}
