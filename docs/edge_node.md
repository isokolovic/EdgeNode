# EdgeNode

EdgeNode is an embedded edge gateway running C++23 on a Raspberry Pi. It reads sensors, receives structured frames from an Arduino over UART (Universal Asynchronous Receiver-Transmitter) or CAN (Controller Area Network), processes the data locally, and publishes telemetry. The Arduino runs C++11 as a sensor node.

## Architecture

The system is six layers deep. Each layer owns one concern and enforces one boundary. Data flows up from hardware to telemetry. Control flows down from configuration to actuators.

**Layer 1 -- Hardware abstraction (HAL)**

- Wraps every hardware resource (GPIO pin, serial port, SPI bus, CAN socket) in an RAII (Resource Acquisition Is Initialization) object that acquires on construction and releases on destruction.
- No file descriptor, memory mapping, or device handle may exist outside a wrapper.
- No heap allocation. All HAL objects use stack or static storage.
- All transport wrappers implement a common port interface so higher layers never depend on a concrete transport type.
- When the build system detects a non-ARM host (Windows, WSL2), all hardware calls route to mock implementations that log to stdout. The binary must not crash on platforms without GPIO.

**Layer 2 -- Protocol adapters**

- Each adapter reads from one transport (CAN bus, UART stream, or direct GPIO sensor) and emits a unified reading type into the message bus.
- The reading type is the only data structure that crosses this boundary. Raw frames, register values, and device-specific encodings stay below.
- An adapter that fails to initialise (device absent, permission denied) logs a warning and is skipped. The system runs with whatever hardware is available.
- The UART adapter and CAN adapter share the same frame format. Only the physical transport differs.

**Layer 3 -- Async message bus**

- A lock-free single-producer single-consumer (SPSC) ring buffer connects each data source to the processing pipeline.
- No mutexes on the data-flow hot path. Synchronisation happens only at startup and shutdown.
- Worker threads use cooperative cancellation tokens for clean shutdown propagation.
- Buffer overflow policy is drop-oldest. The consumer always sees the freshest data.

**Layer 4 -- Processing and logic**

- Composable pipeline stages that each accept a reading and either transform it, filter it, or trigger an action.
- Includes smoothing filters, calibration (gain/offset from configuration), threshold detection, multi-source fusion, and a rule engine driven by external configuration.
- State machines model ECU (Electronic Control Unit) behaviour using sum types and pattern matching.
- Diagnostic trouble codes are stored, cleared, and queried through a dedicated manager.

**Layer 5 -- Telemetry sinks**

- Pluggable output destinations behind a common sink interface: console, file (JSON lines), and MQTT broker.
- An async exporter batches readings and pushes to all active sinks on a background thread.
- Sink failures are isolated. One failing sink does not block the others.

**Layer 6 -- System service**

- The daemon runs as a managed service with automatic restart on failure and structured logging.
- A watchdog timer ensures the process is restarted if it stops responding.

## Hardware

**Sensors (inputs)**

- DHT11 temperature and humidity sensor, connected to an RPi GPIO pin. Produces ambient environment readings.
- Sound/vibration sensor, connected to an RPi GPIO pin. Produces knock-event readings.
- Photoresistor, connected to an RPi GPIO pin. Produces light-level readings used for tamper detection.
- Joystick on an Arduino analog input. Produces a throttle percentage signal.
- Hall-effect sensor on an Arduino digital input. Produces wheel speed / RPM readings.
- Button/touch sensor on an Arduino digital input. Produces discrete event signals (door, ignition).

**Actuators (outputs)**

- Relay module on an RPi GPIO pin. Switched by the rule engine in response to threshold breaches.
- Buzzer on an RPi GPIO pin. Driven as an alarm output.
- RGB LED on three RPi GPIO pins. Displays daemon state.

**Communication interfaces**

- USB cable (Type-A to Type-B) connects the RPi to the Arduino for UART communication.
- Two MCP2515 CAN controller modules (one per board) connected via SPI (Serial Peripheral Interface). This enables real CAN bus traffic. Without it, the Arduino sends CAN-compatible frames over UART using the same frame format.

## Wire Protocol

The wire protocol uses a fixed-structure binary frame shared across UART and CAN transports. A frame begins with a start-of-text marker (0xAA) and ends with an end-of-text marker (0x55). Between them sit four fields: a two-byte message identifier in big-endian order, a one-byte data length code (0 through 8, matching the CAN specification maximum), the payload bytes, and a one-byte integrity check.

The identifier uses the standard CAN 11-bit address space (0x0000 through 0x07FF). This means frames produced over UART are byte-identical to frames on a real CAN bus. 

The integrity check is CRC-8 using the CCITT polynomial (0x07), with no reflection and no final XOR. It is computed over the identifier bytes, the data length code, and the payload. 

The total frame overhead is 6 bytes (start marker, two identifier bytes, length code, CRC, end marker). Maximum frame size is 14 bytes.

Payload encoding is signal-specific. Integer signals use fixed-width unsigned types in network byte order. Scaled values (temperature, humidity) use a fixed-point encoding with a documented scale factor per signal. Floating-point payloads use IEEE 754 single precision.

