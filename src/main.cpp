#include "rpi/gpio.h"
#include "rpi/serial_comm.h"
#include "rpi/edge_protocol.h"
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>

// Toggle one GPIO pin and verify the readback.
bool test_gpio()
{
    std::cout << "\n=== GPIO Test ===\n";

    if (!edgenode::gpio::init())
    {
        std::cerr << "GPIO init failed\n";
        return false;
    }

    constexpr int PIN = 17;
    edgenode::gpio::set_pin_mode(PIN, edgenode::gpio::PinMode::OUTPUT);

    edgenode::gpio::write_pin(PIN, true);
    bool high = edgenode::gpio::read_pin(PIN);

    edgenode::gpio::write_pin(PIN, false);
    bool low = edgenode::gpio::read_pin(PIN);

    edgenode::gpio::cleanup();

    bool pass = high && !low;
    std::cout << "Pin " << PIN << " toggle: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

// -- Test 2: Serial ping/pong -------------------------------------------------
// Opens /dev/ttyACM0, sends a PING message to the Arduino, and waits
// up to 2 seconds for a PONG reply.  Verifies the response frame.

bool test_serial(const std::string& device)
{
    std::cout << "\n=== Serial Test ===\n";

    edgenode::serial::SerialPort port;
    if (!port.open(device, 9600))
    {
        std::cerr << "Cannot open " << device << "\n";
        return false;
    }

    // Opening the port resets the Arduino.
    std::cout << "Waiting for Arduino to reset...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Build a PING frame with no payload.
    edgenode::protocol::Message ping{};
    ping.type = edgenode::protocol::MsgType::PING;
    ping.length = 0;

    uint8_t tx_buf[edgenode::protocol::MAX_PAYLOAD + edgenode::protocol::OVERHEAD];
    int tx_len = edgenode::protocol::serialize(ping, tx_buf, sizeof(tx_buf));
    if (tx_len < 0)
    {
        std::cerr << "Failed to serialize PING\n";
        return false;
    }

    // Send the PING frame.
    std::cout << "Sending PING (" << tx_len << " bytes)...\n";
    if (port.write(tx_buf, tx_len) != tx_len)
    {
        std::cerr << "Write failed\n";
        return false;
    }

    // Collect bytes until at least a full header is available.
    uint8_t rx_buf[64]{};
    int rx_len = 0;
    int remaining_ms = 2000;

    while (rx_len < edgenode::protocol::OVERHEAD && remaining_ms > 0)
    {
        auto start = std::chrono::steady_clock::now();

        int n = port.read(rx_buf + rx_len, sizeof(rx_buf) - rx_len, remaining_ms);
        if (n < 0)
        {
            std::cerr << "Read error\n";
            return false;
        }
        rx_len += n;

        // Reduce the remaining timeout budget.
        auto elapsed = std::chrono::steady_clock::now() - start;
        remaining_ms -= static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    }

    if (rx_len < edgenode::protocol::OVERHEAD)
    {
        std::cerr << "Incomplete response (" << rx_len << " bytes received)\n";
        return false;
    }

    // Decode the response frame.
    edgenode::protocol::Message reply{};
    if (!edgenode::protocol::deserialize(rx_buf, rx_len, reply))
    {
        std::cerr << "Invalid response frame (" << rx_len << " bytes received)\n";
        return false;
    }

    bool pass = (reply.type == edgenode::protocol::MsgType::PONG);
    std::cout << "Response: "
              << (pass ? "PONG -- PASS" : "unexpected message -- FAIL") << "\n";
    return pass;
}

int main(int argc, char* argv[])
{
    std::cout << "EdgeNode Hardware Test\n";

    // Allow overriding the serial device from argv.
    std::string serial_device = "/dev/ttyACM0";
    if (argc > 1)
        serial_device = argv[1];

    bool gpio_ok = test_gpio();
    bool serial_ok = test_serial(serial_device);

    std::cout << "\n=== Summary ===\n";
    std::cout << "GPIO:   " << (gpio_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "Serial: " << (serial_ok ? "PASS" : "FAIL") << "\n";

    return (gpio_ok && serial_ok) ? 0 : 1;
}
