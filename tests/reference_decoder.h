// Copyright 2020-2026 The Defold Foundation
// Copyright 2014-2020 King
// Copyright 2009-2014 Ragnar Svensson, Christian Murray
// SPDX-License-Identifier: MIT

#include "messagepack_lua.h"
#include <messagepack/mpack.h>
#include <limits.h>
#include <math.h>

extern "C"
{
#include <dmsdk/lua/lauxlib.h>
}

namespace dmMessagePackTest
{
    struct DecodeContext
    {
        mpack_reader_t m_Reader;
        bool           m_NullAsUserdata;
    };

    static void CheckReader(lua_State* L, mpack_reader_t* reader)
    {
        if (mpack_reader_error(reader) != mpack_ok)
        {
            luaL_error(L, "messagepack.decode: %s", mpack_error_to_string(mpack_reader_error(reader)));
        }
    }

    static void PushValue(lua_State* L, DecodeContext* context, uint32_t depth)
    {
        mpack_reader_t* reader = &context->m_Reader;
        mpack_tag_t tag = mpack_read_tag(reader);
        CheckReader(L, reader);
        mpack_type_t type = mpack_tag_type(&tag);
        switch (type)
        {
            case mpack_type_nil:
            {
                if (context->m_NullAsUserdata)
                {
                    lua_pushlightuserdata(L, 0);
                }
                else
                {
                    lua_pushnil(L);
                }
                break;
            }
            case mpack_type_bool:
            {
                lua_pushboolean(L, mpack_tag_bool_value(&tag));
                break;
            }
            case mpack_type_int:
            {
                lua_pushnumber(L, (lua_Number)mpack_tag_int_value(&tag));
                break;
            }
            case mpack_type_uint:
            {
                lua_pushnumber(L, (lua_Number)mpack_tag_uint_value(&tag));
                break;
            }
            case mpack_type_float:
            {
                lua_pushnumber(L, mpack_tag_float_value(&tag));
                break;
            }
            case mpack_type_double:
            {
                lua_pushnumber(L, mpack_tag_double_value(&tag));
                break;
            }
            case mpack_type_str:
            case mpack_type_bin:
            {
                uint32_t length = type == mpack_type_str ? mpack_tag_str_length(&tag) : mpack_tag_bin_length(&tag);
                const char* bytes = mpack_read_bytes_inplace(reader, length);
                CheckReader(L, reader);
                lua_pushlstring(L, bytes, length);
                mpack_done_type(reader, type);
                break;
            }
            case mpack_type_array:
            case mpack_type_map:
            {
                if (depth == 64)
                {
                    luaL_error(L, "messagepack.decode: maximum container depth is 64");
                }
                uint32_t count = type == mpack_type_array ? mpack_tag_array_count(&tag) : mpack_tag_map_count(&tag);
                size_t remaining = mpack_reader_remaining(reader, 0);
                size_t minimum_size = type == mpack_type_array ? 1 : 2;
                if (count > INT_MAX || count > remaining / minimum_size)
                {
                    luaL_error(L, "messagepack.decode: invalid container length");
                }
                luaL_checkstack(L, 4, "messagepack.decode: Lua stack limit");
                lua_createtable(L, type == mpack_type_array ? (int)count : 0, type == mpack_type_map ? (int)count : 0);
                for (uint32_t i = 0; i < count; ++i)
                {
                    if (type == mpack_type_map)
                    {
                        PushValue(L, context, depth + 1);
                        if (lua_isnil(L, -1) || (lua_type(L, -1) == LUA_TNUMBER && lua_tonumber(L, -1) != lua_tonumber(L, -1)))
                        {
                            luaL_error(L, "messagepack.decode: map key is nil or NaN");
                        }
                    }
                    PushValue(L, context, depth + 1);
                    if (type == mpack_type_array)
                    {
                        lua_rawseti(L, -2, (int)i + 1);
                    }
                    else
                    {
                        lua_rawset(L, -3);
                    }
                }
                mpack_done_type(reader, type);
                break;
            }
            default:
            {
                luaL_error(L, "messagepack.decode: extension types are not supported");
            }
        }
    }

    static int DecodeProtected(lua_State* L)
    {
        DecodeContext* context = (DecodeContext*)lua_touserdata(L, 1);
        PushValue(L, context, 0);
        CheckReader(L, &context->m_Reader);
        if (mpack_reader_remaining(&context->m_Reader, 0))
        {
            return luaL_error(L, "messagepack.decode: trailing data after root value");
        }
        return 1;
    }

    // Independent MPack reader oracle for malformed-input/value tests.
    static int DecodeReference(lua_State* L)
    {
        size_t length;
        const char* bytes = luaL_checklstring(L, 1, &length);
        bool null_as_userdata = false;
        if (!lua_isnoneornil(L, 2))
        {
            luaL_checktype(L, 2, LUA_TTABLE);
            lua_getfield(L, 2, "decode_null_as_userdata");
            null_as_userdata = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
        }

        luaL_checkstack(L, 2, "messagepack.decode: Lua stack limit");
        lua_pushcfunction(L, DecodeProtected);
        DecodeContext context;
        context.m_NullAsUserdata = null_as_userdata;
        mpack_reader_init_data(&context.m_Reader, bytes, length);
        lua_pushlightuserdata(L, &context);
        int status = lua_pcall(L, 1, 1, 0);
        mpack_error_t error = mpack_reader_destroy(&context.m_Reader);
        // Release native state before propagating a Lua longjmp, including OOM.
        if (status)
        {
            return lua_error(L);
        }
        if (error != mpack_ok)
        {
            return luaL_error(L, "messagepack.decode: %s", mpack_error_to_string(error));
        }
        return 1;
    }

}
