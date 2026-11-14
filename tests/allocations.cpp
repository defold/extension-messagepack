// Copyright 2020-2026 The Defold Foundation
// Copyright 2014-2020 King
// Copyright 2009-2014 Ragnar Svensson, Christian Murray
// SPDX-License-Identifier: MIT

#include "allocations.h"

// Test-only hooks for native allocation failures and leak assertions.
static int64_t g_LiveAllocations;
static int64_t g_FailAfter = -1;

static bool FailAllocation()
{
    if (g_FailAfter < 0)
    {
        return false;
    }
    if (g_FailAfter == 0)
    {
        return true;
    }
    --g_FailAfter;
    return false;
}

extern "C" void* TestMalloc(size_t size)
{
    if (FailAllocation())
    {
        return 0;
    }
    void* pointer = malloc(size);
    if (pointer)
    {
        ++g_LiveAllocations;
    }
    return pointer;
}

extern "C" void TestFree(void* pointer)
{
    if (!pointer)
    {
        return;
    }
    --g_LiveAllocations;
    free(pointer);
}

extern "C" void* TestRealloc(void* pointer, size_t size)
{
    if (!pointer)
    {
        return TestMalloc(size);
    }
    if (!size)
    {
        TestFree(pointer);
        return 0;
    }
    if (FailAllocation())
    {
        return 0;
    }
    return realloc(pointer, size);
}

extern "C" int64_t TestNativeAllocationCount()
{
    return g_LiveAllocations;
}

extern "C" void TestFailNativeAfter(int64_t requests)
{
    g_FailAfter = requests;
}
