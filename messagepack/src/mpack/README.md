MPack 1.1.1 by Nicholas Fraser and contributors, MIT licensed.

Source: https://github.com/ludocode/mpack/releases/tag/v1.1.1
Archive: https://github.com/ludocode/mpack/releases/download/v1.1.1/mpack-amalgamation-1.1.1.tar.gz

The amalgamated `mpack.c` and `mpack.h` are unchanged. The header is installed
under `messagepack/include/messagepack/mpack.h`; the full upstream license is
preserved here as `LICENSE`. The first-party `mpack-config.h` enables the reader
writer and native tree parser, and disables the unused builder, expect API and
stdio. Read/write tracking is disabled; container lengths come from checked
input or the Lua table traversal.
