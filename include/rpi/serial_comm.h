#pragma once

#include <cstdint>
#include <string>

#include "rpi/port.h"

namespace rpi::serial {

/// @brief POSIX UART port wrapper implementing the common transport interface.
/// Inherits IPort so everything above the HAL talks to a transport, not to a UART.
/// Move-only RAII: the fd is acquired in open() and released in the destructor,
/// so no caller ever has to close it by hand.
class UartPort : public IPort
{
public:
	UartPort() = default;
	~UartPort() override;

	/// @brief Disable copy construction.
	UartPort(const UartPort&) = delete;
	/// @brief Disable copy assignment.
	UartPort& operator=(const UartPort&) = delete;

	/// @brief Transfer ownership from another UART port.
	UartPort(UartPort&& other) noexcept;
	/// @brief Transfer ownership by move assignment.
	UartPort& operator=(UartPort&& other) noexcept;

	/// @brief Open a serial device at the given baud rate.
	bool open(const std::string& device, int baud_rate = 9600) override;

	/// @brief Close the serial port.
	void close() override;

	/// @brief Send raw bytes and return bytes written or -1 on error.
	int write(const uint8_t* data, int length) override;

	/// @brief Read raw bytes with timeout and return bytes read, 0 on timeout, or -1 on error.
	/// The timeout is mandatory by design: a blocking read with no deadline would
	/// stall the whole pipeline when the peer stops sending.
	int read(uint8_t* buffer, int max_length, int timeout_ms = 1000) override;

	/// @brief Return true when the serial port is open.
	bool is_open() const override;

private:
	// File descriptor for the serial port. -1 = "owns nothing" used
	// by is_open(), by the destructor, and to mark a moved-from object (move constructor/assignment).
	int fd = -1;
};

}
