# EdgeNode

EdgeNode is an embedded edge gateway running C++23 on a Raspberry Pi. It reads sensors, receives structured frames from an Arduino over UART (Universal Asynchronous Receiver-Transmitter) or CAN (Controller Area Network), processes the data locally, and publishes telemetry. The Arduino runs C++11 as a sensor node.

## Architecture

The system is six layers deep. Each layer owns one concern and enforces one boundary. Data flows in both directions. Sensor data flows upstream from hardware to telemetry. Commands flow downstream from the web interface through the RPi to actuators on both boards.

**Layer 1 -- Hardware abstraction (HAL)**

- Wraps every hardware resource (GPIO pin, serial port, SPI bus, CAN socket) in an RAII (Resource Acquisition Is Initialization) object that acquires on construction and releases on destruction.
- No file descriptor, memory mapping, or device handle may exist outside a wrapper.
- No heap allocation. All HAL objects use stack or static storage.
- All transport wrappers implement a common port interface so higher layers never depend on a concrete transport type.
- When the build system detects a non-ARM host (WSL2 or a Linux workstation), all hardware calls route to mock implementations that log to stdout. The binary must not crash on platforms without GPIO.

**Layer 2 -- Protocol adapters**

- Each adapter reads from one transport (CAN bus, UART stream, or direct GPIO sensor) and emits a unified reading type into the message bus.
- The reading type is the only data structure that crosses this boundary. Raw frames, register values, and device-specific encodings stay below.
- An adapter that fails to initialise (device absent, permission denied) logs a warning and is skipped. The system runs with whatever hardware is available.
- Adapters handle both directions: upstream (sensor data from Arduino to RPi) and downstream (commands from RPi to Arduino actuators).
- The UART adapter and CAN adapter share the same logical frame format. Only the physical transport and serialisation envelope differ.

**Layer 3 -- Async message bus**

- A lock-free single-producer single-consumer (SPSC) ring buffer connects each data source to the processing pipeline.
- No mutexes on the data-flow hot path. Synchronisation happens only at startup and shutdown.
- Worker threads use cooperative cancellation tokens for clean shutdown propagation.
- Buffer overflow policy is drop-oldest. The consumer always sees the freshest data.
- Without the ring buffer, every sensor read would either block on the processing pipeline or require mutex-protected queues that introduce lock contention and non-deterministic latency.

**Layer 4 -- Processing and logic**

- Composable pipeline stages that each accept a reading and either transform it, filter it, or trigger an action.
- Includes smoothing filters, calibration (gain/offset from configuration), threshold detection, multi-source fusion, and a rule engine driven by external configuration.
- State machines model ECU (Electronic Control Unit) behaviour using sum types and pattern matching.
- Diagnostic trouble codes are stored, cleared, and queried through a dedicated manager.
- Downstream commands (from web interface or rule engine) are routed through the appropriate transport to the correct actuator.

**Layer 5 -- Telemetry sinks**

- Pluggable output destinations behind a common sink interface: console, file (JSON lines), and MQTT broker.
- An async exporter batches readings and pushes to all active sinks on a background thread.
- Sink failures are isolated. One failing sink does not block the others.

**Layer 6 -- System service**

- The daemon runs as a managed service with automatic restart on failure and structured logging.
- A watchdog timer ensures the process is restarted if it stops responding.

## Data Flow

Communication is bidirectional:

**Upstream (sensor data):**
Arduino reads its connected sensors and sends SENSOR_DATA frames to the RPi. RPi reads its own GPIO sensors directly. All readings enter the processing pipeline through the ring buffer.

**Downstream (commands):**
A web interface, MQTT command, or rule engine decision triggers a command. The RPi sends a GPIO_COMMAND frame to the Arduino, which drives the specified pin. RPi-local actuators are driven directly via GPIO.

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
- Any Arduino digital output pin can be driven remotely via GPIO_COMMAND frames sent from the RPi.

**Communication interfaces**

