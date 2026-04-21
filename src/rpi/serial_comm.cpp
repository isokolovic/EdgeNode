#include "rpi/serial_comm.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <iostream>

namespace {

// Map an integer baud rate to the POSIX constant.
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

namespace edgenode::serial {

SerialPort::~SerialPort() { close(); }

SerialPort::SerialPort(SerialPort&& other) noexcept : fd_(other.fd_)
{
    // Leave the moved-from object empty.
    other.fd_ = -1;
}

SerialPort& SerialPort::operator=(SerialPort&& other) noexcept
{
    if (this != &other)
    {
        // Close the current descriptor before taking ownership.
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

bool SerialPort::open(const std::string& device, int baud_rate)
{
    // Open the device in non-blocking mode.
    fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0)
    {
        std::cerr << "Cannot open " << device << ": " << strerror(errno) << "\n";
        return false;
    }

    // Start from the current terminal settings.
    struct termios tty{};

    if (tcgetattr(fd_, &tty) != 0)
    {
        std::cerr << "tcgetattr failed: " << strerror(errno) << "\n";
        close();
        return false;
    }

    // Apply the same baud rate to input and output.
    speed_t speed = to_speed(baud_rate);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    // Configure 8N1 with no flow control.
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    tty.c_lflag = 0;
    tty.c_oflag = 0;

    // Reads stay non-blocking and poll() handles waiting.
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd_, TCSANOW, &tty) != 0)
    {
        std::cerr << "tcsetattr failed: " << strerror(errno) << "\n";
        close();
        return false;
    }

    // Drop any stale bytes buffered by the driver.
    tcflush(fd_, TCIOFLUSH);

    std::cout << "Opened " << device << " @ " << baud_rate << " baud\n";
    return true;
}

void SerialPort::close()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
        fd_ = -1;
    }
}

int SerialPort::write(const uint8_t* data, int length)
{
    if (fd_ < 0 || !data) return -1;
    return static_cast<int>(::write(fd_, data, static_cast<size_t>(length)));
}

int SerialPort::read(uint8_t* buffer, int max_length, int timeout_ms)
{
    if (fd_ < 0 || !buffer)
        return -1;

    // Wait for input or until the timeout expires.
    struct pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0)
        return -1;
    if (ret == 0)
        return 0;

    return static_cast<int>(::read(fd_, buffer, static_cast<size_t>(max_length)));
}

bool SerialPort::is_open() const { return fd_ >= 0; }

}
