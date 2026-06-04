# Smart2Raw ports

This directory contains independent, useful ports of the Smart2Raw idea. They are not artificial variants: each port targets a different ecosystem while preserving the same core expression of the project.

Current ports:

- `go/`: service/telemetry-oriented implementation with adaptive integer pools and portable `.s2r` save/load.
- `js/`: pure JavaScript implementation for Node.js, browser demos and portable `.s2r` handling.
- `python/`: pure-Python implementation for notebooks, tutorials and conformance fixtures.

Planned ports:

- `rust/`: safe Rust reference port.
- `wasm/`: browser/demo build for visualization and public demonstrations.
- `embedded/`: FreeRTOS/Zephyr/ESP32/STM32-oriented examples around the C core.

All ports are licensed under AGPL-3.0-or-later unless explicitly stated otherwise.

## JavaScript

`ports/js` is a pure JavaScript port for Node.js, browsers, documentation demos, and `.s2r` file handling. It includes adaptive signed/unsigned pools, promotion, demotion through `fitClass()`, BigInt sums, little-endian `.s2r` save/load, and CRC32 validation.

## Python

`ports/python` is a pure-Python port for notebooks, tutorials and conformance fixtures. It is dependency-free and implements adaptive pools, exact sums, `.s2r` save/load and CRC32 validation.
