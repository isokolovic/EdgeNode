#pragma once

#include <cstdint>
#include <string>

namespace rpi::serial {

/// @brief Abstract transport interface implemented by every HAL port (UartPort,
/// CanPort). Code above the HAL programs against IPort, never a concrete type,
/// so the transport can be swapped without touching higher layers.
class IPort
{
public:
	virtual ~IPort() = default;

	/// @brief Open the transport at the given device and link speed.
	virtual bool open(const std::string& device, int speed) = 0;

	/// @brief Close the transport and release the underlying resource.
	virtual void close() = 0;

	/// @brief Send raw bytes. Returns bytes written or -1 on error.
	virtual int write(const uint8_t* data, int length) = 0;

	/// @brief Read raw bytes with a timeout. Returns bytes read, 0 on timeout,
	/// or -1 on error. Every read path must honour the timeout.
	virtual int read(uint8_t* buffer, int max_length, int timeout_ms) = 0;

	/// @brief Return true when the transport is open.
	virtual bool is_open() const = 0;
};

}
