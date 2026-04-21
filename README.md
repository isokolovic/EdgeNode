# EdgeNode

Embedded edge gateway that reads sensors, processes data, and publishes telemetry. C++23 on Raspberry Pi, C++11 on Arduino.

## Overview

A Raspberry Pi daemon ingests data from physical sensors and an Arduino, processes it through an edge pipeline, and publishes telemetry to a cloud dashboard. The Arduino reads sensors, packages structured frames, and sends them to the RPi over a serial or CAN bus connection. GPIO sensors on the RPi are read directly. The daemon starts, initialises all connected hardware, and runs everything — the hardware determines what data flows.

## Architecture

**Hardware abstraction layer**
Provides a consistent interface to hardware peripherals so higher layers do not depend on specific chips or buses.

**Protocol adapters**
Convert raw hardware input into a single, unified sensor reading format that the rest of the system can use.

**Asynchronous messaging layer**
Decouples producers and consumers of data so components can run independently and communicate without blocking.

**Data processing pipeline**
Applies filtering, calibration, anomaly detection, and rule-based handling to sensor readings before they are acted on or stored.

**Telemetry and storage**
Collects processed data and operational metrics for remote monitoring and historical analysis.

**System integration and lifecycle**
Handles startup, shutdown, error recovery, and health monitoring so the node runs reliably in production.

<img width="893" height="899" alt="image" src="https://github.com/user-attachments/assets/66e5699f-ea43-4165-9efd-02602f378d24" />


## Key points

- **Portable** — hardware-specific details are isolated so the same higher-level code runs across different boards.
- **Modular** — each responsibility is a separate component that can be replaced or extended.
- **Asynchronous** — components communicate without tight coupling to improve responsiveness and resilience.
- **Observable** — the system exposes runtime metrics and logs for monitoring and troubleshooting.
- **Robust lifecycle** — clean startup, graceful shutdown, and health checks are built in.

## Documentation

See [docs/edge_node.md](docs/edge_node.md) for architecture details, protocol specification, hardware wiring.
