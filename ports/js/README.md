# Smart2Raw JavaScript port

This is a pure JavaScript expression of the Smart2Raw design for Node.js, browsers,
demos, documentation playgrounds, and portable `.s2r` file handling.

The canonical implementation remains the C header at `include/smart2raw.h`. This
port is intentionally useful rather than artificial: it provides adaptive signed
and unsigned integer pools, automatic promotion, demotion through `fitClass()`,
O(1) indexed reads, exact integer sums through `BigInt`, and portable `.s2r`
save/load with little-endian payloads and CRC32.

## Quickstart

```js
import { Smart2RawPool } from './smart2raw.mjs';

const pool = new Smart2RawPool();
pool.push(25);
pool.push(300);          // promotes from 8 to 16 bits
console.log(pool.size);  // 16
console.log(pool.sum()); // 325n
```

## Tests

```sh
npm test
```

## License

AGPL-3.0-or-later. Commercial licensing is available from the copyright holder.
