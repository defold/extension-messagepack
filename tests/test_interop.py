# Copyright 2020-2026 The Defold Foundation
# Copyright 2014-2020 King
# Copyright 2009-2014 Ragnar Svensson, Christian Murray
# Licensed under the Defold License version 1.0 (the "License"); you may not use
# this file except in compliance with the License.
#
# You may obtain a copy of the License, together with FAQs at
# https://www.defold.com/license
#
# Unless required by applicable law or agreed to in writing, software distributed
# under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
# CONDITIONS OF ANY KIND, either express or implied. See the License for the
# specific language governing permissions and limitations under the License.

"""Check actual codec output with the independent Python MessagePack implementation."""
import math
import random
import struct
import subprocess
import sys
import msgpack


class MapPairs(list):
    pass


def unpack(wire):
    return msgpack.unpackb(wire, raw=True, strict_map_key=False, object_pairs_hook=MapPairs)


def number(value, key=False):
    value = float(value)
    if math.isnan(value):
        return 'NaN'
    if value == 0 and not key:
        return ('zero', math.copysign(1, value))
    return value


def key_value(value):
    if value is None:
        return ('null',)
    if isinstance(value, bool):
        return ('boolean', value)
    if isinstance(value, (int, float)):
        return ('number', number(value, key=True))
    if isinstance(value, bytes):
        return ('string', value)
    raise AssertionError('Unexpected interoperability key type')


def normalized(value):
    # Lua maps and arrays share a table representation. Preserve nulls, numeric
    # rounding, byte strings and typed keys without assuming map entry order.
    if isinstance(value, MapPairs):
        return ('table', {key_value(k): normalized(v) for k, v in value})
    if isinstance(value, list):
        return ('table', {('number', float(i + 1)): normalized(v) for i, v in enumerate(value)})
    if value is None:
        return ('null',)
    if isinstance(value, bool):
        return ('boolean', value)
    if isinstance(value, (int, float)):
        return ('number', number(value))
    return ('string', value)


def main():
    rng = random.Random(20261113)
    packets = []
    values = [None, False, True, '', 'å🙂', b'\0\xff', [], {}, {None: 'null key'}, {True: 2, False: 3}]
    values += [-(2**63), -(2**53)-1, -(2**31)-1, -32769, -129, -33, -32, -1, 0, 127, 128, 255, 256, 65535, 65536, 2**32-1, 2**32, 2**53-1, 2**53+1, 2**63-1, 2**64-1]
    values += [-0.0, 1.5, -1.5, 2**-1074, float('inf'), -float('inf'), float('nan')]
    values += ['x'*n for n in (31, 32, 255, 256, 65535, 65536)]
    values += [b'\0'*n for n in (0, 255, 256, 65535, 65536)]

    def generated(depth=0):
        kind = rng.randrange(7 if depth < 4 else 5)
        if kind == 0:
            return None
        if kind == 1:
            return bool(rng.randrange(2))
        if kind == 2:
            return rng.randrange(-(2**53), 2**53)
        if kind == 3:
            return rng.uniform(-1000000, 1000000)
        if kind == 4:
            return bytes(rng.randrange(256) for _ in range(rng.randrange(65)))
        if kind == 5:
            return [generated(depth + 1) for _ in range(rng.randrange(7))]
        return {f'key{i}': generated(depth + 1) for i in range(rng.randrange(7))}

    values += [generated() for _ in range(1000)]
    for value in values:
        wire = msgpack.packb(value, use_bin_type=True)
        packets.append((wire, normalized(unpack(wire))))
    for value in (-0.0, 1.5, -(2**-149), (2-2**-23)*2**127, float('inf'), float('nan')):
        wire = msgpack.packb(value, use_single_float=True)
        packets.append((wire, normalized(unpack(wire))))
    # Python dicts merge bool/numeric keys; the pairs hook preserves their
    # distinction so this independent check follows Lua's actual key rules.
    for wire in (bytes.fromhex('8201a161c3a162'), bytes.fromhex('82a17801a17802')):
        packets.append((wire, normalized(unpack(wire))))
    invalid = [b'', b'\xc1', b'\x91', b'\x81\xc0', b'\x01\x02', bytes.fromhex('ddffffffff'), bytes.fromhex('dbffffffff')]
    valid = bytes.fromhex('81a17892cb3ff8000000000000a3616263')
    invalid += [valid[:n] for n in range(len(valid))]
    invalid += [msgpack.packb(msgpack.ExtType(7,b'x')),
                bytes.fromhex('d6ff00000000'), bytes.fromhex('d7ff0000000000000000'),
                b'\xc7\x0c\xff'+b'\0'*12,
                b'\x81\xcb'+struct.pack('>d',float('nan'))+b'\x01']
    packets += [(wire, None) for wire in invalid]
    result = subprocess.run([sys.argv[1], '--interop'], input=''.join(wire.hex()+'\n' for wire,_ in packets), text=True, capture_output=True, check=True)
    lines = result.stdout.splitlines()
    assert len(lines) == len(packets), (len(lines),len(packets),result.stderr)
    for i,(line,(wire,expected)) in enumerate(zip(lines,packets)):
        if expected is None:
            assert line == 'ERR', f'Accepted unsupported/malformed packet {i}: {wire.hex()}'
        else:
            assert line != 'ERR', f'Rejected valid packet {i}: {wire.hex()}'
            actual = normalized(unpack(bytes.fromhex(line)))
            assert actual == expected, f'Independent value mismatch at packet {i}'
    print(f'Independent MessagePack interoperability passed: {len(packets)} packets (Python msgpack {msgpack.__version__}).')


if __name__ == '__main__':
    main()