- USB cable (Type-A to Type-B) connects the RPi to the Arduino for UART communication.
- Two MCP2515 CAN controller modules (one per board) connected via SPI (Serial Peripheral Interface). This enables real CAN bus traffic.
- Both transports can operate simultaneously. Each sensor source is configured at startup to use a specific transport (UART or CAN). The IPort abstraction makes both look identical to all code above the HAL layer.

## Wire Protocol

The wire protocol defines a logical frame structure whose fields are borrowed from the CAN specification. The MCP2515 hardware and SocketCAN kernel driver handle real CAN signalling, arbitration, and hardware CRC. Both UART and CAN transports are used simultaneously; each sensor source chooses which transport carries its messages via the IPort abstraction.

**Logical frame fields (transport-independent):**

- Two-byte message identifier using the standard CAN 11-bit address space (0x0000 through 0x07FF).
- One-byte data length code (0 through 6). The CAN 2.0 data field is 8 bytes; SEQ (1 byte) and CRC (1 byte) also occupy that field, leaving a maximum of 6 payload bytes.
- Payload bytes (0 to 6).
- One-byte sequence counter (0-255, wrapping). Incremented per sender per message ID. Detects replays, reordering, and dropped messages.
- One-byte CRC-8 integrity check using the CCITT polynomial (0x07), with no reflection and no final XOR. Computed over the identifier bytes, the data length code, the sequence counter, and the payload.

**Byte ordering:** Byte order for the message identifier and each payload signal is an application-level convention (CAN itself does not mandate one). Endianness is declared per signal in the signal definition, similar to CAN DBC practice. A signal definition specifies whether its bytes are big-endian or little-endian. This allows mixing byte orders across different sensor types within the same system.

**Software CRC vs hardware CRC:** The software CRC-8 protects the entire path from serialisation to deserialisation regardless of transport. When messages travel over CAN hardware, the MCP2515 adds its own CRC-15, but that only covers the electrical bus segment. The software CRC covers kernel buffers, the IPort read path, and the ring buffer -- none of which are protected by hardware CRC.

**UART serialisation:**

UART is a raw byte stream with no built-in framing. The UART envelope adds start/end markers around the logical frame:

`[STX] [ID_HI] [ID_LO] [DLC] [SEQ] [payload...] [CRC] [ETX]`

- Start marker (STX) and end marker (ETX) values are chosen at implementation time. A complementary bit pattern (e.g. 0xAA / 0x55) aids synchronisation detection.
- Total UART overhead: 2 bytes (start + end markers). The CRC and sequence counter are part of the logical frame, not the UART envelope.
- Maximum UART frame size: 13 bytes (2 markers + 2 ID + 1 DLC + 1 SEQ + 6 payload + 1 CRC).

**CAN serialisation:**

CAN hardware handles bus framing and arbitration at the electrical level. No STX/ETX markers. The CAN ID maps from the logical frame ID. The SEQ, payload, and CRC are packed into the 8-byte CAN data field:

`can_frame.data = [SEQ] [payload...] [CRC]`  (DLC = 1 + payload_len + 1, max 8)

Maximum CAN data field usage: 8 bytes (1 SEQ + 6 payload + 1 CRC).

**What is identical between transports:** The logical content (ID, DLC, sequence counter, payload, CRC) and the integrity validation logic. What differs is the framing envelope (STX/ETX for UART, CAN hardware framing for CAN).


## Transport Selection

Transport is configured per-sensor-source at startup based on available hardware and system configuration. Each sensor source is mapped to a specific IPort instance (UartPort or SocketCanPort) during initialisation.

**Runtime reconfiguration:** Transport assignments can be changed at runtime through the TransportManager. A web interface, CLI, or GUI sends a reconfiguration command that atomically swaps the IPort for a given sensor source. The old port is closed, the new port is opened, and subsequent reads/writes for that source go through the new transport. No restart required. The TransportManager serialises reassignment operations so that in-flight messages complete before the swap takes effect.

