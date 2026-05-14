# EdgeNode
Embedded edge gateway that reads sensors, processes data, and publishes telemetry.

## Overview
A Raspberry Pi daemon ingests data from physical sensors and an Arduino, processes it through an edge pipeline, and publishes telemetry to a cloud dashboard. The Arduino reads sensors, packages structured frames, and sends them to the RPi over UART or CAN — both transports operate simultaneously, and each sensor source is individually configured to use one or the other. GPIO sensors on the RPi are read directly. Communication is bidirectional: sensor data flows upstream from hardware to the dashboard, and commands flow downstream from the dashboard or rule engine back to actuators on both boards.

## Architecture

**Layer 1 — Hardware abstraction**
Provides a consistent, transport-agnostic interface to all hardware peripherals so higher layers never depend on specific chips, buses, or pin numbers. Resource ownership and lifetime are managed here via RAII.

**Layer 2 — Protocol adapters**
Convert raw hardware input — UART frames, CAN frames, or direct GPIO readings — into a single unified sensor reading type that the rest of the system uses. Handle both upstream (sensor data in) and downstream (commands out) directions.

**Layer 3 — Async message bus**
A lock-free ring buffer decouples producers and consumers so components run independently without blocking each other on the data path.

**Layer 4 — Processing and logic**
Applies filtering, calibration, threshold detection, and rule-based handling to sensor readings. ECU state machines and a diagnostic trouble code manager live here. The rule engine can trigger downstream commands in response to sensor conditions.

**Layer 5 — Telemetry**
Collects processed readings and routes them to one or more output destinations: local file, MQTT broker, or cloud dashboard. Sink failures are isolated and do not block other sinks.

**Layer 6 — System integration**
Handles daemon startup, graceful shutdown, automatic restart on failure, and health monitoring so the node runs reliably in production.

<img width="547" height="886" alt="image" src="https://github.com/user-attachments/assets/9568ed8e-4bca-4eb1-89c9-453da1b35d03" />


## Key points
- **Portable** — hardware-specific details are isolated so the same higher-level code runs across different boards and platforms.
- **Modular** — each responsibility is a separate component that can be replaced or extended independently.
- **Bidirectional** — sensor data flows upstream to telemetry; commands flow downstream to actuators on both the RPi and the Arduino.
- **Transport-agnostic** — UART and CAN operate simultaneously; each sensor source is individually assigned to one transport and can be switched at runtime without restarting the daemon.
- **Asynchronous** — the lock-free message bus decouples producers and consumers for non-blocking, deterministic data flow.
- **Observable** — structured logging, telemetry sinks, and a health watchdog provide runtime visibility.
- **Robust lifecycle** — clean startup, graceful shutdown, and automatic restart on failure are built in.

## Documentation
See [docs/edge_node.md](docs/edge_node.md) for architecture details, protocol specification, and hardware wiring.
