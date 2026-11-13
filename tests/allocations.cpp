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
