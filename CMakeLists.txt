cmake_minimum_required(VERSION 3.16)
project(uORM CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(UORM_BUILD_SHARED "Build uORM as shared library" ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(UORM_SOURCES
  src/common/utils/ConfigManager.cpp
  src/orm/MySQLConnectionPool.cpp
  src/orm/MySQLAdapter.cpp
)
if(UORM_BUILD_SHARED)
  add_library(uorm SHARED ${UORM_SOURCES})
else()
  add_library(uorm STATIC ${UORM_SOURCES})
endif()
add_library(uORM::uorm ALIAS uorm)
target_include_directories(uorm PUBLIC include)
find_package(nlohmann_json QUIET)
if(nlohmann_json_FOUND)
  target_link_libraries(uorm PUBLIC nlohmann_json::nlohmann_json)
endif()
add_executable(orm_example examples/main.cpp)
target_link_libraries(orm_example PRIVATE uORM::uorm)
install(TARGETS uorm
  EXPORT uORMTargets
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT uORMTargets
  NAMESPACE uORM::
  FILE uORMTargets.cmake
  DESTINATION lib/cmake/uORM
)
install(FILES cmake/uORMConfig.cmake DESTINATION lib/cmake/uORM)
