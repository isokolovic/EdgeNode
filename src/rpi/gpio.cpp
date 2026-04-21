#include "rpi/gpio.h"
#include <iostream>

#ifdef EDGENODE_MOCK_GPIO

// Stub implementation used when GPIO hardware is not available.

namespace edgenode::gpio {

bool init() { return true; }
void cleanup() {}
void set_pin_mode(int, PinMode) {}
void write_pin(int, bool) {}
bool read_pin(int) { return false; }

}

#else

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>

namespace {
    // Size of the BCM2835 GPIO register block.
    constexpr size_t GPIO_LEN = 0xB4;
    // Base pointer returned by mmap.
    volatile unsigned* gpio_map = nullptr;
}

namespace edgenode::gpio {

bool init()
{
    // Open GPIO memory through the kernel driver.
    int fd = open("/dev/gpiomem", O_RDWR | O_SYNC);
    if (fd < 0)
    {
        std::cerr << "Can't open /dev/gpiomem: " << strerror(errno) << "\n";
        return false;
    }

    // Map the register block into this process.
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
    // Each function select register controls 10 pins.
    int reg = pin / 10;
    int shift = (pin % 10) * 3;

    unsigned val = *(gpio_map + reg);
    // Clear the current mode bits first.
    val &= ~(7u << shift);

    if (mode == PinMode::OUTPUT)
        // Output mode is encoded as 001.
        val |= (1u << shift);

    *(gpio_map + reg) = val;
}

void write_pin(int pin, bool value)
{
    // GPSET is at offset 7 and GPCLR is at offset 10.
    int offset = value ? 7 : 10;
    *(gpio_map + offset) = 1u << pin;
}

bool read_pin(int pin)
{
    // GPLEV starts at offset 13.
    int reg = pin / 32;
    int shift = pin % 32;
    return (*(gpio_map + 13 + reg) & (1u << shift)) != 0;
}

}

#endif
