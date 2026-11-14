// Copyright 2020-2026 The Defold Foundation
// Copyright 2014-2020 King
// Copyright 2009-2014 Ragnar Svensson, Christian Murray
// SPDX-License-Identifier: MIT

#include <dmsdk/extension/extension.h>
#include "messagepack_lua.h"

static ExtensionResult Initialize(ExtensionParams* params)
{
    dmMessagePack::Register(params->m_L);
    return EXTENSION_RESULT_OK;
}

DM_DECLARE_EXTENSION(MessagePack, "MessagePack", 0, 0, Initialize, 0, 0, 0)
