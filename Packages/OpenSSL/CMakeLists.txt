if(TARGET OpenSSL::Crypto)
  return()
endif()

find_package(OpenSSL CONFIG QUIET COMPONENTS Crypto)
if(NOT TARGET OpenSSL::Crypto)
  find_package(OpenSSL QUIET COMPONENTS Crypto)
endif()

if(NOT TARGET OpenSSL::Crypto)
  message(FATAL_ERROR "Packages/OpenSSL requires an OpenSSL CMake package that exports OpenSSL::Crypto")
endif()
