#include "gpio/gpio.h"
#include <iostream>

#ifdef _WIN32

// Windows: GPIO headers don't exist. Stubs let the project compile.

namespace edgenode::gpio {

bool init() { std::cout << "GPIO not available on Windows\n"; return false; }
void cleanup() {}
void set_pin_mode(int, PinMode) {}
void write_pin(int, bool) {}
bool read_pin(int) { return false; }

} // namespace edgenode::gpio

#else

// Real implementation — memory-mapped GPIO registers (BCM2835/2836).
// On WSL this compiles. init() -> fails, /dev/gpiomem doesn't exist.

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>

namespace {
    constexpr size_t GPIO_LEN = 0xB4; // Size of GPIO register block
    volatile unsigned* gpio_map = nullptr; // Pointer to mapped GPIO memory
}

namespace edgenode::gpio {

bool init()
{
    // Open the device that exposes GPIO registers
    int fd = open("/dev/gpiomem", O_RDWR | O_SYNC);
    if (fd < 0)
    {
        std::cerr << "Can't open /dev/gpiomem: " << strerror(errno) << "\n";
        return false;
    }

    // Map the GPIO registers into process memory
    void* mapped = mmap(nullptr, GPIO_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (mapped == MAP_FAILED)
    {
        std::cerr << "mmap failed: " << strerror(errno) << "\n";
        return false;
    }

    gpio_map = static_cast<volatile unsigned*>(mapped);
    return true;
}

void cleanup()
{
    if (gpio_map)
    {
        munmap(const_cast<unsigned*>(gpio_map), GPIO_LEN);
        gpio_map = nullptr;
    }
}

void set_pin_mode(int pin, PinMode mode)
{
    // Each FSEL register controls 10 pins, 3 bits per pin
    int reg   = pin / 10;
    int shift = (pin % 10) * 3;

    unsigned val = *(gpio_map + reg);
    val &= ~(7u << shift); // Clear 3 bits for this pin

    if (mode == PinMode::OUTPUT)
        val |= (1u << shift); // OUTPUT = 001

    *(gpio_map + reg) = val;
}

void write_pin(int pin, bool value)
{
    // SET register at offset +7, CLR register at offset +10
    int offset = value ? 7 : 10;
    *(gpio_map + offset) = 1u << pin;
}

bool read_pin(int pin)
{
    // Level registers start at offset +13
    int reg   = pin / 32;
    int shift = pin % 32;
    return (*(gpio_map + 13 + reg) & (1u << shift)) != 0;
}

} // namespace edgenode::gpio

#endif