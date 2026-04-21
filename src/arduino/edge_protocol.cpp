#include "arduino/edge_protocol.h"

// Forward the Arduino-facing API to the shared codec so protocol logic stays in one place.
uint8_t ep_compute_checksum(const EPMessage& msg)
{
    return epcore::compute_checksum(msg.type, msg.length, msg.payload);
}

int ep_serialize(const EPMessage& msg, uint8_t* buffer, int buffer_size)
{
    return epcore::serialize(msg.type, msg.length, msg.payload, buffer, buffer_size);
}

bool ep_deserialize(const uint8_t* buffer, int length, EPMessage& msg)
{
    epcore::WireMessage wire_msg{};
    if (!epcore::deserialize(buffer, length, wire_msg))
        return false;

    msg.type = wire_msg.type;
    msg.length = wire_msg.length;
    for (uint8_t i = 0; i < wire_msg.length; ++i)
        msg.payload[i] = wire_msg.payload[i];
    msg.checksum = wire_msg.checksum;
    return true;
}
