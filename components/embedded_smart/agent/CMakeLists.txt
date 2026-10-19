cmake_minimum_required(VERSION 3.16)
project(LiteCrab VERSION 1.0.0 LANGUAGES C)

execute_process(
  COMMAND git rev-parse --short=12 HEAD
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE LITECRAB_GIT_REVISION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT LITECRAB_GIT_REVISION)
  set(LITECRAB_GIT_REVISION "unknown")
endif()
string(TIMESTAMP LITECRAB_BUILD_TIME_UTC "%Y-%m-%dT%H:%M:%SZ" UTC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
option(LITECRAB_BUILD_TESTS "Build LiteCrab tests" ON)
option(LITECRAB_ENABLE_STATIC_LINK "Link executables statically when the toolchain supports it" OFF)
option(LITECRAB_INSTALL_METADATA "Install generated capability metadata with release artifacts" OFF)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/Kconfig"
  "${CMAKE_CURRENT_SOURCE_DIR}/build/capabilities.yml"
  "${CMAKE_CURRENT_SOURCE_DIR}/tools/kconfig/configure.py")

include(${CMAKE_CURRENT_SOURCE_DIR}/build/cmake/LiteCrabConfigure.cmake)
if(LITECRAB_CAP_MCP_CLIENT_ENABLED)
  set(LITECRAB_PROFILE_MCP_DEFAULT ON)
else()
  set(LITECRAB_PROFILE_MCP_DEFAULT OFF)
endif()
option(LITECRAB_WITH_MCP_CLIENT "Deprecated compatibility override for MCP Client" ${LITECRAB_PROFILE_MCP_DEFAULT})
option(LITECRAB_WITH_MCP_HTTP "Deprecated compatibility override for MCP HTTP" ${LITECRAB_PROFILE_MCP_DEFAULT})

if(NOT LITECRAB_WITH_MCP_CLIENT STREQUAL LITECRAB_PROFILE_MCP_DEFAULT OR
   NOT LITECRAB_WITH_MCP_HTTP STREQUAL LITECRAB_PROFILE_MCP_DEFAULT)
  message(FATAL_ERROR
    "Legacy MCP options conflict with profile ${LITECRAB_PROFILE}. "
    "Select MCP through defconfigs/${LITECRAB_PROFILE}.defconfig.")
endif()

if(LITECRAB_WITH_MCP_HTTP AND NOT LITECRAB_WITH_MCP_CLIENT)
  message(FATAL_ERROR "LITECRAB_WITH_MCP_HTTP requires LITECRAB_WITH_MCP_CLIENT")
endif()
if(LITECRAB_WITH_MCP_CLIENT AND NOT LITECRAB_WITH_MCP_HTTP)
  message(FATAL_ERROR "The current MCP Client implementation requires LITECRAB_WITH_MCP_HTTP")
endif()

if(WIN32 AND NOT CYGWIN AND NOT MSYS)
  message(FATAL_ERROR "LiteCrab uses POSIX sockets and pthreads. Build it inside WSL or another POSIX environment.")
endif()

find_package(Threads REQUIRED)

if(LITECRAB_PLATFORM STREQUAL "posix")
  if(LITECRAB_ENABLE_STATIC_LINK)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
  endif()
  find_package(OpenSSL REQUIRED)
endif()

if(LITECRAB_CAP_ALARM_ENABLED OR LITECRAB_CAP_MCP_CLIENT_ENABLED)
  find_package(CURL REQUIRED)
endif()
if(LITECRAB_ENABLE_STATIC_LINK)
  find_package(ZLIB REQUIRED)
endif()

add_library(litecrab ${LITECRAB_GENERATED_CORE_SOURCES})
target_sources(litecrab PRIVATE ${LITECRAB_GENERATED_PLATFORM_SOURCES})
target_sources(litecrab PRIVATE ${LITECRAB_GENERATED_DIR}/generated_product_prompt.c)
foreach(LITECRAB_COMPILED_CAPABILITY
    OBSERVABILITY MEMORY SKILLS WORKSPACE_TOOLS PROCESS_TOOLS TCP_GATEWAY
    EMBEDDED_AGENT_API ROBOT_TOOLS TTS_OUTPUT)
  if(LITECRAB_CAP_${LITECRAB_COMPILED_CAPABILITY}_ENABLED)
    target_sources(litecrab PRIVATE ${LITECRAB_CAP_${LITECRAB_COMPILED_CAPABILITY}_SOURCES})
  endif()
