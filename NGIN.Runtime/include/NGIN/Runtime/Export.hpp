#pragma once

/// @file Export.hpp
/// @brief Export/import definitions for NGIN.Runtime shared builds.

#include <NGIN/Defines.hpp>

#if defined(NGIN_RUNTIME_STATIC)
#  define NGIN_RUNTIME_API
#elif defined(_WIN32)
#  if defined(NGIN_RUNTIME_SHARED_BUILD)
#    define NGIN_RUNTIME_API __declspec(dllexport)
#  else
#    define NGIN_RUNTIME_API __declspec(dllimport)
#  endif
#else
#  define NGIN_RUNTIME_API __attribute__((visibility("default")))
#endif

