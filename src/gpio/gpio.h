#pragma once

// GPIO hardware abstraction — memory-mapped I/O via /dev/gpiomem (BCM2835/2836).

namespace edgenode::gpio {

enum class PinMode { INPUT, OUTPUT };

/// @brief Initialize GPIO access by memory-mapping the registers.
bool init();

/// @brief Clean up GPIO access by unmapping the registers.
void cleanup();

/// @brief Set the mode of a GPIO pin (INPUT or OUTPUT).
void set_pin_mode(int pin, PinMode mode);

/// @brief Write a boolean value to a GPIO pin (true = HIGH, false = LOW).
void write_pin(int pin, bool value);

/// @brief Read the current value of a GPIO pin (true = HIGH, false = LOW).
bool read_pin(int pin);

} // namespace edgenode::gpio