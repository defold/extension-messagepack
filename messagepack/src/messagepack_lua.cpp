// Copyright 2020-2026 The Defold Foundation
// Copyright 2014-2020 King
// Copyright 2009-2014 Ragnar Svensson, Christian Murray
// Licensed under the Defold License version 1.0 (the "License"); you may not use
// this file except in compliance with the License.
//
// You may obtain a copy of the License, together with FAQs at
// https://www.defold.com/license
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "messagepack_lua.h"
#include "messagepack_decode.h"
#include <messagepack/mpack.h>
#include <limits.h>
#include <math.h>

extern "C"
{
#include <dmsdk/lua/lauxlib.h>
}

namespace dmMessagePack
{
    struct DecodeContext
    {
        lua_State* m_Lua;
        bool       m_NullAsUserdata;

        void Null()
        {
            if (m_NullAsUserdata)
            {
                lua_pushlightuserdata(m_Lua, 0);
            }
            else
            {
                lua_pushnil(m_Lua);
            }
        }
        void Boolean(bool value)
        {
            lua_pushboolean(m_Lua, value);
        }
        void Number(double value)
        {
            lua_pushnumber(m_Lua, value);
        }
        void String(const char* bytes, uint32_t count)
        {
            lua_pushlstring(m_Lua, bytes, count);
        }
        void Container(uint32_t count, bool map)
        {
            luaL_checkstack(m_Lua, 4, "messagepack.decode: Lua stack limit");
            lua_createtable(m_Lua, map ? 0 : (int)count, map ? (int)count : 0);
        }
        bool Key()
        {
            if (lua_isnil(m_Lua, -1))
            {
                return false;
            }
            if (lua_type(m_Lua, -1) != LUA_TNUMBER)
            {
                return true;
            }
            double number = lua_tonumber(m_Lua, -1);
            return number == number;
        }
        void Set(uint32_t index, bool map)
        {
            if (map)
            {
                lua_rawset(m_Lua, -3);
            }
            else
            {
                lua_rawseti(m_Lua, -2, (int)index + 1);
            }
        }
    };

    /*# Decode one complete MessagePack value. The input is borrowed only for
     * this call; returned strings and tables are owned by Lua. Nil defaults
     * to Lua nil. Set decode_null_as_userdata to retain nulls in containers.
     * @name messagepack.decode
     * @param bytes [type:string] MessagePack bytes, including embedded NUL
     * @param [options] [type:table] Optional decode_null_as_userdata boolean
     * @return value [type:any] Decoded value; raises a Lua error on failure
     */
    static int Decode(lua_State* L)
    {
        size_t length;
        const char* bytes = luaL_checklstring(L, 1, &length);
        DecodeContext consumer = { L, false };
        if (!lua_isnoneornil(L, 2))
        {
            luaL_checktype(L, 2, LUA_TTABLE);
            lua_getfield(L, 2, "decode_null_as_userdata");
            consumer.m_NullAsUserdata = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
        }
        // The reader owns no native allocations, so Lua OOM can unwind directly.
        dmMessagePackDecode::Cursor cursor = { (const uint8_t*)bytes, (const uint8_t*)bytes + length, 0 };
        if (!dmMessagePackDecode::ReadValue(&cursor, &consumer, 0))
        {
            return luaL_error(L, "messagepack.decode: %s", cursor.m_Error);
        }
        if (cursor.m_Position != cursor.m_End)
        {
            return luaL_error(L, "messagepack.decode: trailing data after root value");
        }
        return 1;
    }

    struct EncodeContext
    {
        mpack_writer_t m_Writer;
        const void*    m_Ancestors[64];
        char*          m_Data;
        size_t         m_Size;
        bool           m_Destroyed;
        bool           m_EmptyAsMap;
    };

    static void WriteNumber(mpack_writer_t* writer, lua_Number number)
    {
        // Exclusive upper bound avoids converting a rounded 2^64 to uint64_t.
        // Keep negative zero as a double so its sign survives a round trip.
        if (number >= 0 && number < 18446744073709551616.0 && !(number == 0 && signbit(number)))
        {
            uint64_t integer = (uint64_t)number;
            if ((lua_Number)integer == number)
            {
                mpack_write_u64(writer, integer);
                return;
            }
        }
        else if (number < 0 && number >= -9223372036854775808.0)
        {
            int64_t integer = (int64_t)number;
            if ((lua_Number)integer == number)
            {
                mpack_write_i64(writer, integer);
                return;
            }
        }
        mpack_write_double(writer, number);
    }

