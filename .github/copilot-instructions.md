# Copilot Instructions

## Project Overview
EdgeNode is a C++23 embedded edge gateway on Raspberry Pi that reads sensors, processes
data, and publishes telemetry. Arduino acts as a sensor node (C++11).
Communication is bidirectional: sensor data flows upstream (Arduino/GPIO to RPi pipeline),
commands flow downstream (web interface/rule engine through RPi to Arduino actuators).
Full architecture is in `docs/edge_node.md`.

## Targets
- RPi: C++23 (g++)
- Arduino Mega 2560: C++11 (avr-gcc) supports namespaces and scoped enums.
- Mock GPIO: auto-enabled on non-ARM platforms via `EDGENODE_MOCK_GPIO`.

## Phases
- Phase 1 -- Core Types (SensorReading, Result<T,E>, ring_buffer, GTest).
- Phase 2 -- HAL (GpioPin RAII, UartPort RAII, IPort interface)
- Phase 3 -- Protocol (CRC8, 2-byte CAN IDs, sequence counter, ETX framing, transport-blind integrity)
- Phase 4 -- Sensor Integration
- Phase 5 -- State Machine
- Phase 6 -- Storage / Persistence
- Phase 7 -- Connectivity / Transport
- Phase 8 -- System Integration & Testing

## Naming Conventions
- Classes/structs: PascalCase (`SerialPort`, `WireMessage`).
- Functions/variables: snake_case (`compute_checksum`, `start_byte`).
- Namespaces: lowercase, ownership-based (`protocol`, `coretypes`, `rpi::gpio`, `arduino::protocol`).
- Scoped enum values (`enum class`): UPPER_CASE (`PING`, `SENSOR_DATA`). Used on both RPi
 and Arduino.
- Constants and functions in Arduino protocol code: snake_case inside `namespace arduino::protocol`
  (`arduino::protocol::max_payload`, `arduino::protocol::compute_crc`).
- Unscoped enum values: UPPER_CASE (`MSG_PING`).
- Constants: snake_case (`start_byte`). 
- No column-aligned assignments. Single spacing only.
- No trailing underscore or other special annotations on member variables. Private
  members use the same snake_case as any other variable. If a member name would collide
  with a public method name, shorten or abbreviate the member (e.g. `val` for a `value()`
  accessor, `err` for an `error()` accessor).
- Filenames: snake_case.

## Code Style
- Doxygen (`/// @brief`) for function/class declarations in `.h` files only.
- Normal line comments (`// Comment`) in `.cpp` and `.ino` files explaining intent, not restating code.
- No unicode characters in source (no arrows, em dashes, etc.).

## Namespaces
Ownership-based hierarchy, applied across all layers and files. Shared code carries a
descriptive neutral name; board-specific code lives under `rpi::` or `arduino::`.

- `protocol` - shared, transport-blind wire protocol (`WireMessage`, CRC, framing).
  Compiled into both the RPi and Arduino targets.
- `coretypes` - shared generic containers and data types (`DataPacket`, `Result`,
  `RingBuffer`, `SensorSource`, `ReadingQuality`). Reusable, not board-specific.
- `rpi::protocol` - RPi-side codec wrapping the shared `protocol` frame.
- `rpi::gpio` - RPi GPIO abstraction.
- `rpi::serial` - RPi serial port wrapper.
- `arduino::protocol` - Arduino-side, mirrors `rpi::protocol` on the RPi.
- `arduino::gpio` - Arduino GPIO abstraction.
- `tests` - unit tests (no hardware).
- `hw_test` - hardware integration tests only. 

Note: `DataPacket` is the only type that crosses the protocol
adapter boundary into the pipeline. It does not carry the wire bytes: a `rpi::protocol` /
`arduino::protocol` codec decodes a `protocol::WireMessage`, extracts the payload, and
produces a `DataPacket` (and does the reverse for downstream commands). See
`docs/edge_node.md` (Core Types) for the field-by-field mapping.

## Code Patterns
- **RAII everywhere:** Every hardware resource (fd, mmap, SPI) acquires in constructor,
  releases in destructor. No manual cleanup calls in business logic.
- **No exceptions in HAL or protocol code.** The Arduino toolchain disables C++ exception
  unwinding entirely, and exceptions in destructors cause undefined behavior during cleanup.
  Use `Result<T,E>` or bool return values instead.
- **No heap allocation in HAL layer.** Heap allocation is non-deterministic in timing and
  can fragment memory on constrained hardware. HAL objects (GpioPin, UartPort) must use
  stack or static storage only. "Static storage" = objects with static/global lifetime
  (file-scope or class `static` instances) allocated once by the linker -  its lifetime is 
  fixed and known at compile time.
- **IPort interface:** Abstract base for UartPort, CanPort. All transport code
  programs against IPort, never a concrete type.
