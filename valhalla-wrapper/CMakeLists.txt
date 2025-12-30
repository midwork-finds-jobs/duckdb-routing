cmake_minimum_required(VERSION 3.14)
project(valhalla_wrapper CXX)

# Valhalla requires C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Options
option(VALHALLA_WRAPPER_BUILD_STATIC "Build static library" OFF)
option(VALHALLA_WRAPPER_BUILD_SHARED "Build shared library" ON)

# Find Valhalla - try cmake package first, then manual paths
find_package(valhalla CONFIG QUIET)

if(valhalla_FOUND)
    message(STATUS "Found system Valhalla via CMake")
    set(VALHALLA_INCLUDE_DIRS "")
    set(VALHALLA_LIBRARIES valhalla::valhalla)
else()
    # Try to find Valhalla manually
    find_path(VALHALLA_INCLUDE_DIR
        NAMES valhalla/tyr/actor.h
        PATHS /usr/local/include /usr/include /opt/homebrew/include
    )
    find_library(VALHALLA_LIBRARY
        NAMES valhalla
        PATHS /usr/local/lib /usr/lib /opt/homebrew/lib
    )

    if(VALHALLA_INCLUDE_DIR AND VALHALLA_LIBRARY)
        message(STATUS "Found Valhalla manually:")
        message(STATUS "  Include: ${VALHALLA_INCLUDE_DIR}")
        message(STATUS "  Library: ${VALHALLA_LIBRARY}")
        set(VALHALLA_INCLUDE_DIRS ${VALHALLA_INCLUDE_DIR})
        set(VALHALLA_LIBRARIES ${VALHALLA_LIBRARY})
    else()
        message(FATAL_ERROR "Valhalla not found. Install it with: brew install valhalla")
    endif()
endif()

# Find Boost (header-only usage)
find_package(Boost REQUIRED)

# Common library paths for homebrew
set(LIB_SEARCH_PATHS
    /opt/homebrew/lib
    /opt/homebrew/opt/sqlite/lib
    /opt/homebrew/opt/protobuf/lib
    /opt/homebrew/opt/abseil/lib
    /usr/local/lib
    /usr/lib
)

# Find Valhalla dependencies manually - prioritize homebrew for arm64
find_library(PROTOBUF_LIBRARY NAMES protobuf PATHS ${LIB_SEARCH_PATHS} NO_DEFAULT_PATH)
find_library(ZLIB_LIBRARY NAMES z PATHS ${LIB_SEARCH_PATHS})
find_library(CURL_LIBRARY NAMES curl PATHS ${LIB_SEARCH_PATHS})
find_library(GEOS_C_LIBRARY NAMES geos_c PATHS ${LIB_SEARCH_PATHS} NO_DEFAULT_PATH)
find_library(GEOS_LIBRARY NAMES geos PATHS ${LIB_SEARCH_PATHS} NO_DEFAULT_PATH)
find_library(LZ4_LIBRARY NAMES lz4 PATHS ${LIB_SEARCH_PATHS} NO_DEFAULT_PATH)
find_library(SQLITE3_LIBRARY NAMES sqlite3 PATHS ${LIB_SEARCH_PATHS} NO_DEFAULT_PATH)

# Abseil libraries needed by protobuf/valhalla
find_library(ABSEIL_BASE_LIBRARY NAMES absl_base PATHS ${LIB_SEARCH_PATHS} NO_DEFAULT_PATH)
find_library(ABSEIL_RAW_LOGGING_LIBRARY NAMES absl_raw_logging_internal PATHS ${LIB_SEARCH_PATHS} NO_DEFAULT_PATH)
find_library(ABSEIL_LOG_MESSAGE_LIBRARY NAMES absl_log_internal_message PATHS ${LIB_SEARCH_PATHS} NO_DEFAULT_PATH)
find_library(ABSEIL_LOG_CHECK_OP_LIBRARY NAMES absl_log_internal_check_op PATHS ${LIB_SEARCH_PATHS} NO_DEFAULT_PATH)
find_library(ABSEIL_STRINGS_LIBRARY NAMES absl_strings PATHS ${LIB_SEARCH_PATHS} NO_DEFAULT_PATH)
find_library(ABSEIL_STR_FORMAT_LIBRARY NAMES absl_str_format_internal PATHS ${LIB_SEARCH_PATHS} NO_DEFAULT_PATH)
find_library(ABSEIL_THROW_DELEGATE_LIBRARY NAMES absl_throw_delegate PATHS ${LIB_SEARCH_PATHS} NO_DEFAULT_PATH)

