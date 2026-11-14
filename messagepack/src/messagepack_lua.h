// Copyright 2020-2026 The Defold Foundation
// Copyright 2014-2020 King
// Copyright 2009-2014 Ragnar Svensson, Christian Murray
// SPDX-License-Identifier: MIT

#ifndef MESSAGEPACK_LUA_H
#define MESSAGEPACK_LUA_H

extern "C"
{
#include <dmsdk/lua/lua.h>
}

namespace dmMessagePack
{
    void Register(lua_State* L);
}

#endif