    static void WriteValue(lua_State* L, EncodeContext* context, int index, uint32_t depth)
    {
        if (index < 0)
        {
            index += lua_gettop(L) + 1;
        }
        mpack_writer_t* writer = &context->m_Writer;
        switch (lua_type(L, index))
        {
            case LUA_TNIL:
            {
                mpack_write_nil(writer);
                break;
            }
            case LUA_TBOOLEAN:
            {
                mpack_write_bool(writer, lua_toboolean(L, index) != 0);
                break;
            }
            case LUA_TNUMBER:
            {
                WriteNumber(writer, lua_tonumber(L, index));
                break;
            }
            case LUA_TSTRING:
            {
                size_t length;
                const char* bytes = lua_tolstring(L, index, &length);
                if (length > UINT32_MAX)
                {
                    luaL_error(L, "messagepack.encode: string exceeds the MessagePack size limit");
                }
                mpack_write_str(writer, bytes, (uint32_t)length);
                break;
            }
            case LUA_TLIGHTUSERDATA:
            {
                if (lua_touserdata(L, index) != 0)
                {
                    luaL_error(L, "messagepack.encode: unsupported light userdata");
                }
                mpack_write_nil(writer);
                break;
            }
            case LUA_TTABLE:
            {
                if (depth == 64)
                {
                    luaL_error(L, "messagepack.encode: maximum container depth is 64");
                }
                const void* table = lua_topointer(L, index);
                for (uint32_t i = 0; i < depth; ++i)
                {
                    if (context->m_Ancestors[i] == table)
                    {
                        luaL_error(L, "messagepack.encode: cyclic table");
                    }
                }
                context->m_Ancestors[depth] = table;
                luaL_checkstack(L, 4, "messagepack.encode: Lua stack limit");
                uint32_t count = 0;
                uint32_t maximum = 0;
                bool array = true;
                lua_pushnil(L);
                while (lua_next(L, index))
                {
                    if (count == UINT32_MAX)
                    {
                        luaL_error(L, "messagepack.encode: table exceeds the MessagePack size limit");
                    }
                    ++count;
                    lua_Number key = lua_type(L, -2) == LUA_TNUMBER ? lua_tonumber(L, -2) : 0;
                    if (key >= 1 && key <= INT_MAX && (lua_Number)(uint32_t)key == key)
                    {
                        if (key > maximum)
                        {
                            maximum = (uint32_t)key;
                        }
                    }
                    else
                    {
                        array = false;
                    }
                    lua_pop(L, 1);
                }
                array = array && maximum == count && (count != 0 || !context->m_EmptyAsMap);
                if (array)
                {
                    mpack_start_array(writer, count);
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        lua_rawgeti(L, index, (int)i + 1);
                        WriteValue(L, context, -1, depth + 1);
                        lua_pop(L, 1);
                    }
                    mpack_finish_array(writer);
                }
                else
                {
                    mpack_start_map(writer, count);
                    lua_pushnil(L);
                    while (lua_next(L, index))
                    {
                        WriteValue(L, context, -2, depth + 1);
                        WriteValue(L, context, -1, depth + 1);
                        lua_pop(L, 1);
                    }
                    mpack_finish_map(writer);
                }
                break;
            }
            default:
            {
                luaL_error(L, "messagepack.encode: unsupported type %s", luaL_typename(L, index));
            }
        }
        if (mpack_writer_error(writer) != mpack_ok)
        {
            luaL_error(L, "messagepack.encode: %s", mpack_error_to_string(mpack_writer_error(writer)));
        }
    }

    static int EncodeProtected(lua_State* L)
    {
        EncodeContext* context = (EncodeContext*)lua_touserdata(L, 1);
        WriteValue(L, context, 2, 0);
        mpack_error_t error = mpack_writer_destroy(&context->m_Writer);
        context->m_Destroyed = true;
        if (error != mpack_ok)
        {
            return luaL_error(L, "messagepack.encode: %s", mpack_error_to_string(error));
        }
        // The outer protected call retains ownership until Lua copies the
        // bytes, so a Lua allocation error here cannot leak the output buffer.
        lua_pushlstring(L, context->m_Data, context->m_Size);
        return 1;
    }

    /*# Encode a Lua value into one MessagePack string. Dense sequences become
     * arrays; other tables become maps. Empty tables default to maps. Numbers
     * use the smallest integer representation when exact, otherwise float64.
     * @name messagepack.encode
     * @param value [type:any] Value to encode, including messagepack.null
     * @param [options] [type:table] Optional encode_empty_table_as_map boolean
     * @return bytes [type:string] Owned MessagePack bytes; raises on failure
     */
    static int Encode(lua_State* L)
    {
        luaL_checkany(L, 1);
        bool empty_as_map = true;
        if (!lua_isnoneornil(L, 2))
        {
            luaL_checktype(L, 2, LUA_TTABLE);
            lua_getfield(L, 2, "encode_empty_table_as_map");
            if (!lua_isnil(L, -1))
            {
                empty_as_map = lua_toboolean(L, -1) != 0;
            }
            lua_pop(L, 1);
        }
        luaL_checkstack(L, 3, "messagepack.encode: Lua stack limit");
        lua_pushcfunction(L, EncodeProtected);
        EncodeContext context;
        context.m_Destroyed = false;
        context.m_EmptyAsMap = empty_as_map;
        mpack_writer_init_growable(&context.m_Writer, &context.m_Data, &context.m_Size);
        lua_pushlightuserdata(L, &context);
        lua_pushvalue(L, 1);
        int status = lua_pcall(L, 2, 1, 0);
        if (!context.m_Destroyed)
        {
            mpack_writer_flag_error(&context.m_Writer, mpack_error_data);
            mpack_writer_destroy(&context.m_Writer);
        }
        MPACK_FREE(context.m_Data);
        if (status)
        {
            return lua_error(L);
        }
        return 1;
    }

    void Register(lua_State* L)
    {
        static const luaL_reg methods[] =
        {
            { "decode", Decode },
            { "encode", Encode },
            { 0, 0 }
        };
        luaL_register(L, "messagepack", methods);
        lua_pushlightuserdata(L, 0);
        lua_setfield(L, -2, "null");
        lua_pop(L, 1);
    }
}
