cmake_minimum_required(VERSION 3.16)
project(uORM CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 选项设置
option(UORM_BUILD_SHARED "Build uORM as shared library" ON)
option(USE_POSTGRESQL "Enable PostgreSQL support instead of MySQL" OFF)
option(BUILD_EXAMPLES "Build uORM examples" ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Source files (Currently empty as it is header-only, using dummy for target creation)
# In a real header-only lib, we would use INTERFACE, but to match the requested style:
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp "")
set(UORM_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp)

# Library Definition
if(UORM_BUILD_SHARED)
  add_library(uorm SHARED ${UORM_SOURCES})
else()
  add_library(uorm STATIC ${UORM_SOURCES})
endif()

add_library(uORM::uorm ALIAS uorm)

# Include Directories
target_include_directories(uorm PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Thirdparty Directory
set(THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
set(THIRDPARTY_MYSQL_DIR "${THIRDPARTY_DIR}/mysql")
set(THIRDPARTY_PG_DIR "${THIRDPARTY_DIR}/postgresql")

# ujson (Custom JSON Library)
add_subdirectory(thirdparty/uJSON)
target_link_libraries(uorm PRIVATE $<BUILD_INTERFACE:uJSON_static>)
target_compile_definitions(uorm PUBLIC UJSON_STATIC)

# Database Driver Configuration
if(USE_POSTGRESQL)
    message(STATUS "Configuring uORM with PostgreSQL support...")
    
    # Check thirdparty first
    find_path(PQXX_INCLUDE_DIR pqxx/pqxx
        PATHS "${THIRDPARTY_DIR}/postgresql/include"
        NO_DEFAULT_PATH
    )
    find_library(PQXX_LIB pqxx
        PATHS "${THIRDPARTY_DIR}/postgresql/lib"
        NO_DEFAULT_PATH
    )
    find_library(PQ_LIB pq
        PATHS "${THIRDPARTY_DIR}/postgresql/lib"
        NO_DEFAULT_PATH
    )

    if(PQXX_INCLUDE_DIR AND PQXX_LIB)
         target_include_directories(uorm PUBLIC ${PQXX_INCLUDE_DIR})
         target_link_libraries(uorm PUBLIC ${PQXX_LIB})
         if(PQ_LIB)
             target_link_libraries(uorm PUBLIC ${PQ_LIB})
         endif()
    else()
        # Fallback to system search
        find_package(PkgConfig QUIET)
        pkg_check_modules(LIBPQXX libpqxx)
        
        if(LIBPQXX_FOUND)
            target_include_directories(uorm PUBLIC ${LIBPQXX_INCLUDE_DIRS})
            target_link_libraries(uorm PUBLIC ${LIBPQXX_LIBRARIES})
        else()
             message(FATAL_ERROR "libpqxx not found. Install libpqxx-dev")
        endif()
    endif()
    
    target_compile_definitions(uorm PUBLIC USE_POSTGRESQL)
else()
    message(STATUS "Configuring uORM with MySQL support...")
    
    # Check thirdparty first
    find_path(MYSQL_CONN_INCLUDE_DIR mysql_connection.h
        PATH_SUFFIXES jdbc
        PATHS "${THIRDPARTY_MYSQL_DIR}/include"
        NO_DEFAULT_PATH
    )
    find_library(MYSQL_CONN_LIB NAMES mysqlcppconn mysqlcppconn8
        PATHS "${THIRDPARTY_MYSQL_DIR}/lib"
        NO_DEFAULT_PATH
    )
    
    if(MYSQL_CONN_INCLUDE_DIR AND MYSQL_CONN_LIB)
        target_include_directories(uorm PUBLIC ${MYSQL_CONN_INCLUDE_DIR})
        target_link_libraries(uorm PUBLIC ${MYSQL_CONN_LIB})
    else()
        # Fallback to system search
        find_package(mysql-connector-cpp CONFIG QUIET)
        if(mysql-connector-cpp_FOUND)
            target_link_libraries(uorm PUBLIC mysql-connector-cpp::connector)
        else()
            # Manual system find (library + include)
            find_path(MYSQL_SYS_INCLUDE_DIR mysql_connection.h PATH_SUFFIXES jdbc)
            find_library(MYSQL_SYS_LIB NAMES mysqlcppconn mysqlcppconn8)

            if(MYSQL_SYS_INCLUDE_DIR AND MYSQL_SYS_LIB)
                target_include_directories(uorm PUBLIC ${MYSQL_SYS_INCLUDE_DIR})
                target_link_libraries(uorm PUBLIC ${MYSQL_SYS_LIB})
            else()
                message(FATAL_ERROR "MySQL Connector/C++ not found. Install libmysqlcppconn-dev")
            endif()
        endif()
    endif()
    
    target_compile_definitions(uorm PUBLIC USE_MYSQL)
endif()

# Linux threading support
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(uorm PUBLIC Threads::Threads)
endif()

# Examples
if(BUILD_EXAMPLES)
  add_executable(uORM_example 
      examples/full_usage_example.cpp 
  )
  target_link_libraries(uORM_example PRIVATE uORM::uorm)
  target_include_directories(uORM_example PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
  )
endif()

# Installation
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS uorm 
  EXPORT uORMTargets 
  RUNTIME DESTINATION bin 
  LIBRARY DESTINATION lib 
  ARCHIVE DESTINATION lib 
) 

install(DIRECTORY include/ DESTINATION include) 

# Config files
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/uORMConfigVersion.cmake"
    VERSION 1.0.0
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

