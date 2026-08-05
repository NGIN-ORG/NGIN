if(TARGET OpenSSL::Crypto AND TARGET OpenSSL::SSL)
  return()
endif()

find_package(OpenSSL CONFIG QUIET COMPONENTS SSL Crypto)
if(NOT TARGET OpenSSL::Crypto OR NOT TARGET OpenSSL::SSL)
  find_package(OpenSSL QUIET COMPONENTS SSL Crypto)
endif()

if(NOT TARGET OpenSSL::Crypto OR NOT TARGET OpenSSL::SSL)
  message(FATAL_ERROR "Packages/OpenSSL requires an OpenSSL CMake package that exports OpenSSL::SSL and OpenSSL::Crypto")
endif()
