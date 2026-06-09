# Smart2Raw Roadmap

This roadmap lists natural next steps for the Smart2Raw ecosystem. It is not a promise of delivery order; it is a technical direction map.

## Completed in this development line

- Analytics v2 primitives: sort, sortedness checks, unique, distinct counts and value counts across the C core and Go/JavaScript/Python ports.

## Near term

- Keep the C header-only core as the canonical implementation.
- Maintain Go, JavaScript and Python ports as useful ecosystem expressions.
- Expand cross-language `.s2r` conformance tests.
- Add more real-world fixtures: telemetry, sensor data, status codes, token IDs and quantized integer artifacts.
- Improve CLI workflows for `inspect`, `convert`, `verify`, `sum`, `sort`, `unique`, `value-counts` and `info`.

## Medium term

- Rust port with a safe public API and carefully isolated `unsafe` internals.
- WebAssembly playground for browser demos.
- Embedded examples for FreeRTOS, Zephyr, ESP32, STM32 and PlatformIO.
- Windows memory-mapping backend.
- Android NDK wrapper.
- Swift wrapper for Apple platforms.

## Data and analytics integrations

- SQLite extension.
- DuckDB adapter.
- Apache Arrow bridge.
- NumPy bridge.
- PyTorch or ONNX tooling for integer/quantized auxiliary reductions.

## Architecture targets

- Linux x86_64 validation.
- Linux ARM64 validation on real hardware.
- macOS Apple Silicon validation.
- RISC-V scalar validation through the portable C path.
- AVX-512: dedicated u8 sum path added and measured on hardware (~1.17-1.30x over AVX2 for u8; u16 stays on AVX2 by measurement).
- RISC-V Vector (RVV 1.0): experimental SIMD path for sum added (logic validated via emulation); pending validation on an rv64gcv toolchain plus QEMU/hardware before being promoted to supported.
- ARM SVE2: experimental SIMD path for sum added (logic validated via emulation); pending real SVE hardware. Marginal expected gain at 128-bit SVE.
- Broader ARM NEON validation on real hardware.

## Format evolution

- Block-wise `.s2r` serialization.
- Optional metadata blocks.
- Extended checksums or integrity modes.
- Experimental sub-byte mode for int4-style use cases.

## Philosophy

Smart2Raw should remain honest about its scope: it is a compact integer storage and scan layer. It should not claim to be a general-purpose compressor, a dense linear algebra engine or a replacement for mature columnar databases. Its value is strongest where integer data is stored wider than necessary and where memory, cache, bandwidth, I/O or edge capacity matters.
