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
    
    set(MACOS_FRAMEWORKS 
        ${FOUNDATION_FRAMEWORK}
        ${COREML_FRAMEWORK} 
        ${ACCELERATE_FRAMEWORK}
        ${METAL_FRAMEWORK}
        ${METALKIT_FRAMEWORK}
        ${METALPERFORMANCESHADERS_FRAMEWORK}
    )
endif()

# SDL2 for audio capture
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# Create executable
set(TARGET recognize)
add_executable(${TARGET} 
    recognize.cpp
    model_manager.cpp
    config_manager.cpp
    export_manager.cpp
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

# Compile definitions
target_compile_definitions(${TARGET} PRIVATE
    WHISPER_COREML=1
    GGML_USE_METAL=1
)

# Link libraries
target_link_libraries(${TARGET} PRIVATE
    whisper
    ggml
    ${CMAKE_THREAD_LIBS_INIT}
    ${SDL2_LINK_LIBRARIES}
)

# macOS specific linking
if(APPLE)
    target_link_libraries(${TARGET} PRIVATE ${MACOS_FRAMEWORKS})
    
    # Set deployment target
    set_target_properties(${TARGET} PROPERTIES
        MACOSX_DEPLOYMENT_TARGET "10.15"
    )
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${TARGET} PRIVATE
        -Wall -Wextra -Wpedantic
        -O3
        -march=native
    )
endif()

# Install target
install(TARGETS ${TARGET} RUNTIME DESTINATION bin)