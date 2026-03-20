# cmake version we need, not super new
cmake_minimum_required(VERSION 3.16)
# project name + version, keeps it orginized
project(assignment_1 VERSION 0.1.0 LANGUAGES CXX)

# set c++ standard so compiler knows what to expect
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# quill logging level, can be changed when configuring
set(QUILL_ACTIVE_LOG_LEVEL "INFO" CACHE STRING "Compile-time log level for Quill (TRACE_L3, TRACE_L2, TRACE_L1, DEBUG, INFO, NOTICE, WARNING, ERROR, CRITICAL)")
string(TOUPPER "${QUILL_ACTIVE_LOG_LEVEL}" QUILL_ACTIVE_LOG_LEVEL_UPPER)

# map string to preproc define, a bit verbose but clearer
set(QUILL_ACTIVE_LOG_LEVEL_DEFINE "")
if (QUILL_ACTIVE_LOG_LEVEL_UPPER STREQUAL "TRACE_L3")
    set(QUILL_ACTIVE_LOG_LEVEL_DEFINE "QUILL_COMPILE_ACTIVE_LOG_LEVEL_TRACE_L3")
elseif (QUILL_ACTIVE_LOG_LEVEL_UPPER STREQUAL "TRACE_L2")
    set(QUILL_ACTIVE_LOG_LEVEL_DEFINE "QUILL_COMPILE_ACTIVE_LOG_LEVEL_TRACE_L2")
elseif (QUILL_ACTIVE_LOG_LEVEL_UPPER STREQUAL "TRACE_L1")
    set(QUILL_ACTIVE_LOG_LEVEL_DEFINE "QUILL_COMPILE_ACTIVE_LOG_LEVEL_TRACE_L1")
elseif (QUILL_ACTIVE_LOG_LEVEL_UPPER STREQUAL "DEBUG")
    set(QUILL_ACTIVE_LOG_LEVEL_DEFINE "QUILL_COMPILE_ACTIVE_LOG_LEVEL_DEBUG")
elseif (QUILL_ACTIVE_LOG_LEVEL_UPPER STREQUAL "INFO")
    set(QUILL_ACTIVE_LOG_LEVEL_DEFINE "QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO")
elseif (QUILL_ACTIVE_LOG_LEVEL_UPPER STREQUAL "NOTICE")
    set(QUILL_ACTIVE_LOG_LEVEL_DEFINE "QUILL_COMPILE_ACTIVE_LOG_LEVEL_NOTICE")
elseif (QUILL_ACTIVE_LOG_LEVEL_UPPER STREQUAL "WARNING")
    set(QUILL_ACTIVE_LOG_LEVEL_DEFINE "QUILL_COMPILE_ACTIVE_LOG_LEVEL_WARNING")
elseif (QUILL_ACTIVE_LOG_LEVEL_UPPER STREQUAL "ERROR")
    set(QUILL_ACTIVE_LOG_LEVEL_DEFINE "QUILL_COMPILE_ACTIVE_LOG_LEVEL_ERROR")
elseif (QUILL_ACTIVE_LOG_LEVEL_UPPER STREQUAL "CRITICAL")
    set(QUILL_ACTIVE_LOG_LEVEL_DEFINE "QUILL_COMPILE_ACTIVE_LOG_LEVEL_CRITICAL")
else()
    message(FATAL_ERROR "Invalid QUILL_ACTIVE_LOG_LEVEL: ${QUILL_ACTIVE_LOG_LEVEL}")
endif()

# need fetchcontent for pulling quill
include(FetchContent)
include(CTest)

# bring quill in at configure time
FetchContent_Declare(
    quill
    GIT_REPOSITORY https://github.com/odygrd/quill.git
    GIT_TAG v9.0.3
)
FetchContent_MakeAvailable(quill)

# find system libs, these should be installed by package manager
# Find packages installed via pacman
find_package(SDL2 REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)

# GLM is header only, just find the headers
# GLM is header only — cmake findPath just ensures its headers are available
find_path(GLM_INCLUDE_DIR glm/glm.hpp)

if (NOT GLM_INCLUDE_DIR)
    message(FATAL_ERROR "Could not find GLM include directory")
endif()

# main app executable
# Add executable
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/AppLayer.cpp
    src/Engine.cpp
    src/GameObject.cpp
    src/MeshDiscovery.cpp
    src/MeshLoader.cpp
    src/Quaternion.cpp
    src/Renderer3D.cpp
    src/RtsUnit.cpp
    src/SceneGraph.cpp
    src/SDL_Manager.cpp
    src/ShaderProgram.cpp
    src/Shape.cpp
    src/Sound.cpp
    src/SoundSystem.cpp
    src/Texture2D.cpp
    src/Utility.cpp
)

add_executable(doom_demo
    demo/DoomDemo.cpp
    src/MeshLoader.cpp
    src/Renderer3D.cpp
    src/SceneGraph.cpp
    src/SDL_Manager.cpp
    src/ShaderProgram.cpp
    src/Shape.cpp
    src/Sound.cpp
    src/SoundSystem.cpp
    src/Texture2D.cpp
)

# include dirs for glm
# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${GLM_INCLUDE_DIR}
)
target_include_directories(doom_demo PRIVATE
    src
    ${GLM_INCLUDE_DIR}
)

# link libs to the exe
# Link libs
target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL2::SDL2
    GLEW::GLEW
    OpenGL::GL
    quill::quill
)
target_link_libraries(doom_demo PRIVATE
    SDL2::SDL2
    GLEW::GLEW
    OpenGL::GL
    quill::quill
)

# compile defs for quill log level
target_compile_definitions(${PROJECT_NAME} PRIVATE
    QUILL_COMPILE_ACTIVE_LOG_LEVEL=${QUILL_ACTIVE_LOG_LEVEL_DEFINE}
)
target_compile_definitions(doom_demo PRIVATE
    QUILL_COMPILE_ACTIVE_LOG_LEVEL=${QUILL_ACTIVE_LOG_LEVEL_DEFINE}
)

add_executable(objective_b_benchmark
    tools/ObjectiveB_Benchmark.cpp
    src/GameObject.cpp
    src/Quaternion.cpp
    src/RtsUnit.cpp
)
target_include_directories(objective_b_benchmark PRIVATE
    src
    ${GLM_INCLUDE_DIR}
)

if (BUILD_TESTING)
    # test exe for objective b quaternion and game object behavior
    add_executable(objective_b_test
        tests/ObjectiveB_test.cpp
        src/Engine.cpp
        src/GameObject.cpp
        src/Quaternion.cpp
        src/RtsUnit.cpp
        src/Utility.cpp
    )
    target_include_directories(objective_b_test PRIVATE
        src
        ${GLM_INCLUDE_DIR}
    )
    # register test with ctest
    add_test(NAME objective_b_test COMMAND objective_b_test)

    add_executable(objective_e_test
        tests/ObjectiveE_test.cpp
        src/SceneGraph.cpp
    )
    target_include_directories(objective_e_test PRIVATE
        src
        ${GLM_INCLUDE_DIR}
    )
    add_test(NAME objective_e_test COMMAND objective_e_test)
endif()
