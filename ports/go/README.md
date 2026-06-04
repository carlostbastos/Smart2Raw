# Smart2Raw Go port

This is a Go implementation of the Smart2Raw adaptive integer pool for services, telemetry agents, log processors, and other backend workloads.

It currently implements:

- unsigned adaptive integer pools;
- signed adaptive integer pools;
- automatic class promotion on push;
- `FitClass()` demotion after outliers are removed;
- O(1) indexed reads;
- exact `int64` sums for practical telemetry ranges;
- portable `.s2r` save/load with little-endian payload and CRC32.

The canonical implementation remains the C header in `include/smart2raw.h`. This Go port is a real ecosystem expression, not an artificial variant.

License: AGPL-3.0-or-later.
