cmake_minimum_required(VERSION 3.20)
project(NGIN.UI.InstallConsumer LANGUAGES CXX)

find_package(NGINUI 0.2 CONFIG REQUIRED)

add_executable(NGINUIInstallConsumer main.cpp)
target_compile_features(NGINUIInstallConsumer PRIVATE cxx_std_23)
target_link_libraries(NGINUIInstallConsumer PRIVATE NGIN::UI)

enable_testing()
add_test(NAME NGINUI.InstallConsumer COMMAND NGINUIInstallConsumer)
