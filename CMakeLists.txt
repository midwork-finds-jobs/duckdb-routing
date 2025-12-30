cmake_minimum_required(VERSION 3.12)

# Set extension name here
set(TARGET_NAME travel_time)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})
include_directories(src/include)

set(EXTENSION_SOURCES src/travel_time_extension.cpp)

# Path to Valhalla wrapper
set(VALHALLA_WRAPPER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/valhalla-wrapper")
set(VALHALLA_WRAPPER_INCLUDE "${VALHALLA_WRAPPER_DIR}/include")

# Check if wrapper library exists (pre-built)
find_library(VALHALLA_WRAPPER_LIB
    NAMES valhalla_wrapper
    PATHS
        "${VALHALLA_WRAPPER_DIR}/build"
        "${VALHALLA_WRAPPER_DIR}/lib"
        "/usr/local/lib"
        "/usr/lib"
    NO_DEFAULT_PATH
)

if(NOT VALHALLA_WRAPPER_LIB)
    # Try system paths
    find_library(VALHALLA_WRAPPER_LIB NAMES valhalla_wrapper)
endif()

if(VALHALLA_WRAPPER_LIB)
    message(STATUS "Found Valhalla wrapper: ${VALHALLA_WRAPPER_LIB}")
else()
    message(WARNING "Valhalla wrapper library not found. Build it first:
        cd valhalla-wrapper && mkdir build && cd build
        cmake -DCMAKE_BUILD_TYPE=Release ..
        make")
endif()

# Include the wrapper header
include_directories(${VALHALLA_WRAPPER_INCLUDE})

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# Link the Valhalla wrapper library
if(VALHALLA_WRAPPER_LIB)
    target_link_libraries(${EXTENSION_NAME} ${VALHALLA_WRAPPER_LIB})
    target_link_libraries(${LOADABLE_EXTENSION_NAME} ${VALHALLA_WRAPPER_LIB})
endif()

# macOS needs additional frameworks for dynamic loading
if(APPLE)
    target_link_libraries(${EXTENSION_NAME} "-framework CoreFoundation")
    target_link_libraries(${LOADABLE_EXTENSION_NAME} "-framework CoreFoundation")
endif()

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
