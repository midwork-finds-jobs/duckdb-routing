cmake_minimum_required(VERSION 3.12)

# Set extension name here
set(TARGET_NAME valhalla_routing)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})
include_directories(src/include)

set(EXTENSION_SOURCES
    src/travel_time_extension.cpp
    src/config_setting.cpp
    src/valhalla_build_tiles_simple.cpp
)

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

# Find Valhalla libraries (for tile building)
set(VALHALLA_FOUND FALSE)

# Try pkg-config first
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(VALHALLA valhalla)
endif()

# If pkg-config didn't find it, try manual paths
if(NOT VALHALLA_FOUND)
    # Check common installation paths
    find_path(VALHALLA_INCLUDE_DIR
        NAMES valhalla/valhalla.h
        PATHS
            /usr/local/include
            /usr/include
            /opt/homebrew/include
    )

    find_library(VALHALLA_LIBRARY
        NAMES valhalla
        PATHS
            /usr/local/lib
            /usr/lib
            /opt/homebrew/lib
    )

    if(VALHALLA_INCLUDE_DIR AND VALHALLA_LIBRARY)
        set(VALHALLA_FOUND TRUE)
        set(VALHALLA_INCLUDE_DIRS ${VALHALLA_INCLUDE_DIR})
        set(VALHALLA_LIBRARIES ${VALHALLA_LIBRARY})
        message(STATUS "Found Valhalla manually: ${VALHALLA_INCLUDE_DIR}")
        include_directories(${VALHALLA_INCLUDE_DIRS})

        # Add Valhalla third-party includes (rapidjson, etc)
        include_directories(${VALHALLA_INCLUDE_DIR}/valhalla/third_party)

        # Find Boost (required by Valhalla)
        find_path(BOOST_INCLUDE_DIR
            NAMES boost/version.hpp
            PATHS
                /opt/homebrew/include
                /usr/local/include
                /usr/include
        )
        if(BOOST_INCLUDE_DIR)
            include_directories(${BOOST_INCLUDE_DIR})
            message(STATUS "Found Boost: ${BOOST_INCLUDE_DIR}")
        else()
            message(WARNING "Boost not found. Valhalla may not compile.")
        endif()
    else()
        message(WARNING "Valhalla not found. Tile building will not work.")
    endif()
endif()

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# Valhalla requires C++17
if(VALHALLA_FOUND)
    set_target_properties(${EXTENSION_NAME} PROPERTIES CXX_STANDARD 17)
    set_target_properties(${LOADABLE_EXTENSION_NAME} PROPERTIES CXX_STANDARD 17)
endif()

# Link the Valhalla wrapper library
if(VALHALLA_WRAPPER_LIB)
    target_link_libraries(${EXTENSION_NAME} ${VALHALLA_WRAPPER_LIB})
    target_link_libraries(${LOADABLE_EXTENSION_NAME} ${VALHALLA_WRAPPER_LIB})
endif()

# Link Valhalla libraries (for tile building)
if(VALHALLA_FOUND)
    target_link_libraries(${EXTENSION_NAME} ${VALHALLA_LIBRARIES})
    target_link_libraries(${LOADABLE_EXTENSION_NAME} ${VALHALLA_LIBRARIES})
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
