// Copyright 2020-2026 The Defold Foundation
// Copyright 2014-2020 King
// Copyright 2009-2014 Ragnar Svensson, Christian Murray
// SPDX-License-Identifier: MIT

#ifndef DM_MESSAGEPACK_DECODE_H
#define DM_MESSAGEPACK_DECODE_H
#include <messagepack/mpack.h>
#include <limits.h>
#include <string.h>

// The reader borrows input and emits values directly to its consumer. It owns
// no heap memory. Container counts are checked before the consumer allocates.
namespace dmMessagePackDecode
{
    struct Cursor
    {
        const uint8_t* m_Position;
        const uint8_t* m_End;
        const char*    m_Error;
    };

    static bool Fail(Cursor* cursor, const char* message)
    {
        cursor->m_Error = message;
        return false;
    }

    static bool Need(Cursor* cursor, size_t count)
    {
        return count <= (size_t)(cursor->m_End - cursor->m_Position) || Fail(cursor, "truncated input");
    }

    // Read a checked-width unsigned value without requiring aligned input.
    static uint64_t Unsigned(Cursor* cursor, uint32_t width)
    {
        const uint8_t* bytes = cursor->m_Position;
        cursor->m_Position += width;
        switch (width)
        {
            case 1:
            {
                return bytes[0];
            }
            case 2:
            {
                return mpack_load_u16((const char*)bytes);
            }
            case 4:
            {
                return mpack_load_u32((const char*)bytes);
            }
            default:
            {
                return mpack_load_u64((const char*)bytes);
            }
        }
    }

