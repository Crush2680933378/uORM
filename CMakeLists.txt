cmake_minimum_required(VERSION 3.16)
project(uORM CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 选项设置
option(BUILD_EXAMPLES "Build uORM examples" ON)
option(UORM_BUILD_TESTS "Build uORM tests" OFF)
option(UORM_ENABLE_MYSQL "Enable MySQL driver (libmariadb / libmysqlclient)" ON)
option(UORM_ENABLE_POSTGRESQL "Enable PostgreSQL driver (libpq)" ON)
option(UORM_ENABLE_SQLITE "Enable SQLite driver (sqlite3)" ON)

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

# Examples
if(BUILD_EXAMPLES)
  add_executable(uORM_example
      examples/full_usage_example.cpp
  )
  target_link_libraries(uORM_example PRIVATE uORM::uorm)
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