## C++ Guidelines

- RAII for every hardware resource. Constructors acquire, destructors release. No manual cleanup in business logic.
- Move-only semantics on all HAL wrappers. Copy construction and copy assignment are deleted.
- No exceptions in HAL or protocol code. The Arduino toolchain disables C++ exception unwinding, and exceptions in destructors cause undefined behaviour. Functions that can fail return a result type that holds either a value or an error, or return bool.
- No heap allocation in the HAL layer. Heap allocation is non-deterministic in timing and fragments memory on constrained hardware. HAL objects use stack or static storage only.
- No `using namespace` in header files. It pollutes the include chain.
- All transport code programs against an abstract port interface, never a concrete type.
- Lock-free SPSC ring buffer for the message bus hot path. No mutexes on data flow.
- Worker threads use cooperative cancellation for shutdown. No forced thread termination.
- All reads from hardware or network must have a timeout. No unbounded blocking.
- Scoped enumerations for type safety. No implicit integer conversions for message types or pin modes.
- C++23 on the RPi and host builds. C++11 on the Arduino, with C-style prefixed naming and no namespaces.

The error-handling strategy is explicit return values everywhere below the application layer. Functions that can fail return either a boolean or a result type that discriminates between success and failure. Callers must check the result before using the value. No error is silently ignored.

The threading model is one producer thread per data source, one consumer thread for the processing pipeline. Threads are launched at startup and joined at shutdown. Cooperative cancellation tokens propagate stop requests. No shared mutable state between threads except the lock-free ring buffer.

## Implementation Phases

**Phase 1 -- Core types**

- Define the unified sensor reading struct with timestamp, source identifier, value, and quality fields.
- Implement a result type that holds either a success value or an error, replacing raw bool returns in new code.
- Implement a fixed-capacity lock-free SPSC ring buffer with compile-time sizing.
- Set up the test framework so it downloads and builds automatically as part of the CMake configure step.
- Verification: all core types compile on all three platforms (Windows MSVC, Linux GCC 13, ARM GCC). Ring buffer passes a concurrent stress test with one producer and one consumer thread and no data loss.

**Phase 2 -- Hardware abstraction**

- Wrap GPIO access in an RAII class that acquires the memory mapping on construction and releases it on destruction.
- Wrap UART access in a move-only port class behind the abstract port interface. All reads take a timeout.
- Define the abstract port interface with open, close, read, write, and status query methods.
- Verification: GPIO tests pass using the mock backend on all platforms. Serial port opens, sends, receives, and closes without resource leaks on real hardware.

**Phase 3 -- Wire protocol**

- Implement CRC-8/CCITT computation.
- Implement frame serialisation and deserialisation with start/end markers, two-byte identifiers, and CRC validation.
- Provide both a C++23 RPi-side codec and a C++11 Arduino-side codec, both producing byte-identical output for the same input.
- Verification: serialisation roundtrips for every defined message identifier. Malformed frames (bad CRC, truncated, oversized) are rejected. Both codecs produce identical wire bytes for the same logical message.

**Phase 4 -- Sensor integration**

- Implement protocol adapters that read from each transport and emit unified readings into the message bus.
- Implement direct GPIO sensor drivers (temperature, sound, light).
- Verification: each adapter produces valid readings from real hardware. Adapters for absent hardware skip cleanly without affecting others.

**Phase 5 -- State machines**

- Implement ECU state machines using sum types and pattern matching.
- Implement diagnostic trouble code storage and query.
- Verification: state machines transition correctly for all defined input sequences. Trouble codes persist across state transitions.

**Phase 6 -- Storage and persistence**

- Implement file-based storage for readings and diagnostic codes.
- Verification: data survives a clean shutdown and is readable on restart.

**Phase 7 -- Connectivity and transport**

- Implement MQTT telemetry sink with async batched export.
- Implement the remaining sink types (console, file).
- Verification: readings reach the broker and can be queried from a dashboard. Sink failures do not block the pipeline.

**Phase 8 -- System integration**

- Service management with automatic restart and structured logging.
- End-to-end test: sensors to dashboard with all layers running.
- Verification: the system starts, runs, and shuts down cleanly under normal and abnormal conditions.

## Testing

**Unit tests (no hardware required)**

- Run on every platform (Windows, WSL2, ARM) as part of the normal build.
- Cover core types (sensor reading construction, result type semantics, ring buffer push/pop/overflow/concurrency).
- Cover protocol codec (serialisation roundtrip, CRC validation, malformed frame rejection).
- Cover GPIO using the mock backend (init/cleanup, pin mode, read/write, multiple pins).
- Use the test framework downloaded via CMake. Each module gets its own test file and CMake target.

**Hardware integration tests (real hardware required)**

- Live in a dedicated test file under the hardware test namespace.
- Each test is a standalone function that exercises one hardware path (GPIO toggle and readback, serial PING/PONG roundtrip, CAN frame send/receive).
- The main entry point calls each test via a single commented-out line so individual tests can be enabled or disabled without modifying the test file.
- These tests are not run in CI. They run manually on the Raspberry Pi with the Arduino connected.
