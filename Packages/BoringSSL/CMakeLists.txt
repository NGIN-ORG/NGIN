if(TARGET BoringSSL::Crypto)
  return()
endif()

find_package(BoringSSL CONFIG QUIET)
if(TARGET BoringSSL::Crypto)
  return()
endif()
if(TARGET BoringSSL::crypto)
  add_library(BoringSSL::Crypto INTERFACE IMPORTED)
  set_property(TARGET BoringSSL::Crypto PROPERTY INTERFACE_LINK_LIBRARIES BoringSSL::crypto)
  return()
endif()
if(TARGET crypto)
  add_library(BoringSSL::Crypto INTERFACE IMPORTED)
  set_property(TARGET BoringSSL::Crypto PROPERTY INTERFACE_LINK_LIBRARIES crypto)
  return()
endif()

find_path(BORINGSSL_INCLUDE_DIR openssl/base.h)
set(_boringssl_header_is_boringssl OFF)
if(BORINGSSL_INCLUDE_DIR)
  file(STRINGS
    "${BORINGSSL_INCLUDE_DIR}/openssl/base.h"
    _boringssl_marker
    REGEX "OPENSSL_IS_BORINGSSL")
  if(_boringssl_marker)
    set(_boringssl_header_is_boringssl ON)
  endif()
endif()

find_library(BORINGSSL_CRYPTO_LIBRARY NAMES crypto libcrypto)
if(BORINGSSL_INCLUDE_DIR AND BORINGSSL_CRYPTO_LIBRARY AND _boringssl_header_is_boringssl)
  add_library(BoringSSL::Crypto UNKNOWN IMPORTED)
  set_target_properties(
    BoringSSL::Crypto
    PROPERTIES
      IMPORTED_LOCATION "${BORINGSSL_CRYPTO_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${BORINGSSL_INCLUDE_DIR}")
  return()
endif()

if(BORINGSSL_INCLUDE_DIR AND BORINGSSL_CRYPTO_LIBRARY)
  message(FATAL_ERROR
    "Packages/BoringSSL found openssl/base.h and libcrypto, but the header does not define OPENSSL_IS_BORINGSSL. "
    "Refusing to expose a non-BoringSSL OpenSSL-compatible install as BoringSSL.")
endif()

message(
  FATAL_ERROR
    "Packages/BoringSSL requires a BoringSSL CMake package or discoverable crypto library/header")
