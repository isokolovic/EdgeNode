#include "rpi/gpio.h"
#include "rpi/serial_comm.h"
#include "rpi/edge_protocol.h"
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>

// TEMP: Main currenly runs two hardware tests: GPIO and serial.
// Will be replaced with a proper test framework later. For now, this is a quick way to verify the HAL and protocol stack are working

// Toggle one GPIO pin and verify the readback.
// Under the mock backend read_pin() always returns false, so this test only
// really passes on the RPi. It is a hardware bring-up check, not a unit test.
bool test_gpio()
{
    std::cout << "\n=== GPIO Test ===\n";

    if (!rpi::gpio::init())
    {
        std::cerr << "GPIO init failed\n";
        return false;
    }

    constexpr int PIN = 17;
    rpi::gpio::set_pin_mode(PIN, rpi::gpio::PinMode::OUTPUT);

    rpi::gpio::write_pin(PIN, true);
    bool high = rpi::gpio::read_pin(PIN);

    rpi::gpio::write_pin(PIN, false);
    bool low = rpi::gpio::read_pin(PIN);

    rpi::gpio::cleanup();

    // Readback must follow the drive in both directions, otherwise the pin is
    // stuck or the register offsets are wrong.
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

    rpi::serial::UartPort port;
    if (!port.open(device, 9600))
    {
        std::cerr << "Cannot open " << device << "\n";
        return false;
    }

    // Opening the port resets the Arduino.
	// Wait a couple seconds for the bootloader to finish and the sketch to start.
    std::cout << "Waiting for Arduino to reset...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Build a PING frame with no payload.
    protocol::WireMessage ping{};
    ping.id = protocol::MSG_PING;
    ping.dlc = 0;
    ping.seq = 0;

	uint8_t tx_buf[rpi::protocol::uart_max_frame]; // Enough space for the largest possible frame.
    int tx_len = rpi::protocol::serialize_uart(ping, tx_buf, sizeof(tx_buf));
    if (tx_len < 0)
    {
        std::cerr << "Failed to serialize PING\n";
        return false;
    }

    // Send the PING frame.
    // A short write means the frame is incomplete on the wire, so treat it as failure.
    std::cout << "Sending PING (" << tx_len << " bytes)...\n";
    if (port.write(tx_buf, tx_len) != tx_len)
    {
        std::cerr << "Write failed\n";
        return false;
    }

    // Collect bytes until at least a full frame is available.
    // The reply can arrive split across several reads, so accumulate into one
    // buffer instead of assuming a single read returns the whole frame.
    uint8_t rx_buf[64]{};
    int rx_len = 0;
    int remaining_ms = 2000;

    // uart_overhead is the smallest valid frame (zero payload), so it is the
    // minimum byte count worth attempting to decode.
    while (rx_len < rpi::protocol::uart_overhead && remaining_ms > 0)
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
        // Without this, each iteration would restart the full 2 s and the loop
        // could hang far longer than intended.
        auto elapsed = std::chrono::steady_clock::now() - start;
        remaining_ms -= static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    }

    if (rx_len < rpi::protocol::uart_overhead)
    {
        std::cerr << "Incomplete response (" << rx_len << " bytes received)\n";
        return false;
    }

    // Decode the response frame.
    // This also validates STX/ETX and the CRC, so a successful decode proves the
    // link is clean, not just that some bytes arrived.
    protocol::WireMessage reply{};
    if (!rpi::protocol::deserialize_uart(rx_buf, rx_len, reply))
    {
        std::cerr << "Invalid response frame (" << rx_len << " bytes received)\n";
        return false;
    }

    bool pass = (reply.id == protocol::MSG_PONG);
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
