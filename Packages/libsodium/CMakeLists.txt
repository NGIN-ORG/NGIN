if(TARGET libsodium::libsodium)
  return()
endif()

find_package(unofficial-sodium CONFIG QUIET)
if(TARGET unofficial-sodium::sodium)
  add_library(libsodium::libsodium INTERFACE IMPORTED)
  set_property(TARGET libsodium::libsodium PROPERTY INTERFACE_LINK_LIBRARIES unofficial-sodium::sodium)
  return()
endif()

find_package(sodium CONFIG QUIET)
if(TARGET sodium::sodium)
  add_library(libsodium::libsodium INTERFACE IMPORTED)
  set_property(TARGET libsodium::libsodium PROPERTY INTERFACE_LINK_LIBRARIES sodium::sodium)
  return()
endif()
if(TARGET Sodium::Sodium)
  add_library(libsodium::libsodium INTERFACE IMPORTED)
  set_property(TARGET libsodium::libsodium PROPERTY INTERFACE_LINK_LIBRARIES Sodium::Sodium)
  return()
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(LIBSODIUM QUIET IMPORTED_TARGET libsodium)
  if(TARGET PkgConfig::LIBSODIUM)
    set(_libsodium_pkgconfig_has_header OFF)
    foreach(_libsodium_pkgconfig_include IN LISTS LIBSODIUM_INCLUDE_DIRS)
      if(EXISTS "${_libsodium_pkgconfig_include}/sodium.h")
        set(_libsodium_pkgconfig_has_header ON)
      endif()
    endforeach()
    if(_libsodium_pkgconfig_has_header)
      add_library(libsodium::libsodium INTERFACE IMPORTED)
      set_property(TARGET libsodium::libsodium PROPERTY INTERFACE_LINK_LIBRARIES PkgConfig::LIBSODIUM)
      return()
    endif()
  endif()
endif()

find_path(LIBSODIUM_INCLUDE_DIR sodium.h)
find_library(LIBSODIUM_LIBRARY NAMES sodium libsodium)
if(LIBSODIUM_INCLUDE_DIR AND LIBSODIUM_LIBRARY)
  add_library(libsodium::libsodium UNKNOWN IMPORTED)
  set_target_properties(
    libsodium::libsodium
    PROPERTIES
      IMPORTED_LOCATION "${LIBSODIUM_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${LIBSODIUM_INCLUDE_DIR}")
  return()
endif()

message(
  FATAL_ERROR
    "Packages/libsodium requires a libsodium CMake package, pkg-config module, or discoverable sodium library/header")
