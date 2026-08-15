#include "rpi/gpio.h"
#include <iostream>

// Implementation used when GPIO hardware is not available.
#ifdef EDGENODE_MOCK_GPIO

namespace rpi::gpio {

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
	// Volatile because the hardware can change the values at any time.
	// Unsigned because we want to avoid sign extension issues - e.g. if register has addr 0xFFFFFFFF, 
    // singed = -1 (wrong), left shift = 0xFFFFFFFE = -2 (wrong). 
    volatile unsigned* gpio_map = nullptr;
}

namespace rpi::gpio {

bool init()
{
    // Open GPIO memory through the kernel driver.
    // /dev/gpiomem exposes only the GPIO block
    // if using /dev/mem, root access is needed
    // O_RDWR = read and write
    // O_SYNC = keep mapping uncached - register writes must reach peripheral ASAP, 
    // not held in cache
    // | combines both bits 
    int fd = open("/dev/gpiomem", O_RDWR | O_SYNC);
    if (fd < 0)
    {
        std::cerr << "Can't open /dev/gpiomem: " << strerror(errno) << "\n";
        return false;
    }

    // Map the register block into this process.
    void* mapped = mmap(nullptr, GPIO_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    // After mmap the fd is no longer needed
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
	// 3 mode bits per pin, 32-bit register -> 10 pins fit, 2 bits unused.
	int reg = pin / 10;
	// Pin inside register * 3 bits per pin
	int shift = (pin % 10) * 3; 
    
	// Unsigned to avoid sign extension issues
	unsigned val = *(gpio_map + reg);

	// Clear the current mode bits first.
	// Canonical clear bitfield pattern: val = val & ~(mask_for_field)
	// 7u = 0b111 covers the full 3-bit field; without clearing, the |= below
	// would OR into leftover bits and produce a different mode.
	val &= ~(7u << shift);

	if (mode == PinMode::OUTPUT)
		// Output mode is encoded as 001.
		// Canonical set bitfield pattern
		val |= (1u << shift);

	// Write the new value back to the register.
	*(gpio_map + reg) = val;
}

void write_pin(int pin, bool value)
{
	// GPSET is at offset 7 and GPCLR (clear register) is at offset 10.
    // This allows set/clear register in single write    
	int offset = value ? 7 : 10;

    // Write 1 to the pin of chosen register
	*(gpio_map + offset) = 1u << pin;
}

bool read_pin(int pin)
{
	// GPLEV (pin level) starts at offset 13.
	// One bit per pin, 32 pins per register.
	int reg = pin / 32;
	int shift = pin % 32; 

	// Read the register, mask out the bit for this pin, and return true if it is set.
	return (*(gpio_map + 13 + reg) & (1u << shift)) != 0;
}

}

#endif

// GpioPin RAII wrapper.
namespace rpi::gpio {

GpioPin::GpioPin(int pin, PinMode mode) : pin(pin)
{
    // Acquire: configuring the pin is the resource acquisition.
    set_pin_mode(pin, mode);
}

GpioPin::~GpioPin()
{
	// Release the pin by setting it to INPUT mode (safe default). 
    // Important to do this in the destructor to avoid leaving the pin in an unknown state 
    // when the object goes out of scope.
    if (pin >= 0)
        set_pin_mode(pin, PinMode::INPUT);
}

GpioPin::GpioPin(GpioPin&& other) noexcept : pin(other.pin)
{
	// Invalidate the source so only one object ever reverts this pin.
    other.pin = -1;
}

GpioPin& GpioPin::operator=(GpioPin&& other) noexcept
{
    // Self-assignment check first, otherwise we would release the pin we are about to adopt.
    if (this != &other)
    {
		// Release what we already own before taking the new pin, or it leaks and 
        // leaves the pin in an unknown state.
        if (pin >= 0)
            set_pin_mode(pin, PinMode::INPUT);

        pin = other.pin;
		other.pin = -1; // -1 = invalid pin
    }
    return *this;
}

void GpioPin::write(bool value) { write_pin(pin, value); }

bool GpioPin::read() const { return read_pin(pin); }

}
