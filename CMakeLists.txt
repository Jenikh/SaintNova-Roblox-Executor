cmake_minimum_required(VERSION 3.20)
project(SaintNova VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# Luau (MIT) - vendored upstream tarball, pinned at 0.747.
# ---------------------------------------------------------------------------
set(LUAU_BUILD_CLI     OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_WEB     OFF CACHE BOOL "" FORCE)
set(LUAU_EXTERN_C      ON  CACHE BOOL "" FORCE)
add_subdirectory(third_party/luau-0.747 EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Embedded sandbox prelude (harness/sandbox.luau -> generated C++ literal)
# ---------------------------------------------------------------------------
set(SAINTNOVA_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${SAINTNOVA_GENERATED_DIR})
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/harness/sandbox.luau SAINTNOVA_SANDBOX_CODE)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/sandbox_embed.cpp.in
  ${SAINTNOVA_GENERATED_DIR}/sandbox_embed.cpp
  @ONLY)

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_library(saintnova_core STATIC
  src/protocol.cpp
  src/pipe_server.cpp
  src/server_app.cpp
  src/luau_vm.cpp
  src/harness_api.cpp
  src/crypt_api.cpp
  src/harness_backend.cpp
  src/roblox_backend.cpp
  src/core.cpp
  src/log.cpp
  ${SAINTNOVA_GENERATED_DIR}/sandbox_embed.cpp
)

target_include_directories(saintnova_core
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
          ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_compile_definitions(saintnova_core PUBLIC
  SAINTNOVA_VERSION="${PROJECT_VERSION}"
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _WIN32_WINNT=0x0A00
)

target_link_libraries(saintnova_core PUBLIC Luau.VM Luau.Compiler Luau.Ast bcrypt advapi32)

if(MSVC)
  target_compile_options(saintnova_core PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(saintnova_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
  target_link_options(saintnova_core INTERFACE -static-libgcc -static-libstdc++)
endif()

# ---------------------------------------------------------------------------
# Host executable
# ---------------------------------------------------------------------------
add_executable(saintnova src/main.cpp)
target_link_libraries(saintnova PRIVATE saintnova_core)

# ---------------------------------------------------------------------------
# Embeddable DLL (same core, stable C surface for C#/native hosts)
# ---------------------------------------------------------------------------
add_library(saintnova_shared SHARED src/dllmain.cpp)
target_link_libraries(saintnova_shared PRIVATE saintnova_core)
target_compile_definitions(saintnova_shared PRIVATE SAINTNOVA_BUILD_DLL)
set_target_properties(saintnova_shared PROPERTIES OUTPUT_NAME saintnova)
