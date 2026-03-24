#include "gpio/gpio.h"
#include <iostream>

int main()
{
    // GPIO is only available on Raspberry Pi — graceful skip elsewhere
    if (!edgenode::gpio::init())
    {
        std::cout << "No GPIO hardware — nothing to test.\n";
        return 0;
    }

    // Toggle pin 17 and verify readback
    constexpr int TEST_PIN = 17;
    edgenode::gpio::set_pin_mode(TEST_PIN, edgenode::gpio::PinMode::OUTPUT);

    edgenode::gpio::write_pin(TEST_PIN, true);
    bool high = edgenode::gpio::read_pin(TEST_PIN);

    edgenode::gpio::write_pin(TEST_PIN, false);
    bool low = edgenode::gpio::read_pin(TEST_PIN);

    std::cout << "Pin " << TEST_PIN << " toggle test: "
              << (high && !low ? "PASS" : "FAIL") << "\n";

    edgenode::gpio::cleanup();
    return 0;
};
}