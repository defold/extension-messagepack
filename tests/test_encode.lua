local function equal(a, b)
	if type(a) ~= type(b) then return false end
	if type(a) == "number" then return a == b or (a ~= a and b ~= b) end
	if type(a) ~= "table" then return a == b end
	for k, v in pairs(a) do if not equal(v, b[k]) then return false end end
	for k in pairs(b) do if a[k] == nil then return false end end
	return true
end

-- Verify wire encodings, lossless Lua round trips and cleanup after failures.
return function()
	local encode, decode, char = messagepack.encode, messagepack.decode, string.char
	local checks = 0
	local preserve_null = {decode_null_as_userdata = true}
	local function check(value, expected, options)
		local bytes = encode(value, options)
		assert(bytes == expected, "incorrect MessagePack wire encoding")
		assert(equal(decode(bytes, preserve_null), value == nil and messagepack.null or value))
		checks = checks + 1
	end
	local function roundtrip(value, options)
		assert(equal(value, decode(encode(value, options), preserve_null)), "round trip differs")
		checks = checks + 1
	end
	local function rejects(value, options)
		assert(not pcall(encode, value, options), "accepted unsupported input")
		assert(encode(42) == char(42), "encoder unusable after error")
		checks = checks + 1
	end

	check(nil, char(0xc0))
	check(messagepack.null, char(0xc0))
	check(json.null, char(0xc0))
	check(false, char(0xc2))
	check(true, char(0xc3))
	check(0, char(0))
	check(127, char(127))
	check(128, char(0xcc, 128))
	check(255, char(0xcc, 255))
	check(256, char(0xcd, 1, 0))
	check(65535, char(0xcd, 255, 255))
	check(65536, char(0xce, 0, 1, 0, 0))
	check(4294967295, char(0xce, 255, 255, 255, 255))
	check(4294967296, char(0xcf, 0, 0, 0, 1, 0, 0, 0, 0))
	check(2^63, char(0xcf, 128, 0, 0, 0, 0, 0, 0, 0))
	check(2^64, char(0xcb, 0x43, 0xf0, 0, 0, 0, 0, 0, 0))
	check(-32, char(0xe0))
	check(-33, char(0xd0, 0xdf))
	check(-128, char(0xd0, 0x80))
	check(-129, char(0xd1, 0xff, 0x7f))
	check(-32768, char(0xd1, 0x80, 0))
	check(-32769, char(0xd2, 0xff, 0xff, 0x7f, 0xff))
	check(-2147483648, char(0xd2, 0x80, 0, 0, 0))
	check(-2147483649, char(0xd3, 0xff, 0xff, 0xff, 0xff, 0x7f, 0xff, 0xff, 0xff))
	check(-2^63, char(0xd3, 0x80, 0, 0, 0, 0, 0, 0, 0))
	check(-2^64, char(0xcb, 0xc3, 0xf0, 0, 0, 0, 0, 0, 0))
	check(1.5, char(0xcb, 0x3f, 0xf8, 0, 0, 0, 0, 0, 0))
	check(-0.5, char(0xcb, 0xbf, 0xe0, 0, 0, 0, 0, 0, 0))
	check(-1 / math.huge, char(0xcb, 0x80, 0, 0, 0, 0, 0, 0, 0))
	assert(1 / decode(encode(-1 / math.huge)) == -math.huge)
	roundtrip(math.huge)
	roundtrip(-math.huge)
	roundtrip(0 / 0)
	check("", char(0xa0))
	check("a\0b", char(0xa3) .. "a\0b")
	check("å", char(0xa2) .. "å")
	check(string.rep("a", 32), char(0xd9, 32) .. string.rep("a", 32))
	check(string.rep("a", 256), char(0xda, 1, 0) .. string.rep("a", 256))
	check(string.rep("a", 65536), char(0xdb, 0, 1, 0, 0) .. string.rep("a", 65536))
	check({}, char(0x80))
	check({}, char(0x90), {encode_empty_table_as_map = false})
	check({1, 2, 3}, char(0x93, 1, 2, 3))
	check({1, messagepack.null, 3}, char(0x93, 1, 0xc0, 3))
	check({x = 7}, char(0x81, 0xa1) .. "x" .. char(7))
	roundtrip({[1] = "a", [3] = "c"})
	assert(encode({[1] = "a", [3] = "c"}):byte(1) == 0x82)
	roundtrip({[0] = false, [-1] = "x", [1.5] = 42, [true] = "yes", name = "map"})
	roundtrip({a = {1, 2}, b = {value = messagepack.null}, empty = {}})
	local shared = {x = 1}
	roundtrip({a = shared, b = shared})
	local raw = setmetatable({1, 2}, {__index = function() error("metamethod called") end, __len = function() error("metamethod called") end})
	check(raw, char(0x92, 1, 2))
	local large_array = {}
	for i = 1, 65536 do large_array[i] = 0 end
	assert(encode(large_array):sub(1, 5) == char(0xdd, 0, 1, 0, 0))
	roundtrip(large_array)
	local map = {}
	for i = 1, 16 do map["k" .. i] = i end
	assert(encode(map):sub(1, 3) == char(0xde, 0, 16))
	roundtrip(map)
	local nested = 0
	for _ = 1, 64 do nested = {nested} end
	check(nested, string.rep(char(0x91), 64) .. char(0))
	rejects({nested})
	local cycle = {}; cycle.self = cycle
	rejects(cycle)
	local key_cycle = {}; key_cycle[key_cycle] = 1
	rejects(key_cycle)
	local a, b = {}, {}; a.b = b; b.a = a
	rejects(a)
	-- Fail after writing enough data to grow the native output buffer.
	rejects({string.rep("x", 20000), function() end})
	rejects(function() end)
	rejects(coroutine.create(function() end))
	if newproxy then rejects(newproxy()) end
	rejects(1, "invalid options")
	assert(not pcall(encode))
	return checks + 5
end