    template <typename Consumer>
    static bool ReadValue(Cursor* cursor, Consumer* consumer, uint32_t depth)
    {
        if (!Need(cursor, 1))
        {
            return false;
        }
        uint8_t tag = *cursor->m_Position++;
        if (tag <= 0x7f)
        {
            consumer->Number(tag);
            return true;
        }
        if (tag >= 0xe0)
        {
            consumer->Number((int)tag - 256);
            return true;
        }
        uint32_t count = 0;
        bool map = false;
        uint32_t length = 0;
        if ((tag & 0xe0) == 0xa0)
        {
            length = tag & 31;
        }
        else if ((tag & 0xf0) == 0x90)
        {
            count = tag & 15;
            goto container;
        }
        else if ((tag & 0xf0) == 0x80)
        {
            count = tag & 15;
            map = true;
            goto container;
        }
        else
        {
            switch (tag)
            {
                case 0xc0:
                {
                    consumer->Null();
                    return true;
                }
                case 0xc2:
                case 0xc3:
                {
                    consumer->Boolean(tag == 0xc3);
                    return true;
                }
                case 0xc4:
                case 0xc5:
                case 0xc6:
                case 0xd9:
                case 0xda:
                case 0xdb:
                {
                    uint32_t width = (tag == 0xc4 || tag == 0xd9) ? 1 : (tag == 0xc5 || tag == 0xda) ? 2 : 4;
                    if (!Need(cursor, width))
                    {
                        return false;
                    }
                    length = (uint32_t)Unsigned(cursor, width);
                    break;
                }
                case 0xca:
                case 0xcb:
                {
                    uint32_t width = tag == 0xca ? 4 : 8;
                    if (!Need(cursor, width))
                    {
                        return false;
                    }
                    uint64_t bits = Unsigned(cursor, width);
                    if (width == 4)
                    {
                        uint32_t narrow = (uint32_t)bits;
                        float number;
                        memcpy(&number, &narrow, 4);
                        consumer->Number(number);
                    }
                    else
                    {
                        double number;
                        memcpy(&number, &bits, 8);
                        consumer->Number(number);
                    }
                    return true;
                }
                case 0xcc:
                case 0xcd:
                case 0xce:
                case 0xcf:
                case 0xd0:
                case 0xd1:
                case 0xd2:
                case 0xd3:
                {
                    uint32_t width = 1u << ((tag - 0xcc) & 3);
                    if (!Need(cursor, width))
                    {
                        return false;
                    }
                    uint64_t bits = Unsigned(cursor, width);
                    double number;
                    if (tag >= 0xd0 && (bits & (UINT64_C(1) << (width * 8 - 1))))
                    {
                        uint64_t magnitude = width == 8 ? ~bits + 1 : (UINT64_C(1) << (width * 8)) - bits;
                        number = -(double)magnitude;
                    }
                    else
                    {
                        number = (double)bits;
                    }
                    consumer->Number(number);
                    return true;
                }
                case 0xdc:
                case 0xdd:
                case 0xde:
                case 0xdf:
                {
                    uint32_t width = (tag & 1) ? 4 : 2;
                    if (!Need(cursor, width))
                    {
                        return false;
                    }
                    count = (uint32_t)Unsigned(cursor, width);
                    map = tag >= 0xde;
                    goto container;
                }
                case 0xc1:
                {
                    return Fail(cursor, "invalid tag");
                }
                default:
                {
                    // fixext 1/2/4/8/16 (0xd4..0xd8) or ext 8/16/32 (0xc7..0xc9)
                    if (tag >= 0xd4)
                    {
                        length = 1u << (tag - 0xd4);
                    }
                    else
                    {
                        uint32_t width = 1u << (tag - 0xc7);
                        if (!Need(cursor, width))
                        {
                            return false;
                        }
                        length = (uint32_t)Unsigned(cursor, width);
                    }
                    // Checked separately: 1 + length can wrap a 32-bit size_t.
                    if (!Need(cursor, 1))
                    {
                        return false;
                    }
                    uint8_t type = *cursor->m_Position++;
                    if (!Need(cursor, length))
                    {
                        return false;
                    }
                    const char* data = (const char*)cursor->m_Position;
                    cursor->m_Position += length;
                    return consumer->Ext(type < 0x80 ? (int)type : (int)type - 256, data, length) || Fail(cursor, "unsupported extension type");
                }
            }
        }
        if (!Need(cursor, length))
        {
            return false;
        }
        consumer->String((const char*)cursor->m_Position, length);
        cursor->m_Position += length;
        return true;

    container:
        if (depth == 64)
        {
            return Fail(cursor, "maximum container depth is 64");
        }
        if (count > INT_MAX || count > (size_t)(cursor->m_End - cursor->m_Position) / (map ? 2 : 1))
        {
            return Fail(cursor, "invalid container length");
        }
        consumer->Container(count, map);
        if (map)
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                if (!Need(cursor, 1))
                {
                    return false;
                }
                uint8_t key_tag = *cursor->m_Position;
                if ((key_tag & 0xe0) == 0xa0)
                {
                    ++cursor->m_Position;
                    uint32_t key_length = key_tag & 31;
                    if (!Need(cursor, key_length))
                    {
                        return false;
                    }
                    consumer->String((const char*)cursor->m_Position, key_length);
                    cursor->m_Position += key_length;
                }
                else
                {
                    if (!ReadValue(cursor, consumer, depth + 1))
                    {
                        return false;
                    }
                    if (!consumer->Key())
                    {
                        return Fail(cursor, "map key is nil or NaN");
                    }
                }
                if (!ReadValue(cursor, consumer, depth + 1))
                {
                    return false;
                }
                consumer->Set(i, true);
            }
        }
        else
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                // Numeric arrays can avoid recursive dispatch for float64 values.
                if (cursor->m_End - cursor->m_Position >= 9 && cursor->m_Position[0] == 0xcb)
                {
                    uint64_t bits = mpack_load_u64((const char*)cursor->m_Position + 1);
                    double value;
                    memcpy(&value, &bits, sizeof(value));
                    cursor->m_Position += 9;
                    consumer->Number(value);
                }
                else if (!ReadValue(cursor, consumer, depth + 1))
                {
                    return false;
                }
                consumer->Set(i, false);
            }
        }
        return true;
    }

}
#endif
