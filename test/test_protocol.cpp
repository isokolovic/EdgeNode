#include "rpi/edge_protocol.h"
#include <cassert>
#include <cstring>
#include <iostream>

namespace edgenode::tests {

using namespace edgenode::protocol;

// Verify checksum generation for empty and non-empty payloads.
void test_checksum()
{
    Message msg{};
    msg.type = MsgType::PING;
    msg.length = 0;

    uint8_t cs = compute_checksum(msg);
    assert(cs == 0x01);

    msg.type = MsgType::SENSOR_DATA;
    msg.length = 2;
    msg.payload[0] = 0xAB;
    msg.payload[1] = 0xCD;

    cs = compute_checksum(msg);
    assert(cs == (0x10 ^ 0x02 ^ 0xAB ^ 0xCD));

    std::cout << "  checksum: PASS\n";
}

// Verify a serialized frame roundtrips back into the same message.
void test_roundtrip()
{
    Message original{};
    original.type = MsgType::SENSOR_DATA;
    original.length = 4;
    original.payload[0] = 0x41;
    original.payload[1] = 0xC8;
    original.payload[2] = 0x00;
    original.payload[3] = 0x00;

    uint8_t buffer[64]{};
    int len = serialize(original, buffer, sizeof(buffer));
    assert(len == OVERHEAD + original.length);

    Message decoded{};
    assert(deserialize(buffer, len, decoded));
    assert(decoded.type == original.type);
    assert(decoded.length == original.length);
    assert(std::memcmp(decoded.payload, original.payload, decoded.length) == 0);

    std::cout << "  roundtrip: PASS\n";
}

// Verify the smallest valid message serializes correctly.
void test_ping_pong()
{
    Message ping{};
    ping.type = MsgType::PING;
    ping.length = 0;

    uint8_t buffer[64]{};
    int len = serialize(ping, buffer, sizeof(buffer));
    assert(len == OVERHEAD);

    Message decoded{};
    assert(deserialize(buffer, len, decoded));
    assert(decoded.type == MsgType::PING);
    assert(decoded.length == 0);

    std::cout << "  ping/pong: PASS\n";
}

// Verify malformed frames are rejected.
void test_bad_data()
{
    Message msg{};

    uint8_t bad1[] = {0xBB, 0x01, 0x00, 0x01};
    assert(!deserialize(bad1, sizeof(bad1), msg));

    uint8_t bad2[] = {0xAA, 0x01};
    assert(!deserialize(bad2, sizeof(bad2), msg));

    uint8_t bad3[] = {0xAA, 0x01, 0x00, 0xFF};
    assert(!deserialize(bad3, sizeof(bad3), msg));

    uint8_t bad4[] = {0xAA, 0x01, 0xFF, 0x00};
    assert(!deserialize(bad4, sizeof(bad4), msg));

    std::cout << "  bad data rejection: PASS\n";
}

// Verify all message types survive a roundtrip.
void test_every_message_type()
{
    MsgType types[] = {
        MsgType::PING, MsgType::PONG, MsgType::SENSOR_DATA,
        MsgType::GPIO_COMMAND, MsgType::ACK, MsgType::ERROR
    };

    for (MsgType t : types)
    {
        Message msg{};
        msg.type = t;
        msg.length = 0;

        uint8_t buffer[64]{};
        int len = serialize(msg, buffer, sizeof(buffer));
        assert(len == OVERHEAD);

        Message decoded{};
        assert(deserialize(buffer, len, decoded));
        assert(decoded.type == t);
    }

    std::cout << "  all message types: PASS\n";
}

// Verify the maximum payload size is supported.
void test_max_payload()
{
    Message msg{};
    msg.type = MsgType::SENSOR_DATA;
    msg.length = MAX_PAYLOAD;
    for (uint8_t i = 0; i < MAX_PAYLOAD; ++i)
        msg.payload[i] = i;

    uint8_t buffer[MAX_PAYLOAD + OVERHEAD]{};
    int len = serialize(msg, buffer, sizeof(buffer));
    assert(len == OVERHEAD + MAX_PAYLOAD);

    Message decoded{};
    assert(deserialize(buffer, len, decoded));
    assert(decoded.length == MAX_PAYLOAD);
    assert(std::memcmp(decoded.payload, msg.payload, MAX_PAYLOAD) == 0);

    std::cout << "  max payload: PASS\n";
}

// Verify serialization fails when the output buffer is too small.
void test_buffer_too_small()
{
    Message msg{};
    msg.type = MsgType::PING;
    msg.length = 0;

    uint8_t tiny[2]{};
    int len = serialize(msg, tiny, sizeof(tiny));
    assert(len == -1);

    std::cout << "  buffer too small: PASS\n";
}

} // namespace edgenode::tests

int main()
{
    std::cout << "Protocol tests:\n";
    edgenode::tests::test_checksum();
    edgenode::tests::test_roundtrip();
    edgenode::tests::test_ping_pong();
    edgenode::tests::test_bad_data();
    edgenode::tests::test_every_message_type();
    edgenode::tests::test_max_payload();
    edgenode::tests::test_buffer_too_small();
    std::cout << "All protocol tests passed.\n";
    return 0;
}