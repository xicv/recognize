cmake_minimum_required(VERSION 3.15)
project(recognize)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# Add whisper.cpp as subdirectory
add_subdirectory(../../fixtures/whisper.cpp ${CMAKE_BINARY_DIR}/whisper.cpp)

# CoreML support for macOS
if(APPLE)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(COREML_FRAMEWORK CoreML REQUIRED)
    find_library(ACCELERATE_FRAMEWORK Accelerate REQUIRED)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(METALKIT_FRAMEWORK MetalKit REQUIRED)
    find_library(METALPERFORMANCESHADERS_FRAMEWORK MetalPerformanceShaders REQUIRED)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)

    set(MACOS_FRAMEWORKS
        ${FOUNDATION_FRAMEWORK}
        ${COREML_FRAMEWORK}
        ${ACCELERATE_FRAMEWORK}
        ${METAL_FRAMEWORK}
        ${METALKIT_FRAMEWORK}
        ${METALPERFORMANCESHADERS_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK}
    )
endif()

# SDL2 for audio capture
pkg_check_modules(SDL2 REQUIRED sdl2)

# SQLite3 for history storage (ships with macOS)
find_library(SQLITE3_LIBRARY sqlite3 REQUIRED)

# Create executable
set(TARGET recognize)
add_executable(${TARGET} 
    recognize.cpp
    model_manager.cpp
    config_manager.cpp
    export_manager.cpp
    ptt_manager.cpp
    history_manager.cpp
    text_processing.cpp
    meeting_manager.cpp
    audio_processor.cpp
    ../../fixtures/whisper.cpp/examples/common.cpp
    ../../fixtures/whisper.cpp/examples/common-whisper.cpp
    ../../fixtures/whisper.cpp/examples/common-sdl.cpp
)

# Include directories
target_include_directories(${TARGET} PRIVATE
    ../../fixtures/whisper.cpp/include
    ../../fixtures/whisper.cpp/examples
    ../../fixtures/whisper.cpp/ggml/include
    ${SDL2_INCLUDE_DIRS}
)

# Version
set(RECOGNIZE_VERSION "1.0.0")

# Compile definitions — only define WHISPER_COREML when the subproject enables it
# GGML_USE_METAL is never read by any source code, so it is omitted
target_compile_definitions(${TARGET} PRIVATE
    $<$<BOOL:${WHISPER_COREML}>:WHISPER_COREML=1>
    RECOGNIZE_VERSION="${RECOGNIZE_VERSION}"
)

# Link libraries
target_link_libraries(${TARGET} PRIVATE
    whisper
    ggml
    ${CMAKE_THREAD_LIBS_INIT}
    ${SDL2_LINK_LIBRARIES}
    ${SQLITE3_LIBRARY}
)

# macOS specific linking
if(APPLE)
    target_link_libraries(${TARGET} PRIVATE ${MACOS_FRAMEWORKS})
    
    # Set deployment target
    set_target_properties(${TARGET} PROPERTIES
        MACOSX_DEPLOYMENT_TARGET "12.0"
    )
endif()

# Compiler flags
if(APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    target_compile_options(${TARGET} PRIVATE
        -Wall -Wextra
        -O3
        -mcpu=native          # Better than -march=native on Apple Silicon
        -ffp-contract=fast    # FMA fusion (~5% float speedup)
        -fno-math-errno       # Remove hidden branches from math calls
        -fno-trapping-math    # Safe for audio processing
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${TARGET} PRIVATE
        -Wall -Wextra -O3 -march=native
    )
endif()

# Link-Time Optimization
# ThinLTO: ~95% of full LTO performance, parallel linking, ~40% faster builds
if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${TARGET} PRIVATE -flto=thin)
    target_link_options(${TARGET} PRIVATE -flto=thin)
else()
    set_property(TARGET ${TARGET} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Install target
install(TARGETS ${TARGET} RUNTIME DESTINATION bin)

# Unit tests for text_processing
add_executable(test_text_processing
    tests/test_text_processing.cpp
    text_processing.cpp
)
target_include_directories(test_text_processing PRIVATE ${CMAKE_SOURCE_DIR})
set_target_properties(test_text_processing PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)