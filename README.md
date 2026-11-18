# Defold MessagePack

A native MessagePack encoder and decoder for custom binary data in Defold. This
extension parses directly into Lua tables and strings and encodes Lua values
into binary MessagePack strings.

Format reference: [MessagePack](https://msgpack.org/index.html).

Editor completion and API reference:
[messagepack.script_api](messagepack/api/messagepack.script_api).

## Downloading and decoding

Download a MessagePack file over HTTP and decode the response body directly.
Replace the example URLs with your own endpoints.

```lua
local function on_download(self, _, response)
	if response.status == 200 then
		self.config = messagepack.decode(response.response)
		pprint(self.config)
	else
		print("Download failed:", response.status)
	end
end

function init(self)
	http.request("https://example.com/data/config.msgpack", "GET", on_download)
end
```

## Encoding and uploading

Encode a Lua value and upload the binary string as the HTTP request body:

```lua
local function on_upload(self, _, response)
	if response.status >= 200 and response.status < 300 then
		print("Upload complete")
	else
		print("Upload failed:", response.status)
	end
end

function init(self)
	local config = {name = "player", position = {1, 2, 3}}
	local bytes = messagepack.encode(config)
	local headers = {["Content-Type"] = "application/msgpack"}
	http.request("https://example.com/data/config.msgpack", "PUT", on_upload, headers, bytes)
end
```

`messagepack` is registered by the native extension. No Lua `require` is needed.
Copy the `messagepack` directory into another Defold project, or use this
project as a library dependency. Only the `messagepack` directory is included
when used as a dependency.

## API and value mapping

```lua
local value = messagepack.decode(bytes)
local value = messagepack.decode(bytes, {decode_null_as_userdata = true})
local bytes = messagepack.encode(value)
local bytes = messagepack.encode(value, {encode_empty_table_as_map = false})
```

The decoder consumes exactly one value and raises a Lua error on malformed,
truncated or trailing input. Values map as follows:

| MessagePack | Lua |
|---|---|
| nil | `nil`, or `messagepack.null` with the option above |
| boolean | boolean |
| signed/unsigned integer, float32, float64 | number |
| string or binary | binary-safe string, including embedded NUL |
| array | table with indices starting at 1 |
| map | table |

The default null mapping matches `json.decode`: null object fields and null
array entries become absent Lua entries. Use `decode_null_as_userdata` to
preserve them; `messagepack.null` is the same light-userdata sentinel used by
`json.null`. Lua numbers are doubles, so integers above 2^53 can lose precision,
as with JSON. Arrays and maps both become ordinary tables, including empty
containers. Duplicate map keys use the last value. Lua's restrictions on nil
and NaN keys apply. Nesting is limited to 64 containers.

`messagepack.encode` supports nil, booleans, numbers, strings, tables and the
`messagepack.null` / `json.null` sentinel. Dense tables with exactly the integer
keys `1..n` encode as arrays. Sparse or mixed-key tables encode as maps without
dropping keys. Empty tables default to maps; set `encode_empty_table_as_map`
to false to encode empty arrays. This option applies to nested empty tables too.

Encoding uses raw table entries and does not invoke table metamethods. Map
entry order is unspecified. Shared tables are encoded as separate copies;
cycles, unsupported types and nesting beyond 64 containers raise Lua errors.
Integral numbers use the smallest signed or unsigned integer width when the
Lua value fits; other numbers use float64. Negative zero, NaN and infinities
are preserved. Strings use the MessagePack string type and retain every byte.
Decoding binary data and re-encoding it produces a string tag; Lua strings do
not retain the distinction between MessagePack string and binary tags.

For example, to preserve a null array entry:

```lua
local bytes = messagepack.encode({1, messagepack.null, 3})
local values = messagepack.decode(bytes, {decode_null_as_userdata = true})
assert(values[2] == messagepack.null)
```

MessagePack application extension types, including the timestamp extension,
raise an explicit unsupported-type error. Functions, threads and other userdata
are also unsupported by the encoder.

The production decoder uses a bounded, SAX-style memory reader that emits
values directly into Lua. It allocates tables from the encoded element counts,
uses alignment-safe scalar loads, and handles numeric array values without
recursive dispatch for each float64. The reader allocates no native heap and
borrows its input only for the call. The encoder uses MPack's writer. Neither
Lua path builds an intermediate native tree. Encoder output
is copied into a Lua-owned string; native buffers are released on success,
invalid input and allocation failure.

## Unit tests and CI

The [unit tests](tests/README.md) build the production codec against a pinned
Lua 5.1.5 source archive, without a Defold checkout or build server.
[GitHub Actions](.github/workflows/ci.yml) runs ordinary and ASan/UBSan builds
on Linux and macOS.

Coverage includes all 256 leading bytes, supported format and length boundaries,
special floats, malformed/truncated/trailing input, allocation-failure recovery,
59,000 differential cases against an independent MPack reader, and Python
MessagePack interoperability. The suite also checks live `.script_api` exports
and compiles its Lua examples.

```sh
python3 -m venv .cache/test-python
.cache/test-python/bin/python -m pip install -r tests/requirements.txt
cmake -S tests -B build_unit -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE="$PWD/.cache/test-python/bin/python"
cmake --build build_unit --parallel 4
ctest --test-dir build_unit --output-on-failure
```

Open `game.project` in Defold to run the codec and specification tests in the
engine. CI also performs a full Bob build and runs the resulting engine; see
the [Defold integration build instructions](tests/README.md#defold-integration-build).
The library dependency includes only the native extension.

## Dependencies and license

The decoder uses a bounded memory reader and MPack's alignment-safe scalar
helpers. Encoding uses the MPack 1.1.1 writer. MPack retains its MIT license;
see [its notice and include-path adjustment](messagepack/src/mpack/README.md).
This extension is licensed under the [MIT License](LICENSE).
