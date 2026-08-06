#include "rpi/edge_protocol.h"

namespace rpi::protocol {

uint8_t compute_crc(const ::protocol::WireMessage& msg)
{
    return ::protocol::compute_crc(msg.id, msg.dlc, msg.seq, msg.payload);
}

int serialize_uart(const ::protocol::WireMessage& msg, uint8_t* buffer, int buffer_size)
{
    ::protocol::WireMessage wire = msg;
    ::protocol::finalize_crc(wire);
    return ::protocol::serialize_uart(wire, buffer, buffer_size);
}

bool deserialize_uart(const uint8_t* buffer, int length, ::protocol::WireMessage& msg)
{
    return ::protocol::deserialize_uart(buffer, length, msg);
}

int pack_can(const ::protocol::WireMessage& msg, uint16_t& out_can_id, uint8_t* data, int data_size)
{
    ::protocol::WireMessage wire = msg;
    ::protocol::finalize_crc(wire);
    return ::protocol::pack_can(wire, out_can_id, data, data_size);
}

bool unpack_can(uint16_t can_id, const uint8_t* data, int can_dlc, ::protocol::WireMessage& msg)
{
    return ::protocol::unpack_can(can_id, data, can_dlc, msg);
}

}
