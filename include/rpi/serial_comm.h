#pragma once

#include <cstdint>
#include <string>

namespace edgenode::serial {

/// @brief POSIX serial port wrapper.
class SerialPort
{
public:
    SerialPort() = default;
    ~SerialPort();

    /// @brief Disable copy construction.
    SerialPort(const SerialPort&) = delete;
    /// @brief Disable copy assignment.
    SerialPort& operator=(const SerialPort&) = delete;

    /// @brief Transfer ownership from another serial port.
    SerialPort(SerialPort&& other) noexcept;
    /// @brief Transfer ownership by move assignment.
    SerialPort& operator=(SerialPort&& other) noexcept;

    /// @brief Open a serial device at the given baud rate.
    bool open(const std::string& device, int baud_rate = 9600);

    /// @brief Close the serial port.
    void close();

    /// @brief Send raw bytes and return bytes written or -1 on error.
    int write(const uint8_t* data, int length);

    /// @brief Read raw bytes with timeout and return bytes read, 0 on timeout, or -1 on error.
    int read(uint8_t* buffer, int max_length, int timeout_ms = 1000);

    /// @brief Return true when the serial port is open.
    bool is_open() const;

private:
    int fd_ = -1;
};

}
