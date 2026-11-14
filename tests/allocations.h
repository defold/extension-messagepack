// Copyright 2020-2026 The Defold Foundation
// Copyright 2014-2020 King
// Copyright 2009-2014 Ragnar Svensson, Christian Murray
// SPDX-License-Identifier: MIT

#ifndef MESSAGEPACK_TEST_ALLOCATIONS_H
#define MESSAGEPACK_TEST_ALLOCATIONS_H
#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
void*   TestMalloc(size_t size);
void*   TestRealloc(void* pointer, size_t size);
void    TestFree(void* pointer);
int64_t TestNativeAllocationCount(void);
void    TestFailNativeAfter(int64_t requests);
#ifdef __cplusplus
}
#endif
#endif
