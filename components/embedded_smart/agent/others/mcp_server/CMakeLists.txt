cmake_minimum_required(VERSION 3.16)
project(LiteCrabDeviceMcpServer VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

find_package(Threads REQUIRED)

add_executable(litecrab_device_mcp
  src/server.c
  ../../src/util/json.c)
target_include_directories(litecrab_device_mcp PRIVATE ../../include)
target_link_libraries(litecrab_device_mcp PRIVATE Threads::Threads)
target_compile_definitions(litecrab_device_mcp PRIVATE
  _POSIX_C_SOURCE=200809L _GNU_SOURCE)
target_compile_options(litecrab_device_mcp PRIVATE
  -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration)

install(TARGETS litecrab_device_mcp RUNTIME DESTINATION bin)
