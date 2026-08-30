cmake_minimum_required(VERSION 3.18.1)

set(CMAKE_BUILD_TYPE Release)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -flto -fuse-linker-plugin -finline-functions -fomit-frame-pointer -falign-functions -falign-jumps")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -flto -fuse-linker-plugin -finline-functions -fomit-frame-pointer -falign-functions -falign-jumps")
endif()

project("imgui")

add_library(ktx_read SHARED IMPORTED)
set_target_properties(ktx_read PROPERTIES IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/Utils/ktx/libktx_read.so)

file(GLOB CPP_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/**/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/**/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/Utils/**/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Utils/**/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/Utils/KittyMemory/KittyMemory/*.cpp
)

file(GLOB_RECURSE LUA_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Lua/lua_src/*.c
)

add_library(
        ciphered
        SHARED
        main.cpp
        ${CPP_FILES}
        ${LUA_SOURCES}
        scroll.cpp
        Core/imgui/backends/imgui_impl_opengl3.cpp
        Utils/imgui_androidbk/androidbk.cpp
        Core/starwatch/starwatch_block.cpp
)

target_include_directories(
        ciphered PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Utils
        ${CMAKE_CURRENT_SOURCE_DIR}/Utils/KittyMemory
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/Core
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Lua
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Lua/lua_src
)

target_compile_options(ciphered PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-Werror -std=c++17 -Wno-error=c++11-narrowing  -Wall>)
target_compile_options(ciphered PUBLIC -w -s -Wno-error=format-security -fpermissive -fexceptions)

if (NOT kNO_KEYSTONE)
        set(keystone ${CMAKE_CURRENT_SOURCE_DIR}/Utils/KittyMemory/KittyMemory/Deps/Keystone/libs-android/arm64-v8a/libkeystone.a)
        target_link_libraries(ciphered ${keystone})
endif()

find_package(shadowhook REQUIRED CONFIG)

target_link_libraries(
        ciphered
        ktx_read
        log
        GLESv3
        EGL
        android
        z
        shadowhook::shadowhook
)

add_library(
        lualoader
        SHARED
        LuaLoader/LuaLoaderMain.cpp
        Core/Lua/LuaManager.cpp
        Core/Lua/LuaMemoryScanner.cpp
        Core/Lua/LuaUIBridge.cpp
        Core/Lua/GGLib.cpp
        Core/Lua/CanvasLuaExt.cpp
        ${LUA_SOURCES}
)

target_include_directories(
        lualoader PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Utils
        ${CMAKE_CURRENT_SOURCE_DIR}/Utils/KittyMemory
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/Core
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Lua
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/Lua/lua_src
)

target_compile_options(lualoader PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-std=c++17 -Wall>)
target_compile_options(lualoader PUBLIC -w -s -Wno-error=format-security -fpermissive -fexceptions)

target_link_libraries(
        lualoader
        ciphered
        log
        GLESv3
        EGL
        android
        z
)