#include "rpi/serial_comm.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <iostream>

namespace {

// Map an integer baud rate to the POSIX constant.
// Termios takes opaque speed_t constants, not plain numbers, so the caller-facing
// API can stay a simple int.
speed_t to_speed(int baud)
{
    switch (baud)
    {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    default: return B9600;
    }
}

}

namespace rpi::serial {

UartPort::~UartPort() { close(); }

UartPort::UartPort(UartPort&& other) noexcept : fd(other.fd)
{
    // Leave the moved-from object empty.
    other.fd = -1;
}

UartPort& UartPort::operator=(UartPort&& other) noexcept
{
    // Self-assignment check first, otherwise close() would destroy the file descriptor we are adopting.
    if (this != &other)
    {
        // Close the current descriptor before taking ownership.
        close();
        fd = other.fd;
        other.fd = -1; // -1 = invalid fd
    }
    return *this;
}

bool UartPort::open(const std::string& device, int baud_rate)
{
    // Open the device in non-blocking mode.
	// O_RDWR = read/write
	// O_NOCTTY = don't make this the controlling terminal (otherwise signals on the serial line could kill the process)
	// O_NONBLOCK = don't block on read() (we'll use poll() to wait for data, they provide the timeout)
    fd = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
    {
        std::cerr << "Cannot open " << device << ": " << strerror(errno) << "\n";
        return false;
    }

    // Start from the current terminal settings.
    struct termios tty{};

	if (tcgetattr(fd, &tty) != 0) // Get the current terminal attributes for the file descriptor.
    {
        std::cerr << "tcgetattr failed: " << strerror(errno) << "\n";
        close();
        return false;
    }

    // Apply the same baud rate to input and output.
    speed_t speed = to_speed(baud_rate);
    cfsetispeed(&tty, speed); // RX
	cfsetospeed(&tty, speed); // TX

    // Configure the control flags

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // Clear the size field, then set 8 data bits.

    tty.c_cflag |= CLOCAL | CREAD; // Ignore modem control lines, enable the receiver.
    tty.c_cflag &= ~(PARENB | PARODD); // No parity, the protocol CRC covers integrity.
    tty.c_cflag &= ~CSTOPB; // One stop bit.
    tty.c_cflag &= ~CRTSCTS; // No hardware flow control, only TX/RX/GND are wired.

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // No software flow control, 0x11/0x13 must pass as data.
    // No input translation: CR/NL rewriting or the 8th bit being stripped would mangle binary payloads.
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    tty.c_lflag = 0; // Raw mode: no canonical line buffering, no echo, no signal chars.
    tty.c_oflag = 0; // No output post-processing.

    // Reads stay non-blocking and poll() handles waiting.
    // VMIN 0 / VTIME 0 = return immediately with whatever is buffered.
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

	// Apply the settings to the serial port.

    // TCSANOW applies the settings immediately instead of draining pending output first.
    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        std::cerr << "tcsetattr failed: " << strerror(errno) << "\n";
        close();
        return false;
    }

    // Drop any stale bytes buffered by the driver.
    // Leftovers from a previous session would be read as the head of the first frame.
    tcflush(fd, TCIOFLUSH);

    std::cout << "Opened " << device << " @ " << baud_rate << " baud\n";
    return true;
}

void UartPort::close()
{
    if (fd >= 0)
    {
        ::close(fd);
        fd = -1;
    }
}

int UartPort::write(const uint8_t* data, int length)
{
    // Guard the closed and null cases 
    if (fd < 0 || !data || length < 0) return -1;

	// write() is non-blocking, so it may return before all bytes are sent
	// The caller is responsible for retrying until the whole frame is sent.
    return static_cast<int>(::write(fd, data, static_cast<size_t>(length)));
}

int UartPort::read(uint8_t* buffer, int max_length, int timeout_ms)
{
    if (fd < 0 || !buffer)
        return -1;

    // Wait for input or until the timeout expires.
    // poll() is what makes this a bounded read: the fd itself is non-blocking,
    // so without this the call would just spin returning nothing.
    struct pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0)
        return -1; // Error.
    if (ret == 0)
        return 0; // Timeout, distinct from an error so the caller can retry.

    // Data is ready, so this read returns at once. 
	// It may still be a partial frame: reassembly is the caller's job (see write() above).
    return static_cast<int>(::read(fd, buffer, static_cast<size_t>(max_length)));
}

bool UartPort::is_open() const { return fd >= 0; }

}
