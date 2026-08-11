cmake_minimum_required(VERSION 3.20)
project(NGIN.SDL3.Provider LANGUAGES C)

include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    ngin_sdl3
    URL "https://github.com/libsdl-org/SDL/archive/f87239e71e42da91ca317a12eefb82cfbf3393eb.tar.gz"
    URL_HASH "SHA256=363AB3EB2225D700BF0B67BC16349F9949125C747AC50DA3BA805C2D3A61B2A0"
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
)
FetchContent_GetProperties(ngin_sdl3)
if(NOT ngin_sdl3_POPULATED)
    cmake_policy(PUSH)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(ngin_sdl3)
    cmake_policy(POP)
    add_subdirectory(
        "${ngin_sdl3_SOURCE_DIR}"
        "${ngin_sdl3_BINARY_DIR}"
        EXCLUDE_FROM_ALL
    )
endif()

# SDL applies these definitions for MSVC, but CMake's GNU-like Clang driver
# does not set MSVC even when it targets the Microsoft CRT and ABI.
if(WIN32 AND CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(
        SDL3-static
        PRIVATE
            _CRT_SECURE_NO_DEPRECATE
            _CRT_NONSTDC_NO_DEPRECATE
            _CRT_SECURE_NO_WARNINGS
    )
endif()
