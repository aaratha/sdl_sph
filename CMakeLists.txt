cmake_minimum_required(VERSION 3.20)
project(waveguide LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add SDL3 submodules
add_subdirectory(external/sdl)
add_subdirectory(external/sdl_ttf)



# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/include
)

# Gather source files
file(GLOB_RECURSE WAVEGUIDE_SOURCES
    ${PROJECT_SOURCE_DIR}/src/*.c
)

# Create executable
add_executable(${PROJECT_NAME} ${WAVEGUIDE_SOURCES})

# Link SDL3 + SDL_ttf + SDL_gpu (for shadercross if needed)
target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf)

# Copy assets folder to build directory
file(COPY ${CMAKE_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR})

# Enable compiler warnings (C mode)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)

