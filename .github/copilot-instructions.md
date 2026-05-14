# Copilot Instructions

## Project Overview
EdgeNode is a C++23 embedded edge gateway on Raspberry Pi that reads sensors, processes
data, and publishes telemetry. Arduino acts as a sensor node (C++11).
Communication is bidirectional: sensor data flows upstream (Arduino/GPIO to RPi pipeline),
commands flow downstream (web interface/rule engine through RPi to Arduino actuators).
Full architecture is in `docs/edge_node.md`.

## Targets
- RPi / WSL2 / host: C++23
- Arduino: C++11
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
- Namespaces: lowercase (`epcore`, `edgenode::gpio`).
- Scoped enum values (`enum class`): UPPER_CASE (`PING`, `SENSOR_DATA`).
- Unscoped enum values: UPPER_CASE (`MSG_PING`, `EP_PING`).
- Constants: snake_case for namespace-scoped (`start_byte`), UPPER_CASE for Arduino/file-scoped (`EP_START_BYTE`).
- No column-aligned assignments. Single spacing only.
- No trailing underscore or other special annotations on member variables. Private
  members use the same snake_case as any other variable. If a member name would collide
  with a public method name, shorten or abbreviate the member (e.g. `val` for a `value()`
  accessor, `err` for an `error()` accessor).
- Filenames: snake_case.

## Code Style
- Doxygen (`/// @brief`) for function/class declarations in `.h` files only.
- Normal line comments (`// Comment`) in `.cpp` and `.ino` files explaining intent, not restating code.
- Use `src/rpi/edge_protocol.cpp` as the template for file headers and comment style.
- No unicode characters in source (no arrows, em dashes, etc.).

## Namespaces
- `epcore` - shared wire protocol, used by both RPi and Arduino host-side code.
- `edgenode::core` - core types (SensorReading, Result, RingBuffer).
- `edgenode::protocol` - RPi protocol wrapper.
- `edgenode::gpio` - RPi GPIO abstraction.
- `edgenode::serial` - RPi serial port wrapper.
- `edgenode::tests` - unit tests.
- `edgenode::hw_test` - hardware integration tests only, lives in `tests/test_hw.cpp`.
- Arduino code: Use simple C-style prefixes instead of namespaces to avoid linker issues

## Code Patterns
- **RAII everywhere:** Every hardware resource (fd, mmap, SPI) acquires in constructor,
  releases in destructor. No manual cleanup calls in business logic.
- **No exceptions in HAL or protocol code.** The Arduino toolchain disables C++ exception
  unwinding entirely, and exceptions in destructors cause undefined behavior during cleanup.
  Use `Result<T,E>` or bool return values instead.
- **No heap allocation in HAL layer.** Heap allocation is non-deterministic in timing and
  can fragment memory on constrained hardware. HAL objects (GpioPin, UartPort) must use
  stack or static storage only.
- **IPort interface:** Abstract base for UartPort, SocketCanPort. All transport code
  programs against IPort, never a concrete type.
- **Ring buffer:** Lock-free SPSC for the message bus hot path. No mutexes on data flow.
- **Move semantics:** HAL wrappers are move-only (deleted copy ctor/assignment).

## Testing
- Framework: GTest, downloaded automatically at build time via CMake FetchContent.
- Mock GPIO is always enabled in test builds (`EDGENODE_MOCK_GPIO=1`).
- Every new module needs: a test file in `test/`, a target in `test/CMakeLists.txt`,
  and an `add_test()` registration.
- Test file naming: `test_<module>.cpp`. Test functions: `TEST(<Suite>, <Case>)`.
- Protocol tests must run on all platforms without hardware.
- GPIO tests must use the mock backend.
- `main.cpp` is a hardware integration test runner, not a daemon. Hardware integration
  tests live in `tests/test_hw.cpp` under namespace `edgenode::hw_test`.

## Never Do
- Never assume how something is implemented. Read the relevant source file first,
  then act on what the code actually says. If the file does not exist yet, say so
  and ask before proceeding.
- No heap allocation in HAL layer. Stack or static storage only.
- No exceptions in HAL or protocol code.
- No `using namespace` in header files -- it pollutes the scope of every file that
  includes that header, causing silent name collisions.
- No raw `new`/`delete`. Use RAII or smart pointers.
- No blocking I/O without a timeout in any read path.
- No platform ifdefs in RPi source except `EDGENODE_MOCK_GPIO`. RPi files use
  POSIX APIs only. Cross-platform builds are handled exclusively via the mock GPIO shim.
- No manual fd cleanup - destructors handle it.
- No column-aligned assignments or extra spacing.
- Do not implement features beyond the current phase unless explicitly instructed.

## Skills
Reusable prompt fragments. Invoke by name (e.g., "Apply the RAII Wrapper skill").

### Skill: RAII Wrapper
Create a move-only wrapper class for a system resource:
1. Constructor acquires (open/mmap/ioctl). Store fd/handle as private member.
2. Destructor releases (close/munmap). Check for valid handle before releasing.
3. Delete copy ctor and copy assignment.
4. Implement move ctor and move assignment (transfer ownership, invalidate source).
5. Provide `is_valid()` or `is_open()` query method.
6. No exceptions - return bool or error code from factory/open methods.

### Skill: New Test Module
Add a test for a new module:
1. Create `test/test_<module>.cpp` with GTest `TEST()` macros.
2. Add target in `test/CMakeLists.txt`: `add_executable`, `target_link_libraries(GTest::gtest_main)`,
   `target_include_directories`, `add_test`.
3. If testing HAL code, add `target_compile_definitions(... PRIVATE EDGENODE_MOCK_GPIO=1)`.

### Skill: IPort Implementation
Implement a new transport behind the IPort interface:
1. Inherit from `IPort` (pure virtual: `open`, `close`, `read`, `write`, `is_open`).
2. Constructor takes config (device path, baud/bitrate).
3. Follow the RAII Wrapper skill for resource management.
4. All reads must have a timeout parameter.

## Interaction Preferences
- Before implementing or modifying anything, read the relevant existing files.
  State what you found, then propose what you will change.
- Concise, direct answers. No repeated analysis. No unsolicited refactors outside the current phase.