When only UART is available (no MCP2515 modules connected), all sensor sources use the UART transport automatically. If a source is configured for CAN and CAN hardware is present, use CAN. If a source is configured for CAN and CAN hardware is absent, warn the user and fall back to UART.

## Arduino Firmware

The Arduino runs a loop firmware that:

1. Reads incoming frames from the RPi via the configured transport.
2. Responds to PING with PONG (connectivity check).
3. Responds to GPIO_COMMAND by setting the specified Arduino pin HIGH/LOW and sending ACK.
4. Actively reads its local sensors and pushes SENSOR_DATA frames upstream to the RPi.

The firmware is compiled and uploaded to the Arduino using a build script integrated into the CMake build as a custom target.

## Core Types

**SensorReading**

A unified data structure that represents one sensor measurement. Every sensor on both boards produces a SensorReading. This is the only type that crosses the boundary from protocol adapters into the processing pipeline.

Fields: timestamp (when the reading was taken), source (which sensor produced it -- a logical identifier, not a pin number), value (the measurement), quality (semantic reliability of the reading).

**SensorSource enum**

A logical identifier naming the kind of sensor that produced a reading. Values like DHT11_TEMPERATURE, JOYSTICK, HALL_EFFECT identify the sensor type, not the physical pin. Pin assignments are a separate concern in the HAL layer. New sensor types are added by extending this enum.

**ReadingQuality enum**

Describes the semantic reliability of a reading:
- GOOD: fresh, valid reading from a working sensor.
- STALE: sensor has not reported recently; using last known value.
- ESTIMATED: value was interpolated or inferred because the sensor was temporarily unavailable.
- BAD: value is outside physically plausible range or sensor is known to be malfunctioning.

Without ReadingQuality, the processing pipeline and telemetry sinks would treat every reading as equally trustworthy. A 30-second-old temperature reading would be indistinguishable from a fresh one.

**Result\<T,E\>**

A discriminated union holding either a success value of type T or an error value of type E. Replaces raw bool returns and output pointers with a single object that cannot be used without checking for error first.

**RingBuffer\<T, Capacity\>**

A fixed-capacity lock-free SPSC ring buffer. Capacity is a compile-time power of two. Overflow policy is drop-oldest. Used as the async message bus connecting sensor producers to the processing pipeline consumer. Without it, sensor reads would either block on processing or require mutex-protected queues.

## C++ Guidelines

- RAII for every hardware resource. Constructors acquire, destructors release. No manual cleanup in business logic.
- Move-only semantics on all HAL wrappers. Copy construction and copy assignment are deleted.
- No exceptions in HAL or protocol code. The Arduino toolchain disables C++ exception unwinding, and exceptions in destructors cause undefined behaviour. Functions that can fail return Result<T,E> or bool.
- No heap allocation in the HAL layer. Heap allocation is non-deterministic in timing and fragments memory on constrained hardware. HAL objects use stack or static storage only.
- No `using namespace` in header files. It pollutes the include chain.
- All transport code programs against an abstract port interface, never a concrete type.
- Lock-free SPSC ring buffer for the message bus hot path. No mutexes on data flow.
- Worker threads use cooperative cancellation for shutdown. No forced thread termination.
- All reads from hardware or network must have a timeout. No unbounded blocking.
- Scoped enumerations for type safety. No implicit integer conversions for message types or pin modes.
- C++23 on the RPi and host builds. C++11 on the Arduino, with C-style prefixed naming and no namespaces.

The error-handling strategy is explicit return values everywhere below the application layer. Functions that can fail return either a boolean or a Result type that discriminates between success and failure. Callers must check the result before using the value. No error is silently ignored.

The threading model is one producer thread per data source, one consumer thread for the processing pipeline. Threads are launched at startup and joined at shutdown. Cooperative cancellation tokens propagate stop requests. No shared mutable state between threads except the lock-free ring buffer.

## Implementation Phases

**Phase 1 -- Core types**

