#pragma once

/// @file Export.hpp
/// @brief Export/import definitions for NGIN.Core shared builds.

#include <NGIN/Defines.hpp>

#if defined(NGIN_CORE_STATIC)
#  define NGIN_CORE_API
#elif defined(_WIN32)
#  if defined(NGIN_CORE_SHARED_BUILD)
#    define NGIN_CORE_API __declspec(dllexport)
#  else
#    define NGIN_CORE_API __declspec(dllimport)
#  endif
#else
#  define NGIN_CORE_API __attribute__((visibility("default")))
#endif

