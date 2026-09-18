#pragma once

#include <cstdint>

#include "core/sensor_reading.h"
#include "edge_protocol_core.h"
#include "rpi/port.h"

namespace rpi::protocol {

/// @brief Reads bytes from a UART port and turns them into SensorReading values.
/// UART just sends bytes with no message boundaries, so this class has to find
/// where each frame starts and ends, check the CRC, and check the sequence
/// number, before code above can get a reading.
class UartSensorAdapter
{
public:
	/// @brief Bind to an already-open port. This class does not open or close it.
	explicit UartSensorAdapter(serial::IPort& port); // No implicit copy or move


	/// @brief Read from the port and try to get one decoded sensor reading.
	/// Returns true and fills out if a full, valid SENSOR_DATA frame came in.
	/// Returns false if nothing came in within timeout_ms, if a frame failed the CRC check, 
	/// or if a valid frame came in but it wasn't SENSOR_DATA (for example a PONG reply)
	/// May not touch the port at all if there is already enough buffered data left over from 
	/// the previous call.
	bool poll(coretypes::SensorReading& out, int timeout_ms = 1000);

	/// @brief Send a GPIO_COMMAND frame telling the other board to set one pin.
	bool send_gpio_command(uint8_t pin, bool state);

	/// @brief How many frames were dropped because the CRC did not match.
	uint32_t crc_errors() const { return crc_err; }

	/// @brief How many frames arrived with a sequence number we did not expect (a dropped frame or a repeated one).
	uint32_t sequence_gaps() const { return seq_gap_count; }

private:

	/// @brief Add one byte to the frame being assembled.
	/// Returns true only when this byte was the last byte of a full, valid SENSOR_DATA frame, and out has been filled in. 
	/// Returns false for every other byte, including the last byte of a frame that turned out to be corrupt or not SENSOR_DATA.
	bool consume(uint8_t byte, coretypes::SensorReading& out);

	serial::IPort& port;

	// Bytes from the last port.read() (e.g. rpi::serial::UartPort::read()) call. 
	// One read can return more than one frame's worth of bytes (partial frame) so we keep them here and work
	// through them one at a time across calls.
	uint8_t input[64] = {};
	int input_len = 0; // number of valid bytes in input[], set after each port.read()
	int input_pos = 0; // index of the next byte in input[] we have not looked at yet

	// The frame currently being built, one byte at a time, by consume().
	uint8_t frame[::protocol::uart_max_frame] = {};
	// how many bytes of the current frame we have so far, reset to 0 once the frame is finished (good or bad)
	int frame_len = 0;

	::protocol::SequenceCounter tx_seq; // counter for frames we send out
	uint8_t expected_rx_seq = 0; // sequence number we expect on the next incoming frame
	bool rx_seq_valid = false; // false until the first incoming frame sets expected_rx_seq

	uint32_t crc_err = 0;
	uint32_t seq_gap_count = 0;
};

}
