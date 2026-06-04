# Smart2Raw Python port

Pure-Python Smart2Raw port for notebooks, tutorials, test fixtures and conformance checks.
It is intentionally small and dependency-free. The C header remains the canonical high-performance implementation.

Features:

- adaptive signed and unsigned integer pools;
- automatic promotion on `push()`;
- `fit_class()` demotion after outlier removal;
- exact Python integer sums;
- canonical portable `.s2r` save/load;
- little-endian payloads and CRC32 IEEE validation.

## Test

```sh
cd ports/python
python3 -m unittest discover -s tests
```

## Example

```python
from smart2raw import Smart2RawPool

p = Smart2RawPool([25, 30, 40])
print(p.size)      # 8
print(p.sum())     # 95
p.save("temperatures.s2r")
```

License: AGPL-3.0-or-later, inherited from the main project.
