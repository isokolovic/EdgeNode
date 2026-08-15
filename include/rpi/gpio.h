#pragma once

namespace rpi::gpio {

enum class PinMode { INPUT, OUTPUT };

// Free functions are the raw register layer: they have exactly one backend
// swapped at compile time (real /dev/gpiomem or the mock). GpioPin below is
// built on top of them.

/// @brief Map the GPIO registers into process memory.
/// Must succeed before any pin call, otherwise the register pointer is null.
bool init();

/// @brief Unmap the GPIO registers.
void cleanup();

/// @brief Configure a pin as input or output.
void set_pin_mode(int pin, PinMode mode);

/// @brief Drive a pin high or low.
void write_pin(int pin, bool value);

/// @brief Read the current pin level.
bool read_pin(int pin);

/// @brief RAII wrapper for a single configured GPIO pin.
///
/// Construction configures the pin in the requested mode (resource acquire);
/// destruction reverts it to INPUT (resource release). Move-only so a pin is
/// never owned twice. Requires init() to have mapped the register block first.
class GpioPin
{
public:
    /// @brief Configure the pin in the given mode.
    GpioPin(int pin, PinMode mode);
    /// @brief Revert the pin to INPUT on destruction.
    ~GpioPin();

    GpioPin(const GpioPin&) = delete;
    GpioPin& operator=(const GpioPin&) = delete;

    /// @brief Transfer ownership of the pin.
    GpioPin(GpioPin&& other) noexcept;
    /// @brief Transfer ownership by move assignment.
    GpioPin& operator=(GpioPin&& other) noexcept;

    /// @brief Drive the pin high or low (OUTPUT mode).
    void write(bool value);

    /// @brief Read the current pin level.
    bool read() const;

    /// @brief Return true when this object owns a pin.
    bool is_valid() const { return pin >= 0; }

private:
    // -1 = "owns nothing": it marks a moved-from object so the
    // destructor skips the release and the pin is never reverted twice.
    int pin = -1;
};

}
