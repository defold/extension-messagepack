# Codec unit tests

These tests compile the production codec with real Lua 5.1 headers and a pinned
Lua 5.1.5 source archive, verified by SHA-256. The small headers under `support/`
adapt Defold's include paths to those public headers; they do not implement or
mock Lua. The codec uses only the extension's public include root, matching
Defold's header layout. No Defold engine checkout, SDK binaries or extension-build
server is required for these standalone tests. Test-only `json.null` aliases the
same null light-userdata sentinel.

```sh
python3 -m venv .cache/test-python
.cache/test-python/bin/python -m pip install -r tests/requirements.txt
cmake -S tests -B build_unit -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE="$PWD/.cache/test-python/bin/python"
cmake --build build_unit --parallel 4
ctest --test-dir build_unit --output-on-failure
```

Configure a second build with `-DMESSAGEPACK_SANITIZE=ON` to instrument the codec
and harness with AddressSanitizer and UndefinedBehaviorSanitizer. Lua is an
external dependency and is not instrumented. The workflow runs ordinary and
sanitized builds on Linux and macOS. Test allocation hooks check native cleanup and inject allocation failures
independently of the sanitizer.

## Specification coverage

The golden vectors in `test_spec.lua` are derived directly from the
[MessagePack specification](https://github.com/msgpack/msgpack/blob/master/spec.md),
independently of the extension's encoder. They cover every leading byte:

- Positive/negative fixints, every signed/unsigned integer width, boundaries
  and non-minimal encodings.
- Float32/float64, subnormals, signed zero, infinities and NaN.
- Fixed and variable string, binary, array and map formats, including the
  15/16, 31/32, 255/256 and 65535/65536 length transitions.
- Reserved bytes, truncated headers/payloads, excessive lengths, trailing data,
  invalid Lua keys, duplicate keys and explicit rejection of extension types,
  including valid timestamp32/64/96 packets.

The supported profile maps MessagePack values to ordinary Lua values. Integer
precision is limited by Lua doubles; arrays/maps share the table representation;
strings/binary share a byte-string representation. Encoding Lua strings uses
the string tag without validating UTF-8. Extension types and timestamps are
unsupported. These mappings are tested rather than claiming full wire-type
identity for decode/encode round trips.

`test_interop.py` uses the independently maintained Python `msgpack` package:
it generates packets, sends them through the actual C decoder and encoder,
then decodes and compares the emitted bytes with Python. Comparison accounts
for the documented Lua mappings, map order, numeric rounding, nulls, signed
zero and NaN. This prevents a shared encoder/decoder bug from passing solely
because the two agree with each other.

Existing unit cases, 59,000 deterministic differential cases against an
independent MPack tag reader, allocator-failure/recovery trials, live API checks
and compilation of all `.script_api` examples run alongside the golden vectors.
The reference decoder is used only by tests; the extension uses its own bounded
memory decoder. The C/C++ tests use the bundled MPack library.
