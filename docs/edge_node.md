# EdgeNode

EdgeNode is an embedded edge gateway running C++23 on a Raspberry Pi. It reads sensors, receives structured frames from an Arduino over UART (Universal Asynchronous Receiver-Transmitter) or CAN (Controller Area Network), processes the data locally, and publishes telemetry. The Arduino runs C++11 as a sensor node.

## Architecture

The system is six layers deep. Each layer owns one concern and enforces one boundary. Data flows in both directions. Sensor data flows upstream from hardware to telemetry. Commands flow downstream from the web interface through the RPi to actuators on both boards.

**Layer 1 -- Hardware abstraction (HAL)**

- Wraps every hardware resource (GPIO pin, serial port, SPI bus, CAN socket) in an RAII (Resource Acquisition Is Initialization) object that acquires on construction and releases on destruction.
- No file descriptor, memory mapping, or device handle may exist outside a wrapper.
- No heap allocation. All HAL objects use stack or static storage. Allocating memory at runtime on a small board (Arduino) is slow and, over time, fragments the limited RAM until an allocation fails. Fixed stack or static storage keeps memory use predictable.
- All transport wrappers implement a common port interface so higher layers never depend on a concrete transport type.
- When the build system detects a non-ARM host (WSL2 or a Linux workstation), all hardware calls route to mock implementations that log to stdout. The binary must not crash on platforms without GPIO.

**Layer 2 -- Protocol adapters**

- Each adapter reads from one transport (CAN bus, UART stream, or direct GPIO sensor) and emits a unified reading type into the message bus.
- The reading type that crosses this boundary is `DataPacket` (see Core Types), not the wire frame. The wire frame (`WireMessage` in the shared `protocol::` namespace, wrapped per side as `rpi::protocol` on the RPi and `arduino::protocol` on the Arduino) stays in the protocol layer. An adapter decodes a frame and turns it into a `DataPacket` before it reaches the message bus. Raw frames, register values, and device-specific encodings stay below.
- Namespaces follow ownership. Transport-blind wire-protocol code shared by both boards lives in `protocol::`. Generic reusable containers (`Result`, `RingBuffer`, `DataPacket`) live in `coretypes::`. RPi-only code lives under `rpi::` (`rpi::gpio`, `rpi::serial`, `rpi::protocol`). Arduino-only code lives under `arduino::` (`arduino::protocol`, `arduino::gpio`).

- An adapter that fails to initialise (device absent, permission denied) logs a warning and is skipped. The system runs with whatever hardware is available.
- Adapters handle both directions: upstream (sensor data from Arduino to RPi) and downstream (commands from RPi to Arduino actuators).
- The UART adapter and CAN adapter share the same logical frame format. Only the physical transport and serialisation envelope differ.

**Layer 3 -- Async message bus**

- A lock-free single-producer single-consumer (SPSC) ring buffer connects each data source to the processing pipeline.
- No locks on the data-flow hot path. A lock would force a sensor thread to wait whenever the pipeline thread holds it, adding unpredictable delay. The ring buffer lets one thread write and one thread read at the same time without waiting. Threads only coordinate (start and join) at startup and shutdown.
- Each worker thread checks a shared stop flag and exits its loop on its own when shutdown is requested. The stop flag is a single `std::atomic<bool>` (not a smart pointer) shared by reference with every worker; shutdown sets it once, and each thread observes it on its next loop iteration and returns. Threads are never killed mid-operation, so they always finish cleanly.
- Overflow policy is drop-oldest: when the buffer is full, the oldest unread entry is overwritten so the consumer always sees the freshest data. This is a deliberate capacity policy, not a memory-safety "buffer overflow"; indices are bounds-checked and no memory is corrupted.
- A per-reading mutex would also be correct, but it forces the writer and reader to serialise on every hand-off: whenever one holds the lock the other blocks, so latency depends on scheduling and becomes unpredictable. With exactly one writer and one reader, the ring buffer needs no lock at all, so neither thread blocks the other. Mutexes are therefore reserved for the low-rate Layer 5 sink stage (see Layer 5), never this hot path.
- Without the ring buffer, every sensor read would either wait for the pipeline to finish or use a locked queue. Locked queues make threads wait on each other, so timing becomes unpredictable. The ring buffer avoids both.

