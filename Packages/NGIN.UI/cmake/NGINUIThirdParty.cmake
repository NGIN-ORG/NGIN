include(FetchContent)

function(_ngin_ui_fetch_source name url sha256)
    FetchContent_Declare(
        ${name}
        URL "${url}"
        URL_HASH "SHA256=${sha256}"
        DOWNLOAD_EXTRACT_TIMESTAMP OFF
    )
    FetchContent_GetProperties(${name})
    if(NOT ${name}_POPULATED)
        cmake_policy(PUSH)
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(${name})
        cmake_policy(POP)
        # The pinned HarfBuzz CMake entry point unconditionally warns that its
        # CMake support is community-maintained. This integration deliberately
        # uses that entry point, so keep the known warning local.
        if(name STREQUAL "ngin_ui_harfbuzz")
            set(CMAKE_MESSAGE_LOG_LEVEL ERROR)
        endif()
        add_subdirectory(
            "${${name}_SOURCE_DIR}"
            "${${name}_BINARY_DIR}"
            EXCLUDE_FROM_ALL
        )
    endif()
endfunction()

function(ngin_ui_configure_native_text target)
    if(NGIN_UI_FETCH_THIRD_PARTY)
        set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
        set(FT_ENABLE_ERROR_STRINGS ON CACHE BOOL "" FORCE)
        _ngin_ui_fetch_source(
            ngin_ui_freetype
            "https://github.com/freetype/freetype/archive/0a0221a1347e2f1e07c395263540026e9a0aa7c7.tar.gz"
            "11CD478953FC1D382F20A233B8E2AED6C31B47CBF0568C4BDB3334BCC1550698"
        )

        set(HB_HAVE_FREETYPE ON CACHE BOOL "" FORCE)
        set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
        set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
        set(HB_BUILD_RASTER OFF CACHE BOOL "" FORCE)
        set(HB_BUILD_VECTOR OFF CACHE BOOL "" FORCE)
        set(HB_BUILD_GPU OFF CACHE BOOL "" FORCE)
        _ngin_ui_fetch_source(
            ngin_ui_harfbuzz
            "https://github.com/harfbuzz/harfbuzz/archive/56feae4035bdd48f62ba2b8d8c16232d4d89b3a4.tar.gz"
            "FF66AEA9CFC2BF07819C2352FEC4F2B4859257D33CA65E616DEDB04346EC727B"
        )

        if(WIN32 AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
            target_compile_definitions(
                harfbuzz
                PRIVATE
                    _CRT_SECURE_NO_DEPRECATE
                    _CRT_NONSTDC_NO_DEPRECATE
                    _CRT_SECURE_NO_WARNINGS
            )
        endif()

        set(_ngin_ui_freetype_target freetype)
        set(_ngin_ui_harfbuzz_target harfbuzz)
    else()
        find_package(Freetype 2.14 REQUIRED)
        find_package(harfbuzz 14 CONFIG REQUIRED)
        set(_ngin_ui_freetype_target Freetype::Freetype)
        if(TARGET harfbuzz::harfbuzz)
            set(_ngin_ui_harfbuzz_target harfbuzz::harfbuzz)
        elseif(TARGET harfbuzz)
            set(_ngin_ui_harfbuzz_target harfbuzz)
        else()
            message(FATAL_ERROR "The HarfBuzz package does not export a supported target")
        endif()
    endif()

    target_link_libraries(
        ${target}
        PRIVATE
            ${_ngin_ui_freetype_target}
            ${_ngin_ui_harfbuzz_target}
    )
    target_compile_definitions(
        ${target}
        PRIVATE
            NGIN_UI_HAS_NATIVE_TEXT=1
            NGIN_UI_BUNDLED_FONT_PATH="${NGIN_UI_BUNDLED_FONT_PATH}"
            NGIN_UI_BUNDLED_ARABIC_FONT_PATH="${NGIN_UI_BUNDLED_ARABIC_FONT_PATH}"
            NGIN_UI_BUNDLED_SYMBOLS_FONT_PATH="${NGIN_UI_BUNDLED_SYMBOLS_FONT_PATH}"
    )
endfunction()

function(ngin_ui_configure_standard_images target)
    if(NGIN_UI_FETCH_THIRD_PARTY)
        FetchContent_Declare(
            ngin_ui_stb
            URL
                "https://github.com/nothings/stb/archive/013ac3beddff3dbffafd5177e7972067cd2b5083.tar.gz"
            URL_HASH
                "SHA256=B01AA93E1A968AED55F43E072C98EE401D2F20E897AABDB1A166C7166886ED11"
            DOWNLOAD_EXTRACT_TIMESTAMP OFF
        )
        FetchContent_GetProperties(ngin_ui_stb)
        if(NOT ngin_ui_stb_POPULATED)
            cmake_policy(PUSH)
            if(POLICY CMP0169)
                cmake_policy(SET CMP0169 OLD)
            endif()
            FetchContent_Populate(ngin_ui_stb)
            cmake_policy(POP)
        endif()
        set(_ngin_ui_stb_include "${ngin_ui_stb_SOURCE_DIR}")
    else()
        find_path(_ngin_ui_stb_include stb_image.h REQUIRED)
    endif()

    target_include_directories(${target} SYSTEM PRIVATE "${_ngin_ui_stb_include}")
    target_compile_definitions(
        ${target}
        PRIVATE NGIN_UI_HAS_STANDARD_IMAGE_FORMATS=1
    )
endfunction()
