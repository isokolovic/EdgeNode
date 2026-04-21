#pragma once

namespace edgenode::gpio {

enum class PinMode { INPUT, OUTPUT };

/// @brief Map the GPIO registers into process memory.
bool init();

/// @brief Unmap the GPIO registers.
void cleanup();

/// @brief Configure a pin as input or output.
void set_pin_mode(int pin, PinMode mode);

/// @brief Drive a pin high or low.
void write_pin(int pin, bool value);

/// @brief Read the current pin level.
bool read_pin(int pin);

}
