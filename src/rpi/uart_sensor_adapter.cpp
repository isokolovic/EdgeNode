#include "rpi/uart_sensor_adapter.h"

#include <chrono>

#include "rpi/edge_protocol.h"
#include "sensor_payload.h"

namespace rpi::protocol {

namespace {

// Current time in milliseconds. Used to stamp a reading when it arrives, since the wire frame itself does not carry a timestamp.
uint64_t now_ms()
{
	using namespace std::chrono;
	return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace

UartSensorAdapter::UartSensorAdapter(serial::IPort& port)
	: port(port)
{
}

bool UartSensorAdapter::consume(uint8_t byte, coretypes::SensorReading& out)
{
	// Not currently in a frame. Ignore everything until we see STX, which marks the start of a new frame.
	if (frame_len == 0)
	{
		if (byte != ::protocol::stx)
			return false;

		frame[frame_len++] = byte;
		return false;
	}

	frame[frame_len++] = byte;

	// Byte at index 3 is DLC (frame is STX, ID_HI, ID_LO, DLC, ...).
	// If it is bigger than the max payload allowed, something is wrong with the stream.
	// Throw away what we have and start looking for the next STX.
	if (frame_len == 4 && frame[3] > ::protocol::max_payload)
	{
		frame_len = 0;
		++crc_err;
		return false;
	}

	if (frame_len < 4)
		return false;

	// Now that we know DLC, we know the full frame length. Keep collecting bytes until we have that many.
	const int total = ::protocol::uart_overhead + frame[3];
	if (frame_len < total)
		return false;


	// We have a full frame. Reset frame_len now so the next call to consume() starts a fresh frame, 
	// regardless of what happens below.
	frame_len = 0;

	::protocol::WireMessage msg{};
	if (!rpi::protocol::deserialize_uart(frame, total, msg))
	{
		++crc_err;
		return false;
	}

	// Check the sequence number against what we expected from this sender.
	// The very first frame we ever see just sets the starting point, nothing to compare it against yet.
	if (rx_seq_valid && ::protocol::seq_gap(expected_rx_seq, msg.seq) != 0)
		++seq_gap_count;
	

	expected_rx_seq = static_cast<uint8_t>(msg.seq + 1);
	rx_seq_valid = true;

	// The frame is valid, but it might not be a sensor reading (e.g. it could be a PONG). 
	// If it is not SENSOR_DATA, just drop it, no error counted.
	if (!::protocol::decode_sensor_reading(msg, out))
		return false;

	out.timestamp_ms = now_ms();
	return true;
}

bool UartSensorAdapter::poll(coretypes::SensorReading& out, int timeout_ms)
{
	// First use up any bytes left over from the last call to poll(), before doing a new read on the port. 
	// A previous port.read() may have returned more than one frame at once, and we only decode one frame per poll() call.
	while (input_pos < input_len)
	{
		if (consume(input[input_pos++], out))
			return true;
	}

	// input[] is now fully used up, reset it before reading more.
	input_pos = 0;
	input_len = 0;

	if (!port.is_open())
		return false;

	const int received = port.read(input, static_cast<int>(sizeof(input)), timeout_ms);
	if (received <= 0)
		return false;

	input_len = received;

	while (input_pos < input_len)
	{
		if (consume(input[input_pos++], out))
			return true;
	}

	return false;
}

bool UartSensorAdapter::send_gpio_command(uint8_t pin, bool state)
{
	if (!port.is_open())
		return false;

	::protocol::WireMessage msg{};
	msg.id = ::protocol::MSG_GPIO_COMMAND;
	msg.dlc = 2;
	msg.seq = tx_seq.next();
	msg.payload[0] = pin;
	msg.payload[1] = state ? 1 : 0;

	uint8_t buffer[::protocol::uart_max_frame];
	const int length = rpi::protocol::serialize_uart(msg, buffer, static_cast<int>(sizeof(buffer)));
	if (length < 0)
		return false;

	return port.write(buffer, length) == length;
}

}