- **Ring buffer:** Lock-free SPSC for the message bus hot path. No mutexes on data flow.
- **Move semantics:** HAL wrappers are move-only (deleted copy ctor/assignment). This is
  ownership transfer of the raw resource itself (fd/handle), not smart pointers; no heap or
  `shared_ptr` is used in the HAL layer on either board.

## Testing
- Framework: GTest, downloaded automatically at build time via CMake FetchContent.
- Mock GPIO is always enabled in test builds (`EDGENODE_MOCK_GPIO=1`), so unit tests run
  on any host with no hardware. Hardware integration tests (`hw_test`) are the
  exception: they build with `EDGENODE_MOCK_GPIO=0` and run manually on the real RPi/Arduino.
- Every new module needs: a test file in `test/`, a target in `test/CMakeLists.txt`,
  and an `add_test()` registration.
- Test file naming: `test_<module>.cpp`. Test functions: `TEST(<Suite>, <Case>)`.
- Protocol tests must run on all platforms without hardware.
- GPIO unit tests must use the mock backend (`EDGENODE_MOCK_GPIO=1`) and stay under `tests`.
  Real-hardware GPIO checks belong to `hw_test`, built with the mock disabled and run 
  manually on the RPi and Arduino.
    
## Never Do
- Never assume how something is implemented. Read the relevant source file first,
  then act on what the code actually says. If the file does not exist yet, say so
  and ask before proceeding.
- No heap allocation in HAL layer. Stack or static storage only.
- No exceptions in HAL or protocol code. Fallible functions return `Result<T,E>` (success
  value or error enum) or a plain bool; callers must check before using the value.
- No `using namespace` in header files -- even with a correct hierarchy where names could
  not otherwise collide, a `using` in a header pulls those names into every translation unit
  that includes it, defeating the namespace and risking overload/lookup ambiguities. Inside a
  `.cpp` it is fine.
- No raw `new`/`delete`. Use RAII or smart pointers.
- No blocking I/O without a timeout in any read path.
- No platform ifdefs in RPi source except `EDGENODE_MOCK_GPIO`. RPi files use POSIX APIs
  only; cross-platform host builds are handled exclusively via the mock GPIO shim, which keeps
  RPi code free of `#ifdef` branching that is hard to test. Arduino firmware is a separate
  target with its own toolchain (avr-gcc) and is not subject to this POSIX rule.
- No manual fd cleanup - destructors handle it.
- No column-aligned assignments or extra spacing.
- Do not implement features beyond the current phase unless explicitly instructed.

## Skills
Reusable prompt fragments. Invoke by name (e.g., "Apply the RAII Wrapper skill").

### Skill: RAII Wrapper
Create a move-only wrapper class for a system resource:
1. Constructor acquires the resource. On the RPi this is a POSIX fd/handle (open/mmap/ioctl);
   on the Arduino it is the board resource (e.g. a configured pin or `HardwareSerial`). Store
   the handle as a private member.
2. Destructor releases it (close/munmap on RPi, or reset the pin/stream on Arduino). Check the
   handle is valid before releasing.
3. Delete the copy constructor and copy assignment (`= delete`), so the resource cannot be
   duplicated and double-released.
4. Implement move ctor and move assignment (transfer ownership, invalidate source).
5. Provide `is_valid()` or `is_open()` query method.
6. No exceptions - a factory/`open()` returns `Result<T,E>` or bool so the caller checks for
   success before use.

### Skill: New Test Module
Add a test for a new module:
1. Create `test/test_<module>.cpp` with GTest `TEST()` macros.
2. Add target in `test/CMakeLists.txt`: `add_executable`, `target_link_libraries(GTest::gtest_main)`, `target_include_directories`, `add_test`.
3. For unit tests that touch HAL code, add `target_compile_definitions(... PRIVATE EDGENODE_MOCK_GPIO=1)`
   so they run on any host. Hardware integration tests are the opposite: they build with
   `EDGENODE_MOCK_GPIO=0` and run manually on the real board, never in the automated suite.

### Skill: IPort Implementation
Implement a new transport behind the IPort interface:
1. Inherit from `IPort`, the abstract transport interface (pure virtual `open`, `close`,
   `read`, `write`, `is_open`) that all transports (UartPort, CanPort) implement so code
   above the HAL never depends on a concrete transport.
2. Constructor takes config: device path plus the link speed (UART baud rate, CAN bitrate).
   This is configurable rather than hardcoded because the same wrapper serves different
   devices/wiring, and the speed must match the peer at the other end of the link.
3. Follow the RAII Wrapper skill for resource management.
4. All reads must have a timeout parameter.

## Interaction Preferences
- Before implementing or modifying anything, read the relevant existing files.
  State what you found, then propose what you will change.
- Concise, direct answers. No repeated analysis. No unsolicited refactors outside the current phase. Pragmatic approach is imperative. Clear practical explanations without unnecessary verbosity or fancy language. Avoid over-engineering. Focus on the current phase and its requirements.