# Collect all Valhalla dependencies
set(VALHALLA_DEPS
    ${PROTOBUF_LIBRARY}
    ${ZLIB_LIBRARY}
    ${CURL_LIBRARY}
    ${GEOS_C_LIBRARY}
    ${GEOS_LIBRARY}
    ${LZ4_LIBRARY}
    ${SQLITE3_LIBRARY}
    ${ABSEIL_BASE_LIBRARY}
    ${ABSEIL_RAW_LOGGING_LIBRARY}
    ${ABSEIL_LOG_MESSAGE_LIBRARY}
    ${ABSEIL_LOG_CHECK_OP_LIBRARY}
    ${ABSEIL_STRINGS_LIBRARY}
    ${ABSEIL_STR_FORMAT_LIBRARY}
    ${ABSEIL_THROW_DELEGATE_LIBRARY}
)

# RapidJSON is included in Valhalla's third_party headers
# Add third_party path so <rapidjson/...> includes work
set(VALHALLA_THIRD_PARTY_DIR "${VALHALLA_INCLUDE_DIR}/valhalla/third_party")

# Wrapper source files
set(WRAPPER_SOURCES
    src/wrapper.cpp
)

# Build shared library
if(VALHALLA_WRAPPER_BUILD_SHARED)
    add_library(valhalla_wrapper SHARED ${WRAPPER_SOURCES})
    target_include_directories(valhalla_wrapper
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${Boost_INCLUDE_DIRS}
            ${VALHALLA_INCLUDE_DIRS}
            ${VALHALLA_THIRD_PARTY_DIR}
    )

    target_link_libraries(valhalla_wrapper PRIVATE ${VALHALLA_LIBRARIES} ${VALHALLA_DEPS})

    # Define VALHALLA_WRAPPER_EXPORTS to export C API
    target_compile_definitions(valhalla_wrapper PRIVATE VALHALLA_WRAPPER_EXPORTS)

    # Hide C++ symbols, only expose C API
    set_target_properties(valhalla_wrapper PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )

    # Set output name and version
    set_target_properties(valhalla_wrapper PROPERTIES
        OUTPUT_NAME "valhalla_wrapper"
        VERSION 1.0.0
        SOVERSION 1
    )
endif()

# Build static library
if(VALHALLA_WRAPPER_BUILD_STATIC)
    add_library(valhalla_wrapper_static STATIC ${WRAPPER_SOURCES})
    target_include_directories(valhalla_wrapper_static
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${Boost_INCLUDE_DIRS}
            ${VALHALLA_INCLUDE_DIRS}
            ${VALHALLA_THIRD_PARTY_DIR}
    )

    target_link_libraries(valhalla_wrapper_static PRIVATE ${VALHALLA_LIBRARIES} ${VALHALLA_DEPS})

    set_target_properties(valhalla_wrapper_static PROPERTIES
        OUTPUT_NAME "valhalla_wrapper"
    )
endif()

# Install
include(GNUInstallDirs)

install(FILES include/valhalla_wrapper.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(VALHALLA_WRAPPER_BUILD_SHARED)
    install(TARGETS valhalla_wrapper
        EXPORT valhalla_wrapper-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

if(VALHALLA_WRAPPER_BUILD_STATIC)
    install(TARGETS valhalla_wrapper_static
        EXPORT valhalla_wrapper-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()

# Print summary
message(STATUS "")
message(STATUS "=== Valhalla Wrapper Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Shared library: ${VALHALLA_WRAPPER_BUILD_SHARED}")
message(STATUS "Static library: ${VALHALLA_WRAPPER_BUILD_STATIC}")
message(STATUS "")
