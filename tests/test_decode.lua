local function equal(a, b)
	if type(a) ~= type(b) then return false end
	if type(a) ~= "table" then return a == b end
	for k, v in pairs(a) do
		if not equal(v, b[k]) then return false end
	end
	for k in pairs(b) do
		if a[k] == nil then return false end
	end
	return true
end

return function()
	local decode = messagepack.decode
	local char = string.char
	local checks = 0
	local function check(bytes, expected, options)
		assert(equal(decode(bytes, options), expected), "decoded value differs")
		checks = checks + 1
	end
	local function rejects(bytes, options)
		assert(not pcall(decode, bytes, options), "accepted invalid MessagePack")
		-- Failure must leave the decoder usable and its Lua stack intact.
		assert(decode(char(42)) == 42)
		checks = checks + 1
	end

	-- Scalars and all integer widths, including signed boundary encodings.
	check(char(0xc0), nil)
	check(char(0xc0), messagepack.null, {decode_null_as_userdata = true})
	check(char(0xc2), false)
	check(char(0xc3), true)
	check(char(0), 0)
	check(char(0x7f), 127)
	check(char(0xe0), -32)
	check(char(0xff), -1)
	check(char(0xcc, 0xff), 255)
	check(char(0xcd, 0xff, 0xff), 65535)
	check(char(0xce, 0xff, 0xff, 0xff, 0xff), 4294967295)
	check(char(0xcf, 0, 0x20, 0, 0, 0, 0, 0, 0), 9007199254740992)
	check(char(0xd0, 0x80), -128)
	check(char(0xd1, 0x80, 0), -32768)
	check(char(0xd2, 0x80, 0, 0, 0), -2147483648)
	check(char(0xd3, 0x80, 0, 0, 0, 0, 0, 0, 0), -9223372036854775808)
	check(char(0xca, 0x3f, 0xc0, 0, 0), 1.5)
	check(char(0xcb, 0x3f, 0xf8, 0, 0, 0, 0, 0, 0), 1.5)
	check(char(0xa0), "")
	check(char(0xa3) .. "a\0b", "a\0b")
	check(char(0xd9, 3) .. "hej", "hej")
	check(char(0xda, 0, 3) .. "hej", "hej")
	check(char(0xdb, 0, 0, 0, 3) .. "hej", "hej")
	check(char(0xc4, 3, 0, 0xff, 1), char(0, 0xff, 1))
	check(char(0xc5, 0, 1, 42), "*")
	check(char(0xc6, 0, 0, 0, 1, 42), "*")
	check(char(0x90), {})
	check(char(0x80), {})
	check(char(0xdc, 0, 2, 1, 2), {1, 2})
	check(char(0xdd, 0, 0, 0, 2, 1, 2), {1, 2})
	check(char(0xde, 0, 1, 0xa1) .. "x" .. char(7), {x = 7})
	check(char(0xdf, 0, 0, 0, 1, 0xa1) .. "x" .. char(7), {x = 7})
	check(char(0x83, 0xa1) .. "a" .. char(0x93, 1, 0xc0, 3, 0xa1) .. "b" .. char(0xc2, 0xa1) .. "c" .. char(0xa2) .. "å", {a = {[1] = 1, [3] = 3}, b = false, c = "å"})
	check(char(0x93, 1, 0xc0, 3), {1, messagepack.null, 3}, {decode_null_as_userdata = true})
	check(char(0x82, 1, 7, 0xc3, 8), {[1] = 7, [true] = 8})
	check(string.rep(char(0x91), 64) .. char(0), (function()
		local value = 0
		for _ = 1, 64 do value = {value} end
		return value
	end)())

	-- Bounds checks must reject lengths before allocating enormous Lua tables.
	for _, bytes in ipairs({"", char(0xc1), char(0xd9), char(0xa2) .. "a", char(0x91), char(0x81, 1), char(1, 2), char(0x81, 0xc0, 1), char(0xdd, 0xff, 0xff, 0xff, 0xff), char(0xdf, 0xff, 0xff, 0xff, 0xff), char(0xd4, 0, 0), string.rep(char(0x91), 65) .. char(0), char(0x81, 0xcb, 0x7f, 0xf8, 0, 0, 0, 0, 0, 0, 1)}) do
		rejects(bytes)
	end
	-- Every proper prefix of a nested value must fail, including string payloads.
	local complete = char(0x81, 0xa1) .. "x" .. char(0x93, 1, 0xa3) .. "abc" .. char(0xc4, 2, 0, 0xff)
	for i = 0, #complete - 1 do rejects(complete:sub(1, i)) end
	-- Extension handlers: their result replaces the value, nil drops it like a null.
	local undefined = {ext = {[0] = function() return nil end}}
	check(char(0x82, 0xa1) .. "a" .. char(1, 0xa1) .. "b" .. char(0xd4, 0, 0), {a = 1}, undefined)
	check(char(0x93, 1, 0xd4, 0, 0, 3), {[1] = 1, [3] = 3}, undefined)
	local timestamp = {ext = {[-1] = function(data) return {seconds = data:byte(4)} end}}
	check(char(0x91, 0xd6, 0xff, 0, 0, 0, 42), {{seconds = 42}}, timestamp)
	check(char(0x81, 0xd4, 1, 9, 7), {[9] = 7}, {ext = {[1] = function(data) return data:byte() end}})
	check(char(0xd5, 2, 0x91, 0x2a), {42}, {ext = {[2] = function(data) return decode(data) end}})
	check(char(0x92, 0xc0, 0xd4, 0, 0), {messagepack.null, true}, {decode_null_as_userdata = true, ext = {[0] = function() return true end}})
	-- Rejected: unregistered types, nil/NaN keys, bad handlers, and handler errors.
	rejects(char(0xd4, 1, 0), undefined)
	rejects(char(0x81, 0xd4, 0, 0, 1), undefined)
	rejects(char(0x81, 0xd4, 0, 0, 1), {ext = {[0] = function() return 0 / 0 end}})
	rejects(char(0xd4, 0, 0), {ext = {[0] = true}})
	rejects(char(0xd4, 0, 0), {ext = true})
	rejects(char(0xd4, 0, 0), {ext = {[0] = function() error("handler failed") end}})
	local _, message = pcall(decode, char(0xd6, 0xff, 0, 0, 0, 0))
	assert(message:find("unsupported extension type -1", 1, true), message)
	_, message = pcall(decode, char(0xd4, 0, 0), {ext = {[0] = function() error("handler failed") end}})
	assert(message:find("handler failed", 1, true), message)
	-- Truncated extensions fail before any handler runs.
	local calls = 0
	local counting = {ext = {[0] = function() calls = calls + 1 end}}
	for _, bytes in ipairs({char(0xd4), char(0xd4, 0), char(0xd8, 0) .. string.rep("x", 15), char(0xc7), char(0xc7, 2, 0, 0), char(0xc9, 0xff, 0xff, 0xff, 0xff, 0)}) do
		rejects(bytes, counting)
	end
	assert(calls == 0)
	assert(not pcall(decode, {}))
	assert(not pcall(decode, char(1), "invalid options"))
	return checks + 2
end
