cmake_minimum_required(VERSION 3.15)
project(StreamingUtilities)

include(FetchContent)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

# Make the default build type Release. If user or another
# project sets a different value than use that
if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "Setting build type to default -- Release")
  set(CMAKE_BUILD_TYPE "Release" CACHE
      STRING "Choose the type of build." FORCE)
endif()
message(STATUS "StreamingUtilities Build Type: ${CMAKE_BUILD_TYPE}")

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  message(STATUS "Adding GNU compiler flags")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W -Wall")
elseif(STATUS "${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  message("Adding MSVC compiler flags")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Wall")
else()
  message(STATUS "${CMAKE_CXX_COMPILER_ID} not recognized, no flags added")
endif()

#add_compile_options(-fsanitize=address)
#add_link_options(-fsanitize=address)
#add_compile_options(-fsanitize=undefined)
#add_link_options(-fsanitize=undefined)

# Check if this project is the top directory or build type is Debug
# If so, build executables, otherwise, only build libraries
get_directory_property(not_root PARENT_DIRECTORY)
if (not_root AND "${CMAKE_BUILD_TYPE}" STREQUAL "Release")
  set(BUILD_EXE OFF)
else()
  set(BUILD_EXE ON)
  message (STATUS "StreamingUtilities building executables")
endif()

# Common types and components
FetchContent_Declare(
  GraphZeppelinCommon

  GIT_REPOSITORY  https://github.com/GraphStreamingProject/GraphZeppelinCommon.git
  GIT_TAG         main
)

# Get xxHash
FetchContent_Declare(
  xxhash

  GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
  GIT_TAG        v0.8.0
)

FetchContent_MakeAvailable(GraphZeppelinCommon xxHash)
#####
# Some additional steps for xxHash as it is unofficial
#####
#xxHash messes with BUILD_SHARED_LIBS if it is empty
set(SAVED_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")

add_subdirectory(
  "${xxhash_SOURCE_DIR}/cmake_unofficial"
  "${xxhash_BINARY_DIR}"
  EXCLUDE_FROM_ALL
)
#Restore BUILD_SHARED_LIBS
set(BUILD_SHARED_LIBS "${SAVED_BUILD_SHARED_LIBS}" CACHE BOOL "" FORCE)

add_library(StreamingUtilities
  src/static_erdos_generator.cpp
  src/dynamic_erdos_generator.cpp)
add_dependencies(StreamingUtilities xxhash GraphZeppelinCommon)
target_link_libraries(StreamingUtilities PUBLIC xxhash GraphZeppelinCommon)
target_include_directories(StreamingUtilities PUBLIC include/)
target_compile_definitions(StreamingUtilities PUBLIC XXH_INLINE_ALL)

if (BUILD_EXE)
  add_executable(run_permuted_set
    tools/run_permuted_set.cpp)
  add_dependencies(run_permuted_set StreamingUtilities)
  target_link_libraries(run_permuted_set PRIVATE StreamingUtilities)

  add_executable(run_erdos_gen
    tools/run_erdos_gen.cpp)
  add_dependencies(run_erdos_gen StreamingUtilities)
  target_link_libraries(run_erdos_gen PRIVATE StreamingUtilities)

  add_executable(stream_file_converter
    tools/stream_file_converter.cpp)
  add_dependencies(stream_file_converter StreamingUtilities)
  target_link_libraries(stream_file_converter PRIVATE StreamingUtilities)

  add_executable(stream_validator
    tools/stream_validator.cpp)
  add_dependencies(stream_validator StreamingUtilities)
  target_link_libraries(stream_validator PRIVATE StreamingUtilities)

  add_executable(streamifier
    tools/streamifier.cpp)
  add_dependencies(streamifier StreamingUtilities)
  target_link_libraries(streamifier PRIVATE StreamingUtilities)
add_executable(run_matrix_market_convert
  tools/run_matrix_market_convert.cpp)
add_dependencies(run_matrix_market_convert StreamingUtilities)
target_link_libraries(run_matrix_market_convert PRIVATE StreamingUtilities)
endif()

