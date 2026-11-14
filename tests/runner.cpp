// Copyright 2020-2026 The Defold Foundation
// Copyright 2014-2020 King
// Copyright 2009-2014 Ragnar Svensson, Christian Murray
// SPDX-License-Identifier: MIT

#include "messagepack_lua.h"
#include "reference_decoder.h"
#include "allocations.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern "C"
{
#include <dmsdk/lua/lauxlib.h>
#include <dmsdk/lua/lualib.h>
}

static void Require(bool condition, const char* message)
{
    if (!condition)
    {
        fprintf(stderr, "%s\n", message);
        exit(2);
    }
}

static void Call(lua_State* L, int arguments, int results)
{
    if (lua_pcall(L, arguments, results, 0))
    {
        fprintf(stderr, "Lua error: %s\n", lua_tostring(L, -1));
        exit(3);
    }
}

static lua_State* CreateLua()
{
    lua_State* L = luaL_newstate();
    Require(L != 0, "Cannot create Lua state");
    luaL_openlibs(L);
    dmMessagePack::Register(L);
    // Defold's json.null is the same null light-userdata sentinel. Only this
    // value is needed by codec tests; no JSON implementation is substituted.
    lua_newtable(L);
    lua_pushlightuserdata(L, 0);
    lua_setfield(L, -2, "null");
    lua_setglobal(L, "json");
    lua_pushcfunction(L, dmMessagePackTest::DecodeReference);
    lua_setglobal(L, "reference_decode");
    return L;
}

#include "allocator_tests.h"

static int HexDigit(char value)
{
    if (value >= '0' && value <= '9')
    {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f')
    {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F')
    {
        return value - 'A' + 10;
    }
    return -1;
}

// Test transport only: one hex packet per line, allowing an independent
// implementation to inspect our actual re-encoded bytes in one process.
static int Interoperability(lua_State* L)
{
    const size_t capacity = 262144;
    char* line = (char*)malloc(capacity);
    unsigned char* bytes = (unsigned char*)malloc(capacity / 2);
    Require(line && bytes, "Cannot allocate interoperability transport");
    while (fgets(line, (int)capacity, stdin))
    {
        size_t length = strcspn(line, "\r\n");
        Require(length < capacity - 1, "Interoperability test packet exceeds transport limit");
        Require(length % 2 == 0, "Odd-length test packet");
        for (size_t i = 0; i < length; i += 2)
        {
            int high = HexDigit(line[i]), low = HexDigit(line[i + 1]);
            Require(high >= 0 && low >= 0, "Invalid test packet hex");
            bytes[i / 2] = (unsigned char)(high * 16 + low);
        }
        lua_getglobal(L, "messagepack");
        lua_getfield(L, -1, "decode");
        lua_remove(L, -2);
        lua_pushlstring(L, (const char*)bytes, length / 2);
        lua_newtable(L);
        lua_pushboolean(L, 1);
        lua_setfield(L, -2, "decode_null_as_userdata");
        int status = lua_pcall(L, 2, 1, 0);
        if (!status)
        {
            lua_getglobal(L, "messagepack");
            lua_getfield(L, -1, "encode");
            lua_remove(L, -2);
            lua_insert(L, -2);
            status = lua_pcall(L, 1, 1, 0);
        }
        if (status)
        {
            puts("ERR");
        }
        else
        {
            const char* result = lua_tolstring(L, -1, &length);
            const char hex[] = "0123456789abcdef";
            for (size_t i = 0; i < length; ++i)
            {
                unsigned char byte = (unsigned char)result[i];
                putchar(hex[byte >> 4]);
                putchar(hex[byte & 15]);
            }
            putchar('\n');
        }
        lua_settop(L, 0);
        lua_gc(L, LUA_GCCOLLECT, 0);
        Require(TestNativeAllocationCount() == 0, "Interoperability operation leaked native memory");
    }
    Require(!ferror(stdin), "Cannot read test packets");
    free(bytes);
    free(line);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc == 2 && !strcmp(argv[1], "--decode-oom"))
    {
        return TestDecodeOutOfMemory();
    }
    if (argc == 2 && !strcmp(argv[1], "--encode-oom"))
    {
        return TestEncodeOutOfMemory();
    }
    lua_State* L = CreateLua();
    if (argc == 2 && !strcmp(argv[1], "--interop"))
    {
        Interoperability(L);
    }
    else if (argc == 2 && !strcmp(argv[1], "--api"))
    {
        lua_getglobal(L, "messagepack");
        lua_pushnil(L);
        while (lua_next(L, -2))
        {
            printf("%s %s\n", lua_tostring(L, -2), luaL_typename(L, -1));
            lua_pop(L, 1);
        }
    }
    else if (argc == 3 && (!strcmp(argv[1], "--test") || !strcmp(argv[1], "--reference-test") || !strcmp(argv[1], "--check-lua")))
    {
        if (!strcmp(argv[1], "--reference-test"))
        {
            lua_getglobal(L, "messagepack");
            lua_pushcfunction(L, dmMessagePackTest::DecodeReference);
            lua_setfield(L, -2, "decode");
            lua_pop(L, 1);
        }
        int status = luaL_loadfile(L, argv[2]);
        Require(status == 0, status ? lua_tostring(L, -1) : "Cannot load test");
        if (strcmp(argv[1], "--check-lua"))
        {
            Call(L, 0, 1);
            Call(L, 0, 1);
            printf("Codec tests passed: %d\n", (int)lua_tointeger(L, -1));
        }
    }
    else
    {
        fputs("Usage: messagepack_unit_tests --test FILE | --reference-test FILE | --check-lua FILE | --decode-oom | --encode-oom | --interop | --api\n", stderr);
        lua_close(L);
        return 1;
    }
    lua_close(L);
    Require(TestNativeAllocationCount() == 0, "Unit test leaked native allocations");
    return 0;
}
