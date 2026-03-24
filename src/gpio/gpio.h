#pragma once

// GPIO hardware abstraction — memory-mapped I/O via /dev/gpiomem (BCM2835/2836).

namespace edgenode::gpio {

enum class PinMode { INPUT, OUTPUT };

bool init();
void cleanup();
void set_pin_mode(int pin, PinMode mode);
void write_pin(int pin, bool value);
bool read_pin(int pin);

} // namespace edgenode::gpio