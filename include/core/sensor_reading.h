#pragma once

#include <cstdint>
#include <chrono>

//Test

namespace edgenode::core {

/// @brief Identifies the source of a sensor reading.
enum class SensorSource : uint8_t
{
    UNKNOWN = 0,
    DHT11_TEMPERATURE = 1,
    DHT11_HUMIDITY = 2,
    SOUND = 3,
    LIGHT = 4,
    JOYSTICK = 5,
    HALL_EFFECT = 6,
    BUTTON = 7,
};

/// @brief Indicates the quality or reliability of a sensor reading.
enum class ReadingQuality : uint8_t
{
	GOOD = 0, //Fresh data, reliable reading
	STALE = 1, //Data is old but still usable, may be less reliable
	ESTIMATED = 2, //Value is estimated based on previous readings or other sensors, not directly measured
	BAD = 3, //Data is unreliable or invalid, should not be used for decision making
};

/// @brief A single sensor measurement with metadata.
struct SensorReading
{
    std::chrono::steady_clock::time_point timestamp;
    SensorSource source = SensorSource::UNKNOWN;
    float value = 0.0f;
    ReadingQuality quality = ReadingQuality::GOOD;
};

} // namespace edgenode::core
