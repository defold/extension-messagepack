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