- Define the unified sensor reading struct with timestamp, source identifier, value, and quality fields.
- Implement Result<T,E> to replace raw bool returns in new code.
- Implement a fixed-capacity lock-free SPSC ring buffer with compile-time sizing.
- Set up GTest via CMake FetchContent so tests download and build automatically.
- Verification: all core types compile on both target toolchains (Linux GCC 13 and ARM GCC). Ring buffer passes a concurrent stress test with one producer and one consumer thread and no data loss.

**Phase 2 -- Hardware abstraction**

- Wrap GPIO access in an RAII class that acquires the memory mapping on construction and releases it on destruction.
- Wrap UART access in a move-only port class behind the abstract port interface. All reads take a timeout.
- Define the abstract port interface with open, close, read, write, and status query methods.
- Verification: GPIO tests pass using the mock backend on all platforms. Serial port opens, sends, receives, and closes without resource leaks on real hardware.

**Phase 3 -- Wire protocol**

- Implement CRC-8/CCITT computation at the logical frame layer (transport-blind).
- Implement one-byte sequence counter per sender per message ID.
- Implement UART frame serialisation and deserialisation with start/end markers wrapping the logical frame (ID + DLC + SEQ + payload + CRC).
- Implement CAN frame packing: map logical frame ID to can_id, pack SEQ + payload + CRC into can_frame.data. No STX/ETX markers (CAN hardware handles bus framing).
- Both UART and CAN paths must compute and validate CRC and sequence counter identically.
- Provide both a C++23 RPi-side codec and a C++11 Arduino-side codec, both producing identical logical content for the same input.
- Verification: serialisation roundtrips for every defined message identifier on both UART and CAN paths. Malformed frames (bad CRC, truncated, oversized, replayed sequence) are rejected. Both codecs produce identical logical frame bytes for the same input.

**Phase 4 -- Sensor integration**

- Implement protocol adapters that read from each transport and emit unified readings into the message bus.
- Implement direct GPIO sensor drivers on the RPi.
- Implement Arduino-side active sensor reads that push SENSOR_DATA frames upstream.
- Implement downstream command path: RPi sends GPIO_COMMAND frames to Arduino actuators.
- Implement per-source transport configuration: each sensor source is mapped to a specific IPort (UART or CAN) at startup.
- Verification: each adapter produces valid readings from real hardware. Adapters for absent hardware skip cleanly without affecting others. Downstream commands reach Arduino actuators and produce ACK responses.

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
- Implement web interface command ingestion: commands received from the web UI or MQTT are routed downstream to the appropriate actuator via the configured transport.
- Verification: readings reach the broker and can be queried from a dashboard. Commands from the web interface reach actuators. Sink failures do not block the pipeline.

**Phase 8 -- System integration**

- Service management with automatic restart and structured logging.
- End-to-end test: sensors to dashboard with all layers running, bidirectional.
- Verification: the system starts, runs, and shuts down cleanly under normal and abnormal conditions. Commands from the web interface reach actuators within acceptable latency.

## Testing

**Unit tests (no hardware required)**

- Run on every platform (WSL2, ARM) as part of the normal build.
- Cover core types (sensor reading construction, result type semantics, ring buffer push/pop/overflow/concurrency).
- Cover protocol codec (UART serialisation roundtrip, CRC validation, malformed frame rejection).
- Cover GPIO using the mock backend (init/cleanup, pin mode, read/write, multiple pins).
- Use GTest downloaded via CMake FetchContent. Each module gets its own test file and CMake target.

**Hardware integration tests (real hardware required)**

- Live in a dedicated test file under the hardware test namespace.
- Each test is a standalone function that exercises one hardware path (GPIO toggle and readback, serial PING/PONG roundtrip, CAN frame send/receive, downstream GPIO_COMMAND/ACK roundtrip).
- The main entry point calls each test via a single commented-out line so individual tests can be enabled or disabled without modifying the test file.
- These tests are not run in CI. They run manually on the Raspberry Pi with the Arduino connected.
