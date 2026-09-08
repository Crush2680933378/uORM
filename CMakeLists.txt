cmake_minimum_required(VERSION 3.16)
project(uORM C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 选项设置
option(BUILD_EXAMPLES "Build uORM examples" ON)
option(UORM_BUILD_TESTS "Build uORM tests" OFF)
option(UORM_ENABLE_MYSQL "Enable MySQL driver (libmariadb / libmysqlclient)" ON)
option(UORM_ENABLE_POSTGRESQL "Enable PostgreSQL driver (libpq)" ON)
option(UORM_ENABLE_SQLITE "Enable SQLite driver (sqlite3)" ON)
option(UORM_ENABLE_WEB "Enable Web console (requires asio)" ON)
option(UORM_BUILD_C_ABI "Build the C ABI shared library (uorm_c)" ON)

# Web console 依赖的 standalone asio
if(UORM_ENABLE_WEB)
    find_path(UORM_ASIO_INCLUDE_DIR asio.hpp)
    if(NOT UORM_ASIO_INCLUDE_DIR)
        message(WARNING "uORM: asio not found, web console disabled")
        set(UORM_ENABLE_WEB OFF)
    endif()
endif()

# uORM 是 header-only 库：驱动在编译期按可用性编入，运行期按名字选择
add_library(uorm INTERFACE)
add_library(uORM::uorm ALIAS uorm)

target_include_directories(uorm INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# uJSON (custom JSON library)
set(UJSON_BUILD_EXAMPLES OFF CACHE INTERNAL "Disable uJSON examples")
set(UJSON_BUILD_SHARED OFF CACHE INTERNAL "Force uJSON static")
add_subdirectory(thirdparty/uJSON)
target_link_libraries(uorm INTERFACE uJSON::ujson)

# ---------------- MySQL (libmariadb / libmysqlclient) ----------------
if(UORM_ENABLE_MYSQL)
    find_path(UORM_MYSQL_INCLUDE_DIR mysql.h
        PATH_SUFFIXES mariadb mariadb/ mysql
    )
    find_library(UORM_MYSQL_LIB
        NAMES mariadb libmariadb mysqlclient
    )
    if(UORM_MYSQL_INCLUDE_DIR AND UORM_MYSQL_LIB)
        target_include_directories(uorm INTERFACE ${UORM_MYSQL_INCLUDE_DIR})
        target_link_libraries(uorm INTERFACE ${UORM_MYSQL_LIB})
        target_compile_definitions(uorm INTERFACE UORM_ENABLE_MYSQL)
        message(STATUS "uORM: MySQL driver enabled -> ${UORM_MYSQL_LIB}")
    else()
        message(WARNING "uORM: MySQL client library not found, MySQL driver disabled")
    endif()
endif()

# ---------------- PostgreSQL (libpq) ----------------
if(UORM_ENABLE_POSTGRESQL)
    find_path(UORM_PG_INCLUDE_DIR libpq-fe.h
        PATH_SUFFIXES postgresql postgresql/ postgres
    )
    find_library(UORM_PG_LIB
        NAMES pq libpq
    )
    if(UORM_PG_INCLUDE_DIR AND UORM_PG_LIB)
        target_include_directories(uorm INTERFACE ${UORM_PG_INCLUDE_DIR})
        target_link_libraries(uorm INTERFACE ${UORM_PG_LIB})
        target_compile_definitions(uorm INTERFACE UORM_ENABLE_POSTGRESQL)
        message(STATUS "uORM: PostgreSQL driver enabled -> ${UORM_PG_LIB}")
    else()
        message(WARNING "uORM: libpq not found, PostgreSQL driver disabled")
    endif()
endif()

# ---------------- SQLite (sqlite3) ----------------
if(UORM_ENABLE_SQLITE)
    find_path(UORM_SQLITE_INCLUDE_DIR sqlite3.h)
    find_library(UORM_SQLITE_LIB NAMES sqlite3)
    if(UORM_SQLITE_INCLUDE_DIR AND UORM_SQLITE_LIB)
        target_include_directories(uorm INTERFACE ${UORM_SQLITE_INCLUDE_DIR})
        target_link_libraries(uorm INTERFACE ${UORM_SQLITE_LIB})
        target_compile_definitions(uorm INTERFACE UORM_ENABLE_SQLITE)
        message(STATUS "uORM: SQLite driver enabled -> ${UORM_SQLITE_LIB}")
    else()
        message(WARNING "uORM: sqlite3 not found, SQLite driver disabled")
    endif()
endif()


# Linux threading support
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(uorm INTERFACE Threads::Threads)
endif()

# Web console (standalone asio)
if(UORM_ENABLE_WEB)
    add_library(uorm_web INTERFACE)
    add_library(uORM::web ALIAS uorm_web)
    target_link_libraries(uorm_web INTERFACE uORM::uorm)
    if(WIN32)
        target_link_libraries(uorm_web INTERFACE ws2_32 mswsock)
    endif()
    if(UNIX)
        target_link_libraries(uorm_web INTERFACE pthread)
    endif()
endif()

# ---------------- C ABI 动态库 ----------------
# 整个 C++ 内核编译进 uorm_c 动态库，对外只导出 uorm_c.h 的稳定 C 符号。
if(UORM_BUILD_C_ABI)
    add_library(uorm_c SHARED src/uorm_c.cpp)
    add_library(uORM::c ALIAS uorm_c)
    target_compile_definitions(uorm_c PRIVATE UORM_C_BUILDING)
    target_compile_definitions(uorm_c PUBLIC UORM_C_SHARED)
    target_include_directories(uorm_c PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(uorm_c PRIVATE uORM::uorm)
    if(UNIX)
        target_link_libraries(uorm_c PRIVATE Threads::Threads)
    endif()
endif()

# Examples
if(BUILD_EXAMPLES)
  add_executable(uORM_example
      examples/full_usage_example.cpp
  )
  target_link_libraries(uORM_example PRIVATE uORM::uorm)

  if(UORM_ENABLE_WEB)
    add_executable(uORM_http_demo examples/http_demo.cpp)
    target_link_libraries(uORM_http_demo PRIVATE uORM::web)

    add_executable(uORM_webconsole examples/webconsole.cpp)
    target_link_libraries(uORM_webconsole PRIVATE uORM::web)
  endif()

  if(UORM_BUILD_C_ABI)
    add_executable(uORM_c_demo examples/c_demo.c)
    target_link_libraries(uORM_c_demo PRIVATE uorm_c)
  endif()
endif()

# Tests (doctest)
if(UORM_BUILD_TESTS)
    find_path(UORM_DOCTEST_INCLUDE_DIR doctest/doctest.h)
    if(NOT UORM_DOCTEST_INCLUDE_DIR)
        message(FATAL_ERROR "doctest not found; install doctest or disable UORM_BUILD_TESTS")
    endif()
    enable_testing()
    foreach(t test_unit test_integration test_scenarios test_web test_async)
        add_executable(uorm_${t} tests/${t}.cpp)
        target_include_directories(uorm_${t} PRIVATE ${UORM_DOCTEST_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        target_link_libraries(uorm_${t} PRIVATE uORM::uorm)
        add_test(NAME ${t} COMMAND uorm_${t})
    endforeach()
    if(UORM_BUILD_C_ABI)
        add_executable(uorm_test_c_abi tests/test_c_abi.c)
        target_link_libraries(uorm_test_c_abi PRIVATE uorm_c)
        add_test(NAME test_c_abi COMMAND uorm_test_c_abi)
    endif()
endif()

# Installation
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS uorm ujson
  EXPORT uORMTargets
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

if(UORM_BUILD_C_ABI)
  install(TARGETS uorm_c
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
  )
endif()

install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY thirdparty/uJSON/include/ DESTINATION include)

# Config files
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/uORMConfigVersion.cmake"
    VERSION 0.2.0
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/uORMConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/uORMConfig.cmake"
    INSTALL_DESTINATION lib/cmake/uORM
)

install(EXPORT uORMTargets
  NAMESPACE uORM::
  FILE uORMTargets.cmake
  DESTINATION lib/cmake/uORM
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/uORMConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/uORMConfigVersion.cmake"
    DESTINATION lib/cmake/uORM
)