**Layer 4 -- Processing and logic**

- The pipeline is a chain of small, independent steps. Each step takes a reading and either changes it, drops it, or triggers an action, then passes it on. Steps can be added, removed, or reordered without touching the others.
- Built-in steps include: smoothing (averaging out noisy readings), calibration (applying a gain and offset from configuration to turn raw values into real units), threshold detection (flagging values above or below a limit), sensor fusion (combining several sensors into one value), and a rule engine that runs if/then rules loaded from a configuration file.
- Each ECU (Electronic Control Unit) is modelled as a state machine. Concretely, a `std::variant` holds exactly one state object at a time (e.g. `Idle`, `Running`, `Fault`), and an event handler uses `std::visit` to run the logic for the current state and return the next state. Illegal transitions are impossible because a state that does not handle an event simply returns itself. Fixed set of states with defined transitions: this means the state list and the allowed event-to-state moves are known at compile time, not built dynamically.
- Diagnostic trouble codes (DTCs) are latched system faults, separate from per-call error handling. `Result<T,E>` reports whether a single operation succeeded; a DTC records that a system-level fault condition became active and stays active until explicitly cleared (like a car's check-engine light). A single `FaultRegistry` component stores them. Each entry is `{ code (enum), first_seen_ms, last_seen_ms, active (bool) }`, held in a fixed-size array indexed by code. Its API is `set(code)` (raise or refresh), `clear(code)`, `is_active(code)`, and `active_codes()` (snapshot for telemetry). Example: three consecutive BAD readings from the light sensor raise `DTC::LIGHT_SENSOR_FAULT`; it is exported in telemetry and cleared once good readings resume.
- Downstream commands (from the web interface or rule engine) are sent to the board that owns the target actuator, over whichever transport (UART or CAN) that board uses.

**Layer 5 -- Output destinations (telemetry sinks)**

- Pluggable output destinations behind a common `ISink` interface: console, file (JSON lines), and MQTT broker. The pipeline pushes each processed `DataPacket` to every enabled sink through this one interface; adding a new consumer (e.g. a web dashboard feed) means implementing `ISink`, not changing the pipeline.
- A background thread collects readings into small groups (batches) and sends each group to every enabled sink. Grouping reduces per-send overhead, and running on its own thread means a slow sink never blocks the pipeline. This Layer 5 thread is where a mutex plus condition-variable queue is used (low rate, multiple consumers), in contrast to the Layer 3 hot path which uses the lock-free ring buffer. Rule of thumb: ring buffer on the high-rate single-writer/single-reader hot path, mutex only here on the low-rate fan-out path.
- Sink failures are isolated: one failing sink does not block the others. A typical failure is the MQTT broker being unreachable (network down) or the log disk being full; that sink reports the error and is retried or skipped while the console and file sinks keep working. Sink health is about the output transport, not sensor data quality; degraded sensor data is signalled separately via `ReadingQuality` and DTCs.

**Layer 6 -- Background service**

- The program runs in the background as a service that the operating system starts on boot and restarts automatically if it crashes. Logs are written as consistent, machine-readable records (each with a timestamp, level, and message) rather than free-form text. 
- A watchdog timer ensures the process is restarted if it stops responding.

## Data Flow

Communication is bidirectional:

**Upstream (sensor data):**
Arduino reads its connected sensors and sends SENSOR_DATA frames to the RPi. RPi reads its own GPIO sensors directly. All readings enter the processing pipeline through the ring buffer. The ring buffer lives at Layer 3 (message bus) and is the only shared state on the hot path; mutexes appear only at Layer 5 (sink batching). See the C++ Guidelines threading model for how the two fit together.

**Downstream (commands):**
A web interface, MQTT command, or rule engine decision triggers a command. The RPi sends a GPIO_COMMAND frame to the Arduino, which drives the specified pin. RPi-local actuators are driven directly via GPIO.

In short: the Arduino sends its sensor data to the RPi over UART or CAN. The RPi reads its own GPIO sensors directly. The RPi then merges everything and publishes it to the web interface and the MQTT broker.

## Hardware

**Sensors (inputs)**

- DHT11 temperature and humidity sensor. Produces ambient environment readings.
- Sound/vibration sensor. Produces knock-event readings.
- Photoresistor. Produces light-level readings used for tamper detection.
- Joystick on an analog input. Produces a throttle percentage signal.
- Hall-effect sensor on a digital input. Produces wheel speed / RPM readings.
- Button/touch sensor on a digital input. Produces discrete event signals (door, ignition).

**Actuators (outputs)**

- Relay module. Switched by the rule engine in response to threshold breaches.
- Buzzer. Driven as an alarm output.
- RGB LED on three pins. Displays service state.
- Any digital output pin can be driven remotely via GPIO_COMMAND frames sent from the RPi.

**Communication interfaces**

- USB cable (Type-A to Type-B) connects the RPi to the Arduino for UART communication.
- Two MCP2515 CAN controller modules (one per board) connected via SPI (Serial Peripheral Interface). This enables real CAN bus traffic.
- Both transports can operate simultaneously. Each sensor source is configured at startup to use a specific transport (UART or CAN). The common port interface makes both look identical to all code above the HAL layer.

## Wire Protocol

The wire protocol defines a logical frame structure whose fields are borrowed from the CAN specification. The MCP2515 hardware and SocketCAN kernel driver handle real CAN signalling, arbitration, and hardware CRC. Both UART and CAN transports are used simultaneously; each sensor source chooses which transport carries its messages. All transports share one common port interface (the abstract transport type described in the HAL layer), so code above the HAL layer does not depend on which transport is used.

**Logical frame fields (transport-independent):**

- Two-byte message identifier using the standard CAN 11-bit address space (0x0000 through 0x07FF). 
- One-byte data length code (0 through 6). The CAN 2.0 data field is 8 bytes; SEQ (1 byte) and CRC (1 byte) also occupy that field, leaving a maximum of 6 payload bytes.
- Payload bytes (0 to 6).
- One-byte sequence counter (0-255, wrapping). Incremented per sender per message ID. Detects replays, reordering, and dropped messages.
- One-byte CRC-8 integrity check using the CCITT polynomial (0x07). Computed over the identifier bytes, the data length code, the sequence counter, and the payload.

**Byte ordering:** CAN does not define a byte order, so it is fixed here by convention. Each signal in a payload states, as part of its definition, whether its bytes are big-endian or little-endian, so different sensor types can use different byte orders within the same system. Code that reads a signal follows that declaration; it does not guess or auto-convert.

**Software CRC vs hardware CRC:** The software CRC-8 protects the whole path from where a frame is built to where it is checked, on any transport. When messages travel over CAN hardware, the MCP2515 adds its own CRC as well, but the software CRC is used regardless of the transport so the entire path stays covered, including the kernel buffers, the port read path, and the ring buffer.

**UART serialisation:**

UART is a raw byte stream with no built-in framing. The UART envelope adds start/end markers around the logical frame:

`[STX] [ID_HI] [ID_LO] [DLC] [SEQ] [payload...] [CRC] [ETX]`

- Start marker (STX) and end marker (ETX) values are chosen at implementation time. A complementary bit pattern (e.g. 0xAA / 0x55) aids synchronisation detection.
- Total UART overhead: 2 bytes (start + end markers). The CRC and sequence counter are part of the logical frame, not the UART envelope.
- Maximum UART frame size: 13 bytes (2 markers + 2 ID + 1 DLC + 1 SEQ + 6 payload + 1 CRC). The message identifier is defined as a two-byte field (using the 11-bit CAN address space, 0x0000–0x07FF): ID_HI and ID_LO.

**CAN serialisation:**

CAN hardware handles bus framing and arbitration at the electrical level. No STX/ETX markers. The logical 11-bit frame identifier is used directly as the CAN arbitration ID (`can_id`); there is no translation table, the same 0x000-0x7FF value simply becomes the CAN ID. The SEQ, payload, and CRC are packed into the 8-byte CAN data field:

`can_frame.data = [SEQ] [payload...] [CRC]`  (DLC = 1 + payload_len + 1, max 8)

Maximum CAN data field usage: 8 bytes (1 SEQ + 6 payload + 1 CRC).

**What is identical between transports (UART and CAN):** The logical content (ID, DLC, sequence counter, payload, CRC) and the integrity validation logic. What differs is the framing envelope: for UART we add STX/ETX bytes in software; for CAN the MCP2515 controller and SocketCAN driver supply the frame delimiting, arbitration, and bit-level framing in hardware, so no software markers are added.


## Transport Selection

Transport is configured per sensor source at startup based on the available hardware and system configuration. Each sensor source is bound to one port instance (a UART port or a CAN port) during initialisation.

**Runtime reconfiguration:** Transport assignments can be changed while the program runs. A web interface, CLI, or GUI sends a command that swaps the port for a given sensor source: the old port is closed, the new port is opened, and later reads and writes for that source go through the new transport. No restart is required. Reassignments are handled one at a time, so any in-flight message finishes before the swap takes effect.

When only UART is available (no MCP2515 modules connected), all sensor sources use the UART transport automatically. If a source is configured for CAN and CAN hardware is present, use CAN. If a source is configured for CAN and CAN hardware is absent, warn the user and fall back to UART.

## Arduino Firmware

The Arduino runs a loop firmware that:

1. Reads incoming frames from the RPi via the configured transport.
2. Responds to PING with PONG (connectivity check).
3. Responds to GPIO_COMMAND by driving the specified Arduino pin and sending ACK. The current command payload is `{ pin, mode, value }`. Today `mode` is DIGITAL and `value` is LOW/HIGH, which covers relay, buzzer, and LED on/off. The `mode` field is the extension point: adding `mode = PWM` later lets the same frame carry a 0-255 duty cycle (LED dimming, analog output) with no change to the frame layout. Behaviour is a firmware-side switch on `mode`, not a protocol change.
4. Actively reads its local sensors and pushes SENSOR_DATA frames upstream to the RPi.

The firmware is compiled and uploaded to the Arduino by a CMake custom target that invokes `arduino-cli`. The target runs `arduino-cli compile` for the Mega 2560 (FQBN `arduino:avr:mega`) against the firmware sketch, then `arduino-cli upload` to the configured serial port. It is an opt-in target (not part of the default build), so host and RPi builds never require the Arduino toolchain.

## Core Types

**DataPacket** 

A unified data structure representing one unit of measurement or command that flows through the pipeline. It is separate from the wire frame (`WireMessage`), which only carries raw protocol bytes. A protocol adapter decodes an incoming frame and produces a `DataPacket`; from there the rest of the system works only with `DataPacket` and never sees transport or protocol details.

The same type is used in both directions. Upstream it carries a sensor measurement (source = sensor, value = reading). Downstream it carries a command (source = target actuator, value = requested level e.g. low/high). The protocol layer packs and unpacks between `DataPacket` and `WireMessage`, and the `WireMessage` is identical regardless of transport (UART or CAN).

How a measurement maps to the wire: the adapter reads the frame payload bytes (per the byte-order declaration for that signal), converts them to the `float value`, sets `source` from the frame ID, stamps `timestamp_ms`, and sets `quality`. Downstream the reverse happens: `value` is encoded into the payload of a GPIO_COMMAND `WireMessage`.

Fields: timestamp (when the reading was taken or the command issued), source (logical sensor or actuator identifier, not a pin number), value (the measurement or command level), quality (semantic reliability; GOOD for freshly issued commands).

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

A type holding either a success value of type T or an error value of type E. Compared with `std::optional<T>`, it also carries why something failed, not just that it failed. Compared with a bare `std::variant<T,E>`, it exposes clear `is_ok()` / `value()` / `error()` access and forces the caller to check before reading the value. Templates are used here (and for the ring buffer) because these are generic containers reused with many types. In practice `Result<T,E>` is instantiated with whatever a call returns plus an error enum, e.g. `Result<SerialPort, IoError>` from a port open, `Result<WireMessage, DecodeError>` from a frame decode, or `Result<size_t, IoError>` from a read.

**RingBuffer\<T, Capacity\>**

With a single writer and a single reader, the ring buffer needs no lock at all. See Layer 3 and the C++ Guidelines threading model: this lock-free buffer is used only on the single-writer/single-reader hot path; the low-rate multi-consumer sink stage (Layer 5) is the only place a mutex is used.

## C++ Guidelines

- RAII for every hardware resource. Constructors acquire, destructors release. No manual cleanup in business logic.
- Move-only semantics on all HAL wrappers. Copy construction and copy assignment are deleted.
- No exceptions in HAL or protocol code. The Arduino toolchain disables C++ exception unwinding, and exceptions in destructors cause undefined behaviour. Functions that can fail return `Result<T,E>` or bool (see the error-handling strategy below).
- No heap allocation in the HAL layer. Runtime allocation on small hardware is slow and can fragment the limited RAM over time until an allocation fails. HAL objects use stack or static storage only. "Static storage" means objects with static/global lifetime (a file-scope or class `static` instance) allocated once by the linker, not on the heap; together with stack locals this keeps every allocation's lifetime fixed and known at compile time.
- No `using namespace` in header files. Even with a correct hierarchy, `using` in a header pulls names into every translation unit that includes it, which defeats the point of the namespace and can still cause surprising overload and lookup ambiguities; inside a `.cpp` it is fine. Both boards use real namespaces (the Mega 2560 toolchain supports them).

- All transport code programs against an abstract port interface, never a concrete type.
- A single-writer single-reader ring buffer carries data on the message bus hot path, so no locks are needed there. This avoids threads waiting on each other and keeps latency predictable (see the threading model below).
- Worker threads shut down by checking a shared `std::atomic<bool>` stop flag and returning on their own; they are never force-killed. Threading maps one-to-one to data sources: each source (a UART adapter, a CAN adapter, a GPIO sensor) runs on its own producer thread and writes `DataPacket`s into its own SPSC ring buffer, and the processing pipeline runs on one consumer thread that drains those buffers. Because each buffer has exactly one writer and one reader, no mutex is needed between producer and consumer; the ring buffer is the entire synchronisation mechanism on the hot path.
- Every read from hardware or the network must have a timeout, so a call can never wait forever if data stops arriving.
- Use scoped enumerations (`enum class`) for message types and pin modes. Their values do not implicitly convert to integers, so you cannot accidentally pass a raw number, or the wrong enum, where a message type or pin mode is expected.
- C++23 on the RPi and host builds. C++11 on the Arduino (Mega 2560).

The error-handling strategy is explicit return values everywhere below the application layer. The application layer is the top layer (the background service and the web/rule logic); everything below it (HAL, protocol, message bus, processing) returns a boolean or a `Result` type that distinguishes success from failure instead of throwing. Callers must check the result before using the value. No error is silently ignored.

The threading model is simple: one thread per data source produces readings, and one thread consumes them for the processing pipeline. All threads start at startup and are joined at shutdown. To stop, each thread checks a shared stop flag and exits on its own. The only data shared between threads on the hot path is the ring buffer, which is safe for one writer and one reader without locks. The stop flag is a single process-wide `std::atomic<bool>` shared by reference with every worker (one flag, not one per thread): setting it once asks all threads to finish their current iteration and return -- a cooperative shutdown, not a per-thread kill switch. 
To summarise the three distinct synchronisation primitives, which do not overlap: the ring buffer is the lock-free hot-path data hand-off, the atomic stop flag is the shutdown signal, and a mutex appears only at the Layer 5 sink batch stage.


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
- Implement per-source transport configuration: each sensor source is bound to a specific port (UART or CAN) at startup.
- Verification: each adapter produces valid readings from real hardware. Adapters for absent hardware skip cleanly without affecting others. Downstream commands reach Arduino actuators and produce ACK responses.

**Phase 5 -- State machines**

- Implement ECU state machines using a variant type and explicit per-state matching.
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

- Run on every platform (Linux/WSL2, ARM) as part of the normal build.
- Cover core types (sensor reading construction, result type semantics, ring buffer push/pop/overflow/concurrency).
- Cover protocol codec (UART serialisation roundtrip, CRC validation, malformed frame rejection).
- Cover GPIO using the mock backend (init/cleanup, pin mode, read/write, multiple pins).
- Use GTest downloaded via CMake FetchContent. Each module gets its own test file and CMake target.

**Hardware integration tests (real hardware required)**

- Live in a dedicated test file under the hardware test namespace.
- Each test is a standalone function that exercises one hardware path (GPIO toggle and readback, serial PING/PONG roundtrip, CAN frame send/receive, downstream GPIO_COMMAND/ACK roundtrip).
- The main entry point calls each test via a single commented-out line so individual tests can be enabled or disabled without modifying the test file.
- These tests are not run in CI. They run manually on the Raspberry Pi with the Arduino connected.
