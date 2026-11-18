cmake_minimum_required(VERSION 3.24)
project(messagepack_unit_tests LANGUAGES C CXX)

option(MESSAGEPACK_SANITIZE "Instrument the codec and test harness with ASan/UBSan" OFF)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
get_filename_component(EXTENSION_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# Build a pinned stock Lua 5.1 VM. SDK-path adapters below include these real
# public Lua headers; production codec sources are compiled without changes.
include(FetchContent)
FetchContent_Declare(lua51
    URL https://www.lua.org/ftp/lua-5.1.5.tar.gz
    URL_HASH SHA256=2640fc56a795f29d28ef15e13c34a47e223960b0240e8cb0a82d9b0738695333
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(lua51)
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "${lua51_SOURCE_DIR}/src/*.c")
list(REMOVE_ITEM LUA_SOURCES "${lua51_SOURCE_DIR}/src/lua.c" "${lua51_SOURCE_DIR}/src/luac.c" "${lua51_SOURCE_DIR}/src/print.c")
add_library(lua51 STATIC ${LUA_SOURCES})
target_include_directories(lua51 PUBLIC "${lua51_SOURCE_DIR}/src")
target_compile_definitions(lua51 PRIVATE LUA_USE_APICHECK=1)
if(UNIX)
    target_compile_definitions(lua51 PRIVATE LUA_USE_POSIX=1)
    target_link_libraries(lua51 PUBLIC m ${CMAKE_DL_LIBS})
endif()

add_library(messagepack_codec STATIC
    "${EXTENSION_ROOT}/messagepack/src/messagepack_lua.cpp"
    "${EXTENSION_ROOT}/messagepack/src/mpack/mpack.c")
target_include_directories(messagepack_codec PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/support"
    "${EXTENSION_ROOT}/messagepack/include"
    "${EXTENSION_ROOT}/messagepack/src"
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(messagepack_codec PUBLIC lua51)
target_compile_definitions(messagepack_codec PUBLIC MPACK_HAS_CONFIG=1
    PRIVATE MPACK_MALLOC=TestMalloc MPACK_REALLOC=TestRealloc MPACK_FREE=TestFree)
target_compile_options(messagepack_codec PRIVATE "-include${CMAKE_CURRENT_SOURCE_DIR}/allocations.h")

add_executable(messagepack_unit_tests runner.cpp allocations.cpp)
target_link_libraries(messagepack_unit_tests PRIVATE messagepack_codec)
foreach(target messagepack_codec messagepack_unit_tests)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions> $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
    if(MESSAGEPACK_SANITIZE)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PUBLIC -fsanitize=address,undefined)
    endif()
endforeach()

enable_testing()
foreach(suite decode encode spec decode_differential)
    add_test(NAME ${suite} COMMAND messagepack_unit_tests --test "${CMAKE_CURRENT_SOURCE_DIR}/test_${suite}.lua")
endforeach()
add_test(NAME decode_oom COMMAND messagepack_unit_tests --decode-oom)
add_test(NAME encode_oom COMMAND messagepack_unit_tests --encode-oom)
add_test(NAME spec_reference COMMAND messagepack_unit_tests --reference-test "${CMAKE_CURRENT_SOURCE_DIR}/test_spec.lua")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME interoperability COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_interop.py" "$<TARGET_FILE:messagepack_unit_tests>")
add_test(NAME script_api COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_script_api.py" "${EXTENSION_ROOT}/messagepack/api/messagepack.script_api" "$<TARGET_FILE:messagepack_unit_tests>")
set_tests_properties(decode encode spec decode_differential decode_oom encode_oom spec_reference interoperability script_api PROPERTIES TIMEOUT 120)