endforeach()
target_include_directories(litecrab PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${LITECRAB_GENERATED_DIR}/include)
target_link_libraries(litecrab PUBLIC Threads::Threads m)
if(LITECRAB_PLATFORM STREQUAL "posix")
  target_link_libraries(litecrab PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()
target_compile_definitions(litecrab PRIVATE _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700 _GNU_SOURCE)
target_compile_options(litecrab PRIVATE -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration)
if(LITECRAB_ARTIFACT STREQUAL "static_library")
  set_target_properties(litecrab PROPERTIES OUTPUT_NAME litecrab_agent)
endif()

if(LITECRAB_CAP_ALARM_ENABLED)
  add_library(litecrab_alarm_core STATIC ${LITECRAB_CAP_ALARM_SOURCES})
  target_include_directories(litecrab_alarm_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/alarm
  )
  target_link_libraries(litecrab_alarm_core PUBLIC litecrab)
  target_compile_definitions(litecrab_alarm_core PRIVATE _GNU_SOURCE)
  target_compile_options(litecrab_alarm_core PRIVATE -Wall -Wextra -Wpedantic)

  target_link_libraries(litecrab_alarm_core PUBLIC CURL::libcurl)
endif()

if(LITECRAB_WITH_MCP_CLIENT)
  add_library(litecrab_mcp_client STATIC ${LITECRAB_CAP_MCP_CLIENT_SOURCES})
  target_include_directories(litecrab_mcp_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/mcp_client)
  target_link_libraries(litecrab_mcp_client PUBLIC litecrab CURL::libcurl)
  target_compile_definitions(litecrab_mcp_client PRIVATE _GNU_SOURCE)
  target_compile_options(litecrab_mcp_client PRIVATE
    -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration)
endif()

add_executable(litecrab_server
  src/main.c
  src/app/standalone_app.c
  src/capability/capability_registry.c)
target_link_libraries(litecrab_server PRIVATE litecrab)
if(LITECRAB_CAP_ALARM_ENABLED)
  target_link_libraries(litecrab_server PRIVATE litecrab_alarm_core)
endif()
if(LITECRAB_WITH_MCP_CLIENT)
  target_link_libraries(litecrab_server PRIVATE litecrab_mcp_client)
  target_compile_definitions(litecrab_server PRIVATE LITECRAB_WITH_MCP_CLIENT=1)
endif()
target_include_directories(litecrab_server PRIVATE ${LITECRAB_GENERATED_DIR})
target_compile_definitions(litecrab_server PRIVATE
  LITECRAB_BUILD_REVISION="${LITECRAB_GIT_REVISION}"
  LITECRAB_BUILD_TIME_UTC="${LITECRAB_BUILD_TIME_UTC}")
if(LITECRAB_ENABLE_STATIC_LINK)
  # Static libcurl's package metadata in the SD5091 sysroot does not carry
  # all transitive libraries. Keep its dependencies after libcurl so the
  # one-pass GNU linker can resolve them.
  target_link_libraries(litecrab_server PRIVATE
    CURL::libcurl ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto ${CMAKE_DL_LIBS} Threads::Threads)
endif()
if(LITECRAB_ENABLE_STATIC_LINK AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_link_options(litecrab_server PRIVATE -static)
endif()

include(CTest)
if(BUILD_TESTING AND LITECRAB_BUILD_TESTS)
  add_executable(litecrab_memory_tests tests/test_memory.c)
  target_link_libraries(litecrab_memory_tests PRIVATE litecrab)
  add_test(NAME litecrab_memory_tests COMMAND litecrab_memory_tests)
  add_executable(litecrab_memory_hardening_tests tests/test_memory_hardening.c)
  target_link_libraries(litecrab_memory_hardening_tests PRIVATE litecrab)
  target_compile_options(litecrab_memory_hardening_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME litecrab_memory_hardening_tests COMMAND litecrab_memory_hardening_tests)
  add_executable(litecrab_tests tests/test_main.c)
  target_link_libraries(litecrab_tests PRIVATE litecrab)
  add_test(NAME litecrab_tests COMMAND litecrab_tests)
  add_executable(litecrab_security_tests tests/test_security.c)
  target_link_libraries(litecrab_security_tests PRIVATE litecrab)
  target_compile_options(litecrab_security_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME litecrab_security_tests COMMAND litecrab_security_tests)
  add_executable(litecrab_approval_tests tests/test_approval.c)
  target_link_libraries(litecrab_approval_tests PRIVATE litecrab)
  target_compile_options(litecrab_approval_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME litecrab_approval_tests COMMAND litecrab_approval_tests)
  if(LITECRAB_CAP_ALARM_ENABLED)
    add_executable(litecrab_alarm_tests tests/test_alarm.c)
    target_link_libraries(litecrab_alarm_tests PRIVATE litecrab_alarm_core)
    target_compile_options(litecrab_alarm_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME litecrab_alarm_tests COMMAND litecrab_alarm_tests)
  endif()
  add_executable(litecrab_public_api_tests tests/test_public_api.c)
  target_link_libraries(litecrab_public_api_tests PRIVATE litecrab)
  target_compile_options(litecrab_public_api_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME litecrab_public_api_tests COMMAND litecrab_public_api_tests)
  add_executable(litecrab_capability_manager_tests tests/test_capability_manager.c)
  target_link_libraries(litecrab_capability_manager_tests PRIVATE litecrab)
  target_compile_options(litecrab_capability_manager_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME litecrab_capability_manager_tests COMMAND litecrab_capability_manager_tests)
  add_executable(litecrab_prompt_provider_tests tests/test_prompt_provider.c)
  target_link_libraries(litecrab_prompt_provider_tests PRIVATE litecrab)
  target_compile_options(litecrab_prompt_provider_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME litecrab_prompt_provider_tests COMMAND litecrab_prompt_provider_tests)
  add_executable(litecrab_embedded_agent_api_tests
    tests/test_embedded_agent_api.c
    src/app/embedded_agent.c)
  target_link_libraries(litecrab_embedded_agent_api_tests PRIVATE litecrab)
  target_compile_options(litecrab_embedded_agent_api_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME litecrab_embedded_agent_api_tests COMMAND litecrab_embedded_agent_api_tests)
  add_executable(litecrab_external_tool_backend_tests
    tests/test_external_tool_backend.c
    src/runtime/tools/robot_tools.c)
  target_link_libraries(litecrab_external_tool_backend_tests PRIVATE litecrab)
  target_compile_options(litecrab_external_tool_backend_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME litecrab_external_tool_backend_tests COMMAND litecrab_external_tool_backend_tests)
  add_executable(litecrab_llm_transport_contract_tests tests/test_llm_transport_contract.c)
  target_link_libraries(litecrab_llm_transport_contract_tests PRIVATE litecrab)
  target_compile_options(litecrab_llm_transport_contract_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME litecrab_llm_transport_contract_tests COMMAND litecrab_llm_transport_contract_tests)
  if(LITECRAB_WITH_MCP_CLIENT)
    add_executable(litecrab_mcp_client_probe tests/mcp/mcp_client_probe.c)
    target_link_libraries(litecrab_mcp_client_probe PRIVATE litecrab_mcp_client)
    target_compile_options(litecrab_mcp_client_probe PRIVATE -Wall -Wextra -Wpedantic)
    add_executable(litecrab_device_mcp_test others/mcp_server/src/server.c)
    target_link_libraries(litecrab_device_mcp_test PRIVATE litecrab Threads::Threads)
    target_compile_definitions(litecrab_device_mcp_test PRIVATE
      _POSIX_C_SOURCE=200809L _GNU_SOURCE)
    target_compile_options(litecrab_device_mcp_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME litecrab_configure_tests
      COMMAND ${Python3_EXECUTABLE} -m unittest -v tests/config_matrix/test_configure.py)
    set_tests_properties(litecrab_configure_tests PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 30)
    add_test(NAME litecrab_energy_inspection_product_smoke
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/products/run_product_smoke.py
              --repo ${CMAKE_CURRENT_SOURCE_DIR}
              --product energy_inspection
              --spec ${CMAKE_CURRENT_SOURCE_DIR}/tests/products/energy_inspection_smoke.yml)
    set_tests_properties(litecrab_energy_inspection_product_smoke PROPERTIES TIMEOUT 30)
    add_test(NAME litecrab_memory_e2e
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_memory_e2e.py $<TARGET_FILE:litecrab_server>)
    set_tests_properties(litecrab_memory_e2e PROPERTIES TIMEOUT 30)
    if(LITECRAB_WITH_MCP_CLIENT)
      add_test(NAME litecrab_mcp_stack
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/mcp/test_mcp_stack.py
                $<TARGET_FILE:litecrab_device_mcp_test>
                $<TARGET_FILE:litecrab_mcp_client_probe>)
      set_tests_properties(litecrab_mcp_stack PROPERTIES TIMEOUT 45)
      add_test(NAME litecrab_mcp_agent_e2e
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/mcp/test_mcp_agent_e2e.py
                $<TARGET_FILE:litecrab_server>
                $<TARGET_FILE:litecrab_device_mcp_test>
                ${CMAKE_CURRENT_SOURCE_DIR})
      set_tests_properties(litecrab_mcp_agent_e2e PROPERTIES TIMEOUT 45)
      add_test(NAME litecrab_mcp_config
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/mcp/test_mcp_config.py
                $<TARGET_FILE:litecrab_server>)
      set_tests_properties(litecrab_mcp_config PROPERTIES TIMEOUT 15)
    endif()
    add_test(NAME litecrab_tcp_e2e
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tcp_e2e.py $<TARGET_FILE:litecrab_server> ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(litecrab_tcp_e2e PROPERTIES TIMEOUT 20)
    add_test(NAME litecrab_approval_e2e
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_approval_e2e.py $<TARGET_FILE:litecrab_server>)
    set_tests_properties(litecrab_approval_e2e PROPERTIES TIMEOUT 150)
    add_test(NAME litecrab_transfer_station
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_transfer_station.py)
    set_tests_properties(litecrab_transfer_station PROPERTIES TIMEOUT 20)
    add_test(NAME litecrab_agent_matrix
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_agent_matrix.py $<TARGET_FILE:litecrab_server> ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(litecrab_agent_matrix PROPERTIES TIMEOUT 45)
    add_test(NAME litecrab_resolver_modes
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_resolver_modes.py $<TARGET_FILE:litecrab_server> ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(litecrab_resolver_modes PROPERTIES TIMEOUT 30)
    add_test(NAME litecrab_session_restart
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_session_restart.py $<TARGET_FILE:litecrab_server>)
    set_tests_properties(litecrab_session_restart PROPERTIES TIMEOUT 30)
    add_test(NAME litecrab_ui_session_transport
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_session_transport.py ${CMAKE_CURRENT_SOURCE_DIR}/others/UI/app.py)
    set_tests_properties(litecrab_ui_session_transport PROPERTIES TIMEOUT 10)
    add_test(NAME litecrab_security_e2e
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_security_e2e.py $<TARGET_FILE:litecrab_server>)
    set_tests_properties(litecrab_security_e2e PROPERTIES TIMEOUT 30)
    add_test(NAME litecrab_alarm_e2e
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_alarm_e2e.py $<TARGET_FILE:litecrab_server> ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(litecrab_alarm_e2e PROPERTIES TIMEOUT 90)
    add_test(NAME litecrab_alarm_subscription_e2e
      COMMAND ${CMAKE_COMMAND} -E env ALARM_TEST_MODE=subscription ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_alarm_e2e.py $<TARGET_FILE:litecrab_server> ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(litecrab_alarm_subscription_e2e PROPERTIES TIMEOUT 60)
    add_test(NAME litecrab_long_running_stress
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_long_running_stress.py $<TARGET_FILE:litecrab_server> ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(litecrab_long_running_stress PROPERTIES TIMEOUT 90)
    add_test(NAME litecrab_deployment_contract
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_deployment_contract.py
              ${CMAKE_COMMAND} ${CMAKE_BINARY_DIR} $<TARGET_FILE:litecrab_server>)
    set_tests_properties(litecrab_deployment_contract PROPERTIES TIMEOUT 30)
  endif()
endif()

# The SD5091 formal artifact is one executable. Configuration and Skills are
# provisioned independently on the board and are intentionally not installed.
if(LITECRAB_ARTIFACT STREQUAL "static_library")
  install(TARGETS litecrab ARCHIVE DESTINATION lib)
  install(DIRECTORY include/litecrab DESTINATION include)
else()
  install(TARGETS litecrab_server RUNTIME DESTINATION bin)
endif()
if(LITECRAB_INSTALL_METADATA)
  install(FILES ${LITECRAB_GENERATED_DIR}/litecrab_capabilities.json DESTINATION metadata)
endif()
