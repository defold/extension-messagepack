-- Copyright 2020-2026 The Defold Foundation
-- Copyright 2014-2020 King
-- Copyright 2009-2014 Ragnar Svensson, Christian Murray
-- SPDX-License-Identifier: MIT

-- Golden bytes follow https://github.com/msgpack/msgpack/blob/master/spec.md.
-- Cover all 256 leading bytes, wire-width boundaries, floating values and the
-- documented Lua mapping. Extension tags decode only through ext handlers.
return function()
	local decode, encode, char = messagepack.decode, messagepack.encode, string.char
	local preserve = {decode_null_as_userdata = true}
	local checks, covered = 0, {}
	local function hex(text)
		return (text:gsub("%s", ""):gsub("..", function(pair) return char(tonumber(pair, 16)) end))
	end
	local function be(value, width)
		local bytes = {}
		for i = width, 1, -1 do bytes[i] = char(value % 256); value = math.floor(value / 256) end
		return table.concat(bytes)
	end
	local function equal(a, b)
		if type(a) ~= type(b) then return false end
		if type(a) == "number" then
			if a ~= a then return b ~= b end
			return a == b and (a ~= 0 or 1/a == 1/b)
		end
		if type(a) ~= "table" then return a == b end
		for k, v in pairs(a) do if not equal(v, b[k]) then return false end end
		for k in pairs(b) do if a[k] == nil then return false end end
		return true
	end
	local function check(bytes, expected)
		assert(equal(decode(bytes, preserve), expected), "golden value differs at case " .. checks)
		covered[bytes:byte(1)] = true
		checks = checks + 1
	end
	local function rejects(bytes)
		assert(not pcall(decode, bytes, preserve), "accepted unsupported or malformed bytes")
		if #bytes > 0 then covered[bytes:byte(1)] = true end
		assert(decode(char(42)) == 42, "decoder unusable after error")
		checks = checks + 1
	end
	for n = 0, 127 do check(char(n), n); assert(encode(n) == char(n)) end
	for n = -32, -1 do check(char(n + 256), n); assert(encode(n) == char(n + 256)) end
	for n = 0, 31 do
		local text = string.rep("s", n)
		check(char(0xa0 + n) .. text, text)
		assert(encode(text) == char(0xa0 + n) .. text)
	end
	for n = 0, 15 do
		local array, map, values, pairs_bytes = {}, {}, {}, {}
		for i = 1, n do
			array[i], values[i] = i, char(i)
			local key = "k" .. i
			map[key] = i
			pairs_bytes[i] = char(0xa0 + #key) .. key .. char(i)
		end
		check(char(0x90 + n) .. table.concat(values), array)
		check(char(0x80 + n) .. table.concat(pairs_bytes), map)
	end
	check(char(0xc0), messagepack.null)
	check(char(0xc2), false)
	check(char(0xc3), true)
	rejects(char(0xc1))
	local integers = {
		{"cc00", 0}, {"ccff", 255}, {"cd0000", 0}, {"cdffff", 65535},
		{"ce00000000", 0}, {"ceffffffff", 4294967295},
		{"cf0000000000000000", 0}, {"cf001fffffffffffff", 2^53 - 1},
		{"cf0020000000000001", 2^53}, {"cfffffffffffffffff", 2^64},
		{"d000", 0}, {"d07f", 127}, {"d080", -128}, {"d0ff", -1},
		{"d10000", 0}, {"d17fff", 32767}, {"d18000", -32768}, {"d1ffff", -1},
		{"d200000000", 0}, {"d27fffffff", 2147483647}, {"d280000000", -2147483648}, {"d2ffffffff", -1},
		{"d30000000000000000", 0}, {"d37fffffffffffffff", 2^63},
		{"d38000000000000000", -2^63}, {"d3ffffffffffffffff", -1},
	}
	for _, row in ipairs(integers) do
		check(hex(row[1]), row[2])
		check(char(0x81) .. hex(row[1]) .. char(42), {[row[2]]=42})
	end
	local floats = {
		{"ca00000000", 0}, {"ca80000000", -1/math.huge},
		{"ca00000001", 2^-149}, {"ca00800000", 2^-126},
		{"ca7f7fffff", (2 - 2^-23)*2^127}, {"cabfc00000", -1.5},
		{"ca7f800000", math.huge}, {"caff800000", -math.huge}, {"ca7fc00000", 0/0},
		{"ca7f800001", 0/0}, {"caff800001", 0/0}, {"caffffffff", 0/0},
		{"cb0000000000000000", 0}, {"cb8000000000000000", -1/math.huge},
		{"cb0000000000000001", 2^-1074}, {"cb0010000000000000", 2^-1022},
		{"cb3ff8000000000000", 1.5}, {"cb7fefffffffffffff", (2 - 2^-52)*2^1023},
		{"cb7ff0000000000000", math.huge}, {"cbfff0000000000000", -math.huge},
		{"cb7ff8000000000000", 0/0},
		{"cb7ff0000000000001", 0/0}, {"cbfff9000000000001", 0/0}, {"cbffffffffffffffff", 0/0},
	}
	for _, row in ipairs(floats) do
		check(hex(row[1]), row[2])
		local packet = char(0x81) .. hex(row[1]) .. char(42)
		if row[2] ~= row[2] then
			rejects(packet)
			assert(not pcall(decode, packet))
		else
			check(packet, {[row[2]]=42})
		end
	end
	check(char(0x93) .. hex("cb7ff0000000000000") .. char(42) .. hex("cb8000000000000000"), {math.huge, 42, -1/math.huge})
	for _, row in ipairs({{0xd9,1,{0,31,32,255}}, {0xda,2,{0,255,256,65535}}, {0xdb,4,{0,65535,65536}}, {0xc4,1,{0,255}}, {0xc5,2,{0,255,256,65535}}, {0xc6,4,{0,65535,65536}}}) do
		for _, n in ipairs(row[3]) do
			local text = string.rep(row[1] >= 0xd9 and "s" or "\0\255", n):sub(1,n)
			local bytes = char(row[1]) .. be(n,row[2]) .. text
			check(bytes, text)
			check(char(0x81) .. bytes .. char(42), {[text]=42})
		end
	end
	for _, row in ipairs({{0xdc,2,{0,15,16,65535}}, {0xdd,4,{0,65535,65536}}}) do
		for _, n in ipairs(row[3]) do
			local bytes = char(row[1]) .. be(n,row[2]) .. string.rep(char(42),n)
			local value = decode(bytes, preserve)
			assert(#value == n and (n == 0 or (value[1] == 42 and value[n] == 42)))
			covered[row[1]] = true; checks = checks + 1
		end
	end
	for _, row in ipairs({{0xde,2,{0,15,16,65535}}, {0xdf,4,{0,65535,65536}}}) do
		for _, n in ipairs(row[3]) do
			-- Repeated keys keep this boundary test's expected result small and
			-- independently exercise the documented last-value-wins behavior.
			check(char(row[1]) .. be(n,row[2]) .. string.rep(char(0xa1).."x"..char(42),n), n == 0 and {} or {x=42})
		end
	end
	for _, row in ipairs({{0xd4,1},{0xd5,2},{0xd6,4},{0xd7,8},{0xd8,16}}) do
		rejects(char(row[1],0) .. string.rep("x",row[2]))
	end
	for _, row in ipairs({{0xc7,1},{0xc8,2},{0xc9,4}}) do rejects(char(row[1]) .. be(1,row[2]) .. char(0) .. "x") end
	-- Without an ext handler, valid timestamp32, timestamp64 and timestamp96 are rejected.
	rejects(hex("d6ff00000000"))
	rejects(hex("d7ff0000000000000000"))
	rejects(hex("c70cff000000000000000000000000"))
	-- With handlers, every extension format delivers its signed type and raw payload.
	local raw = {ext = {}}
	for t = -128, 127 do raw.ext[t] = function(data, ext_type) return {ext_type, data} end end
	local function extension(bytes, ext_type, data)
		local value = decode(bytes, raw)
		assert(value[1] == ext_type and value[2] == data, "extension framing differs at case " .. checks)
		checks = checks + 1
	end
	for _, row in ipairs({{0xd4,1},{0xd5,2},{0xd6,4},{0xd7,8},{0xd8,16}}) do
		extension(char(row[1], 0x80) .. string.rep("x", row[2]), -128, string.rep("x", row[2]))
	end
	for _, row in ipairs({{0xc7,1,{0,255}},{0xc8,2,{0,256,65535}},{0xc9,4,{0,65536}}}) do
		for _, n in ipairs(row[3]) do
			local data = string.rep("\0\255", n):sub(1, n)
			extension(char(row[1]) .. be(n,row[2]) .. char(0x7f) .. data, 127, data)
		end
	end
	extension(hex("d6ff00000000"), -1, string.rep("\0", 4))
	extension(hex("d7ff0000000000000000"), -1, string.rep("\0", 8))
	extension(hex("c70cff000000000000000000000000"), -1, string.rep("\0", 12))
	for _, bytes in ipairs({hex("d6ff00000000"), hex("c70cff000000000000000000000000"), hex("c9000000020501ff")}) do
		for n = 0, #bytes - 1 do assert(not pcall(decode, bytes:sub(1, n), raw)); checks = checks + 1 end
		assert(not pcall(decode, bytes .. char(0), raw)); checks = checks + 1
	end
	-- A 4 GiB ext 32 length must not wrap the bounds check on 32-bit targets.
	assert(not pcall(decode, hex("c9ffffffff00"), raw)); checks = checks + 1
	check(hex("81c02a"), {[messagepack.null]=42})
	assert(not pcall(decode, hex("81c02a")))
	check(hex("82012ac32b"), {[1]=42, [true]=43})
	check(hex("81c22a"), {[false]=42})
	check(hex("81a1c02a"), {[char(0xc0)]=42})
	check(hex("81d901cb2a"), {[char(0xcb)]=42})
	check(hex("81c401ca2a"), {[char(0xca)]=42})
	-- Containers are valid identity keys, including a table containing NaN.
	for _, bytes in ipairs({"81902a", "81802a", "8191012a", "8181a178012a", "8191ca7fc000002a"}) do
		local value = decode(hex(bytes), preserve)
		local key, entry = next(value)
		assert(type(key) == "table" and entry == 42 and next(value, key) == nil)
		if bytes == "8191012a" then assert(key[1] == 1) end
		if bytes == "8181a178012a" then assert(key.x == 1) end
		if bytes == "8191ca7fc000002a" then assert(key[1] ~= key[1]) end
		checks = checks + 1
	end
	rejects(hex("8181ca7fc00000012a"))
	rejects(hex("81cb7ff80000000000002a"))
	for _, bytes in ipairs({hex("ddffffffff"),hex("dfffffffff"),hex("dbffffffff"),hex("c6ffffffff")}) do rejects(bytes) end
	for _, bytes in ipairs({hex("cb3ff8000000000000"),hex("d912")..string.rep("a",18),hex("c600000003000102"),hex("9201a3616263"),hex("81a17892c301")}) do
		for n = 0, #bytes-1 do rejects(bytes:sub(1,n)) end
		rejects(bytes .. char(0))
	end
	for n = 0, 255 do assert(covered[n], "missing leading-byte coverage " .. n) end
	return checks
end
