// Copyright 2020-2026 The Defold Foundation
// Copyright 2014-2020 King
// Copyright 2009-2014 Ragnar Svensson, Christian Murray
// SPDX-License-Identifier: MIT

#ifndef MESSAGEPACK_ALLOCATOR_TESTS_H
#define MESSAGEPACK_ALLOCATOR_TESTS_H

struct FailingLuaAllocator
{
    lua_Alloc m_Original;
    void*     m_Context;
    int       m_Remaining;
};

static void* FailLuaAllocation(void* userdata, void* pointer, size_t old_size, size_t size)
{
    FailingLuaAllocator* allocator = (FailingLuaAllocator*)userdata;
    if (size > (pointer ? old_size : 0))
    {
        if (!allocator->m_Remaining)
        {
            return 0;
        }
        --allocator->m_Remaining;
    }
    return allocator->m_Original(allocator->m_Context, pointer, old_size, size);
}

// Allocation failures at different points must unwind partial decoded values
// without native leaks and leave the Lua state usable for the next call.
static int TestDecodeOutOfMemory()
{
    lua_State* L = CreateLua();
    luaL_checkstack(L, 128, "OOM test stack");
    lua_getglobal(L, "messagepack");
    lua_getfield(L, -1, "decode");
    int decode = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pop(L, 1);
    const char* fixtures[] = {
        "return messagepack.encode(string.rep('x', 50000))",
        "local t={} for i=1,128 do t[i] = {['field'..i]=string.rep(string.char(i), 64)} end return messagepack.encode(t)"
    };
    int points[] = { 0, 1, 2, 3, 4, 8, 16, 32, 64, 128, 256, 512, 1024 };
    int failures = 0, successes = 0;
    for (uint32_t input = 0; input < 2; ++input)
    {
        Require(luaL_loadstring(L, fixtures[input]) == 0, "Cannot prepare decode OOM input");
        Call(L, 0, 1);
        int argument = luaL_ref(L, LUA_REGISTRYINDEX);
        for (uint32_t point = 0; point < sizeof(points) / sizeof(points[0]); ++point)
        {
            lua_gc(L, LUA_GCCOLLECT, 0);
            lua_rawgeti(L, LUA_REGISTRYINDEX, decode);
            lua_rawgeti(L, LUA_REGISTRYINDEX, argument);
            FailingLuaAllocator failing;
            failing.m_Original = lua_getallocf(L, &failing.m_Context);
            failing.m_Remaining = points[point];
            lua_setallocf(L, FailLuaAllocation, &failing);
            int status = lua_pcall(L, 1, 1, 0);
            lua_setallocf(L, failing.m_Original, failing.m_Context);
            failures += status != 0;
            successes += status == 0;
            Require(TestNativeAllocationCount() == 0, "Decoder leaked native memory after allocation failure");
            lua_pop(L, 1);
            lua_gc(L, LUA_GCCOLLECT, 0);
            lua_rawgeti(L, LUA_REGISTRYINDEX, decode);
            lua_pushliteral(L, "*");
            Call(L, 1, 1);
            Require(lua_tonumber(L, -1) == 42, "Decoder unusable after allocation failure");
            lua_pop(L, 1);
        }
        luaL_unref(L, LUA_REGISTRYINDEX, argument);
    }
    Require(failures > 0 && successes > 0, "Decode OOM tests must exercise failures and success");
    lua_close(L);
    printf("Decoding allocation-failure tests passed: 26 trials (%d failures, %d successes)\n", failures, successes);
    return 0;
}

// Verify cleanup when the native writer or the final Lua string cannot allocate.
static int TestEncodeOutOfMemory()
{
    lua_State* L = CreateLua();
    luaL_checkstack(L, 128, "OOM test stack");
    lua_getglobal(L, "messagepack");
    lua_getfield(L, -1, "encode");
    int encode = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pop(L, 1);
    int failures[2] = { 0, 0 };
    char payload[50000];
    memset(payload, 'x', sizeof(payload));
    for (int input = 0; input < 2; ++input)
    {
        lua_pushlstring(L, payload, input ? sizeof(payload) : 32);
        int argument = luaL_ref(L, LUA_REGISTRYINDEX);
        for (int allocator = 0; allocator < 2; ++allocator)
        {
            for (int point = 0; point < 6; ++point)
            {
                lua_gc(L, LUA_GCCOLLECT, 0);
                lua_rawgeti(L, LUA_REGISTRYINDEX, encode);
                lua_rawgeti(L, LUA_REGISTRYINDEX, argument);
                FailingLuaAllocator failing;
                failing.m_Original = lua_getallocf(L, &failing.m_Context);
                failing.m_Remaining = point;
                if (allocator)
                {
                    lua_setallocf(L, FailLuaAllocation, &failing);
                }
                else
                {
                    TestFailNativeAfter(point);
                }
                int status = lua_pcall(L, 1, 1, 0);
                TestFailNativeAfter(-1);
                lua_setallocf(L, failing.m_Original, failing.m_Context);
                failures[allocator] += status != 0;
                Require(TestNativeAllocationCount() == 0, "Encoding leaked native memory after allocation failure");
                lua_pop(L, 1);
            }
        }
        luaL_unref(L, LUA_REGISTRYINDEX, argument);
    }
    Require(failures[0] > 0 && failures[1] > 0, "OOM tests did not exercise both allocators");
    lua_close(L);
    printf("Encoding allocation-failure tests passed: 24 cases (%d native and %d Lua failures)\n", failures[0], failures[1]);
    return 0;
}

#endif
