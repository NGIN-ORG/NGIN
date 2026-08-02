cmake_minimum_required(VERSION 3.20)
project(NGINUIHostingInstallConsumer LANGUAGES CXX)

find_package(NGINUIHosting 0.4 CONFIG REQUIRED)

add_executable(NGINUIHostingInstallConsumer main.cpp)
target_compile_features(NGINUIHostingInstallConsumer PRIVATE cxx_std_23)
target_link_libraries(NGINUIHostingInstallConsumer PRIVATE NGIN::UI::Hosting)
