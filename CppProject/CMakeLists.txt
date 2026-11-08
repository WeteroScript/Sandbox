cmake_minimum_required(VERSION 3.22)
project(SandboxCpp VERSION 0.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(FetchContent)

if(ANDROID)
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
else()
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
endif()
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.4.20
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL3)

set(BOX2D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BOX2D_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(box2d
  GIT_REPOSITORY https://github.com/erincatto/box2d.git
  GIT_TAG v3.2.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(box2d)

if(ANDROID)
  add_library(main SHARED src/main.cpp src/SandboxGame.cpp src/PhysicsWorld.cpp)
  target_include_directories(main PRIVATE src)
  target_link_libraries(main PRIVATE SDL3::SDL3 box2d)
else()
  add_executable(sandbox src/main.cpp src/SandboxGame.cpp src/PhysicsWorld.cpp)
  target_include_directories(sandbox PRIVATE src)
  target_link_libraries(sandbox PRIVATE SDL3::SDL3 box2d)
endif()
