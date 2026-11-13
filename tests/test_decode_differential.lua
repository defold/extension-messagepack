-- Copyright 2020-2026 The Defold Foundation
-- Copyright 2014-2020 King
-- Copyright 2009-2014 Ragnar Svensson, Christian Murray
-- Licensed under the Defold License version 1.0 (the "License"); you may not use
-- this file except in compliance with the License.
--
-- You may obtain a copy of the License, together with FAQs at
-- https://www.defold.com/license
--
-- Unless required by applicable law or agreed to in writing, software distributed
-- under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
-- CONDITIONS OF ANY KIND, either express or implied. See the License for the
-- specific language governing permissions and limitations under the License.

-- Compare observable values and acceptance of malformed bytes across decoders.
return function()
	local random_state = 1701
	local function random(maximum)
		random_state = (random_state * 1664525 + 1013904223) % 4294967296
		return math.floor(random_state / 4294967296 * maximum)
	end
	local function fingerprint(value)
		local kind = type(value)
		if kind == "table" then
			local entries = {}
			for k, v in pairs(value) do entries[#entries + 1] = fingerprint(k) .. fingerprint(v) end
			table.sort(entries)
			return "t" .. #entries .. ":" .. table.concat(entries)
		elseif kind == "string" then
			return "s" .. #value .. ":" .. value
		elseif kind == "number" then
			if value ~= value then return "nNaN;" end
			if value == 0 then return 1 / value < 0 and "n-0;" or "n0;" end
			return "n" .. string.format("%.17g", value) .. ";"
		end
		return kind .. ":" .. tostring(value) .. ";"
	end
	local options = {decode_null_as_userdata = true}
	local checks = 0
	local function check(bytes, preserve)
		local ok, expected = pcall(reference_decode, bytes, preserve and options or nil)
		if ok then expected = fingerprint(expected) end
		local valid, actual = pcall(messagepack.decode, bytes, preserve and options or nil)
		assert(valid == ok, "decode acceptance differs at case " .. checks)
		if valid then assert(fingerprint(actual) == expected, "decoded value differs at case " .. checks) end
		checks = checks + 1
	end
	local function value(depth)
		local kind = random(depth < 5 and 6 or 4)
		if kind == 0 then return messagepack.null end
		if kind == 1 then return random(2) == 1 end
		if kind == 2 then return (random(2000000000) - 1000000000) / (random(2) == 1 and 1000 or 1) end
		if kind == 3 then
			local bytes = {}
			for i = 1, random(33) do bytes[i] = string.char(random(256)) end
			return table.concat(bytes)
		end
		local result = {}
		for i = 1, random(9) do result[kind == 4 and i or "k" .. i] = value(depth + 1) end
		return result
	end
	for i = 1, 50000 do
		local bytes = {}
		for j = 1, random(65) do bytes[j] = string.char(random(256)) end
		check(table.concat(bytes), i % 2 == 0)
		if i % 1000 == 0 then collectgarbage("collect") end
	end
	for i = 1, 3000 do
		local bytes = messagepack.encode(value(0))
		check(bytes, true)
		if #bytes > 0 then
			check(bytes:sub(1, random(#bytes)), true)
			local at = random(#bytes) + 1
			check(bytes:sub(1, at - 1) .. string.char(random(256)) .. bytes:sub(at + 1), true)
		end
		if i % 100 == 0 then collectgarbage("collect") end
	end
	return checks
end
